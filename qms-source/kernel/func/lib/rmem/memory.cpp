#include "memory.h"
#include <io/printg.h>
#include <lib/strtools/string.h>

extern MemoryManager* kernel_mem_manager;
extern MemoryManager* lua_mem_manager;
extern MemoryManager* user_mem_manager;

void init_memory_manager(uintptr_t start, size_t size) {
    if (size <= sizeof(MemoryManager) + PAGE_SIZE) return;

    kernel_mem_manager = reinterpret_cast<MemoryManager*>(start);

    uintptr_t real_start = start + sizeof(MemoryManager);
    size_t    real_size  = size  - sizeof(MemoryManager);

    new (kernel_mem_manager) MemoryManager(real_start, real_size);

    user_mem_manager = kernel_mem_manager;
}

MemoryManager::MemoryManager(uintptr_t start, size_t size) {
    bitmap      = reinterpret_cast<uint8_t*>(start);
    total_pages = size / PAGE_SIZE;
    free_pages  = total_pages;

    size_t bitmap_bytes = (total_pages + 7) / 8;
    memset(bitmap, 0, bitmap_bytes);

    bitmap_pages   = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    base_addr      = start;
    next_free_hint = bitmap_pages;

    for (size_t i = 0; i < bitmap_pages; ++i)
        mark_page(i, true);

    for (int i = 0; i < SLAB_CLASS_COUNT; ++i)
        slab_heads[i] = nullptr;

    alloc_count = 0;
    slab_allocs = 0;
    page_allocs = 0;
    last_error  = MemError::OK;
}

void MemoryManager::mark_page(size_t page_idx, bool used) {
    if (page_idx >= total_pages) return;

    bool currently_used = is_page_used(page_idx);

    if (used && !currently_used) {
        bitmap[page_idx / 8] |= static_cast<uint8_t>(1u << (page_idx % 8));
        if (free_pages > 0) --free_pages;
    } else if (!used && currently_used) {
        bitmap[page_idx / 8] &= static_cast<uint8_t>(~(1u << (page_idx % 8)));
        ++free_pages;
        if (page_idx < next_free_hint)
            next_free_hint = page_idx;
    }
}

bool MemoryManager::is_page_used(size_t page_idx) const {
    if (page_idx >= total_pages) return true;
    return (bitmap[page_idx / 8] >> (page_idx % 8)) & 1u;
}

size_t MemoryManager::find_free_pages(size_t pages_needed) {
    if (pages_needed == 0) return SIZE_MAX;

    for (int pass = 0; pass < 2; ++pass) {
        size_t search_start = (pass == 0) ? next_free_hint : bitmap_pages;
        size_t search_end   = (pass == 0) ? total_pages    : next_free_hint;

        size_t found = 0, start = 0;
        for (size_t i = search_start; i < search_end; ++i) {
            if (!is_page_used(i)) {
                if (found == 0) start = i;
                if (++found == pages_needed) {
                    next_free_hint = start + pages_needed;
                    return start;
                }
            } else {
                found = 0;
            }
        }
    }
    return SIZE_MAX;
}

size_t MemoryManager::pages_for(size_t size) {
    return (size + sizeof(BlockHeader) + PAGE_SIZE - 1) / PAGE_SIZE;
}

bool MemoryManager::is_valid_ptr(const void* p) const {
    if (!p) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    return addr >= base_addr && addr < base_addr + total_pages * PAGE_SIZE;
}

void* MemoryManager::alloc_pages_raw(size_t pages_count) {
    size_t start = find_free_pages(pages_count);
    if (start == SIZE_MAX) {
        last_error = MemError::OUT_OF_MEMORY;
        return nullptr;
    }
    for (size_t i = start; i < start + pages_count; ++i)
        mark_page(i, true);
    return reinterpret_cast<void*>(base_addr + start * PAGE_SIZE);
}

void MemoryManager::free_pages_raw(void* p, size_t pages_count) {
    if (!p) return;
    uintptr_t addr       = reinterpret_cast<uintptr_t>(p);
    size_t    start_page = (addr - base_addr) / PAGE_SIZE;
    for (size_t i = start_page; i < start_page + pages_count; ++i)
        mark_page(i, false);
}

int MemoryManager::slab_class_for(size_t size) const {
    for (int i = 0; i < SLAB_CLASS_COUNT; ++i)
        if (size <= SLAB_SIZES[i]) return i;
    return -1;
}

SlabHeader* MemoryManager::slab_new_page(size_t slot_size, int cls) {
    if (slot_size == 0 || cls < 0 || cls >= SLAB_CLASS_COUNT) return nullptr;

    void* page = alloc_pages_raw(1);
    if (!page) return nullptr;

    SlabHeader* slab  = reinterpret_cast<SlabHeader*>(page);
    slab->magic       = SLAB_MAGIC;
    slab->slot_size   = static_cast<uint16_t>(slot_size);
    slab->next_slab   = slab_heads[cls];
    slab_heads[cls]   = slab;

    uintptr_t data_start = reinterpret_cast<uintptr_t>(page) + sizeof(SlabHeader);
    data_start = (data_start + slot_size - 1) & ~(slot_size - 1);

    size_t slots = (PAGE_SIZE - (data_start - reinterpret_cast<uintptr_t>(page))) / slot_size;
    slab->free_count = static_cast<uint16_t>(slots);

    void* prev = nullptr;
    for (size_t i = slots; i-- > 0;) {
        void** slot = reinterpret_cast<void**>(data_start + i * slot_size);
        *slot = prev;
        prev  = slot;
    }
    slab->free_list = prev;

    return slab;
}

void* MemoryManager::slab_alloc(size_t size) {
    int cls = slab_class_for(size);
    if (cls < 0) return nullptr;

    size_t slot_size = SLAB_SIZES[cls];

    SlabHeader* slab = slab_heads[cls];
    while (slab && slab->free_count == 0)
        slab = slab->next_slab;

    if (!slab) {
        slab = slab_new_page(slot_size, cls);
        if (!slab) return nullptr;
    }

    void** slot     = reinterpret_cast<void**>(slab->free_list);
    slab->free_list = *slot;
    --slab->free_count;

    ++slab_allocs;
    ++alloc_count;
    return slot;
}

SlabHeader* MemoryManager::slab_find_owner(void* p) const {
    if (!p) return nullptr;
    uintptr_t addr      = reinterpret_cast<uintptr_t>(p);
    uintptr_t page_addr = addr & ~(uintptr_t)(PAGE_SIZE - 1);

    uintptr_t mem_end = base_addr + total_pages * PAGE_SIZE;
    if (page_addr < base_addr || page_addr + sizeof(SlabHeader) > mem_end)
        return nullptr;

    SlabHeader* candidate = reinterpret_cast<SlabHeader*>(page_addr);
    if (candidate->magic == SLAB_MAGIC) return candidate;
    return nullptr;
}

void MemoryManager::slab_free(void* p, SlabHeader* slab) {
    void** slot     = reinterpret_cast<void**>(p);
    *slot           = slab->free_list;
    slab->free_list = slot;
    ++slab->free_count;

    if (alloc_count > 0) --alloc_count;

    uintptr_t page_addr  = reinterpret_cast<uintptr_t>(slab);
    uintptr_t data_start = page_addr + sizeof(SlabHeader);
    size_t    slot_size  = slab->slot_size;
    data_start = (data_start + slot_size - 1) & ~(slot_size - 1);
    size_t max_slots = (PAGE_SIZE - (data_start - page_addr)) / slot_size;

    if (slab->free_count < max_slots) return;

    int cls = slab_class_for(slot_size);
    if (cls < 0) return;

    SlabHeader** cur = &slab_heads[cls];
    while (*cur && *cur != slab) cur = &(*cur)->next_slab;
    if (*cur) *cur = slab->next_slab;

    slab->magic = 0;
    free_pages_raw(slab, 1);
}

void* MemoryManager::allocate(size_t size) {
    if (size == 0) return nullptr;

    last_error = MemError::OK;

    if (slab_class_for(size) >= 0) {
        void* p = slab_alloc(size);
        if (!p) {
            last_error = MemError::OUT_OF_MEMORY;
            printg("OUT OF MEMORY (slab)");
        }
        return p;
    }

    size_t pages_needed = pages_for(size);
    size_t start_page   = find_free_pages(pages_needed);
    if (start_page == SIZE_MAX) {
        last_error = MemError::OUT_OF_MEMORY;
        printg("OUT OF MEMORY");
        return nullptr;
    }

    for (size_t j = start_page; j < start_page + pages_needed; ++j)
        mark_page(j, true);

    uintptr_t    block_addr = base_addr + start_page * PAGE_SIZE;
    BlockHeader* hdr        = reinterpret_cast<BlockHeader*>(block_addr);
    hdr->size  = size;
    hdr->magic = BLOCK_MAGIC;
    hdr->flags = 0;

    ++page_allocs;
    ++alloc_count;
    
    return reinterpret_cast<void*>(block_addr + sizeof(BlockHeader));
}

void MemoryManager::free(void* p) {
    if (!p) return;
    last_error = MemError::OK;

    SlabHeader* slab = slab_find_owner(p);
    if (slab) {
        slab_free(p, slab);
        return;
    }

    uintptr_t    data_addr = reinterpret_cast<uintptr_t>(p);
    BlockHeader* hdr       = reinterpret_cast<BlockHeader*>(data_addr - sizeof(BlockHeader));

    if (!is_valid_ptr(hdr)) {
        last_error = MemError::INVALID_PTR;
        printg("free(): invalid pointer");
        return;
    }
    if (hdr->magic == BLOCK_MAGIC_FREE) {
        last_error = MemError::DOUBLE_FREE;
        printg("free(): double-free detected!");
        return;
    }
    if (hdr->magic != BLOCK_MAGIC) {
        last_error = MemError::CORRUPTED_HEADER;
        printg("free(): corrupted block header");
        return;
    }

    size_t size      = hdr->size;
    hdr->magic       = BLOCK_MAGIC_FREE;
    uintptr_t baddr  = reinterpret_cast<uintptr_t>(hdr);
    size_t start     = (baddr - base_addr) / PAGE_SIZE;
    size_t n         = pages_for(size);

    for (size_t i = start; i < start + n; ++i)
        mark_page(i, false);

    if (alloc_count > 0) --alloc_count;
}

void MemoryManager::free(void* p, size_t size) {
    if (!p) return;
    last_error = MemError::OK;

    SlabHeader* slab = slab_find_owner(p);
    if (slab) {
        slab_free(p, slab);
        return;
    }

    uintptr_t    data_addr = reinterpret_cast<uintptr_t>(p);
    BlockHeader* hdr       = reinterpret_cast<BlockHeader*>(data_addr - sizeof(BlockHeader));

    uintptr_t block_addr;
    size_t    real_size;

    if (is_valid_ptr(hdr) && hdr->magic == BLOCK_MAGIC) {
        real_size  = hdr->size;
        block_addr = reinterpret_cast<uintptr_t>(hdr);
        hdr->magic = BLOCK_MAGIC_FREE;
    } else if (size > 0) {
        block_addr = data_addr;
        real_size  = size;
    } else {
        last_error = MemError::INVALID_PTR;
        printg("free(p,size): invalid pointer and zero size");
        return;
    }

    if (block_addr < base_addr || block_addr >= base_addr + total_pages * PAGE_SIZE) {
        last_error = MemError::INVALID_PTR;
        printg("free(p,size): address out of managed range");
        return;
    }

    size_t start_page    = (block_addr - base_addr) / PAGE_SIZE;
    size_t pages_to_free = (real_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = start_page; i < start_page + pages_to_free; ++i)
        mark_page(i, false);

    if (alloc_count > 0) --alloc_count;
}

void* MemoryManager::allocate_aligned(size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0 || align < sizeof(void*)) {
        last_error = MemError::BAD_ALIGNMENT;
        printg("allocate_aligned(): bad alignment");
        return nullptr;
    }
    if (size == 0) return nullptr;

    size_t overhead = align + sizeof(void*);
    if (size > SIZE_MAX - overhead) {
        last_error = MemError::OUT_OF_MEMORY;
        printg("allocate_aligned(): size overflow");
        return nullptr;
    }
    size_t total = size + overhead;
    void*  raw   = allocate(total);
    if (!raw) return nullptr;

    uintptr_t raw_addr    = reinterpret_cast<uintptr_t>(raw);
    uintptr_t aligned_addr = (raw_addr + sizeof(void*) + align - 1) & ~(align - 1);

    reinterpret_cast<void**>(aligned_addr)[-1] = raw;
    return reinterpret_cast<void*>(aligned_addr);
}

void* MemoryManager::reallocate(void* ptr, size_t new_size) {
    if (!ptr)     return allocate(new_size);
    if (!new_size) { free(ptr); return nullptr; }

    size_t old_size = 0;
    SlabHeader* slab = slab_find_owner(ptr);
    if (slab) {
        old_size = slab->slot_size;
    } else {
        uintptr_t    data_addr = reinterpret_cast<uintptr_t>(ptr);
        BlockHeader* hdr       = reinterpret_cast<BlockHeader*>(data_addr - sizeof(BlockHeader));
        if (!is_valid_ptr(hdr) || hdr->magic != BLOCK_MAGIC) {
            last_error = MemError::INVALID_PTR;
            printg("reallocate(): invalid pointer");
            return nullptr;
        }
        old_size = hdr->size;
    }

    if (new_size == old_size) return ptr;

    void* new_ptr = allocate(new_size);
    if (!new_ptr) return nullptr;

    size_t copy_size = (new_size < old_size) ? new_size : old_size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);

    return new_ptr;
}

void* MemoryManager::allocate_zero(size_t count, size_t elem_size) {
    if (elem_size && count > SIZE_MAX / elem_size) {
        last_error = MemError::OUT_OF_MEMORY;
        return nullptr;
    }
    size_t total = count * elem_size;
    void*  p     = allocate(total);
    if (p) memset(p, 0, total);
    return p;
}

size_t MemoryManager::get_total_size() const { return total_pages * PAGE_SIZE; }
size_t MemoryManager::get_free_size()  const { return free_pages  * PAGE_SIZE; }
size_t MemoryManager::get_used_size()  const { return get_total_size() - get_free_size(); }

MemoryStats MemoryManager::get_stats() const {
    return MemoryStats{
        get_total_size(),
        get_used_size(),
        get_free_size(),
        total_pages,
        total_pages - free_pages,
        free_pages,
        alloc_count,
        slab_allocs,
        page_allocs,
    };
}

const char* MemoryManager::error_string(MemError e) const {
    switch (e) {
        case MemError::OK:               return "OK";
        case MemError::OUT_OF_MEMORY:    return "Out of memory";
        case MemError::DOUBLE_FREE:      return "Double free";
        case MemError::INVALID_PTR:      return "Invalid pointer";
        case MemError::CORRUPTED_HEADER: return "Corrupted block header";
        case MemError::BAD_ALIGNMENT:    return "Bad alignment value";
        default:                         return "Unknown error";
    }
}

void* operator new  (size_t size)           { return kernel_mem_manager->allocate(size); }
void* operator new[](size_t size)           { return operator new(size); }

void  operator delete  (void* p) noexcept   { if (kernel_mem_manager) kernel_mem_manager->free(p); }
void  operator delete[](void* p) noexcept   { operator delete(p); }

void  operator delete  (void* p, size_t size) noexcept {
    if (kernel_mem_manager) kernel_mem_manager->free(p, size);
}
void  operator delete[](void* p, size_t size) noexcept {
    operator delete(p, size);
}

extern "C" {
    void* user_malloc(size_t size) {
        return user_mem_manager->allocate(size);
    }
    void user_free(void* p) {
        user_mem_manager->free(p);
    }

    void* k_allocate(size_t size) {
        return kernel_mem_manager->allocate(size);
    }
    void k_free(void* p) {
        kernel_mem_manager->free(p);
    }
    void* k_realloc(void* ptr, size_t new_size) {
        return kernel_mem_manager->reallocate(ptr, new_size);
    }

    void* l_alloc(size_t size) {
        return lua_mem_manager->allocate(size);
    }
    void* l_free(void* p) {
        lua_mem_manager->free(p);
    }
    void* l_realloc(void* ptr, size_t new_size) {
        return lua_mem_manager->reallocate(ptr, new_size);
    }
}
