#ifndef _MEMORY_H
#define _MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096

#define SLAB_CLASS_COUNT 6
static constexpr size_t SLAB_SIZES[SLAB_CLASS_COUNT] = { 8, 16, 32, 64, 128, 256 };

static constexpr uint32_t BLOCK_MAGIC      = 0xDEADBEEFu;
static constexpr uint32_t BLOCK_MAGIC_FREE = 0xFEEDFACEu;
static constexpr uint32_t SLAB_MAGIC       = 0xCAFEBABEu;

struct alignas(8) BlockHeader {
    size_t   size;
    uint32_t magic;
    uint32_t flags;
};

struct SlabHeader {
    uint32_t    magic;
    uint16_t    slot_size;
    uint16_t    free_count;
    SlabHeader* next_slab;
    void*       free_list;
};

struct MemoryStats {
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
    size_t total_pages;
    size_t used_pages;
    size_t free_pages;
    size_t alloc_count;
    size_t slab_allocs;
    size_t page_allocs;
};

enum class MemError : uint8_t {
    OK = 0,
    OUT_OF_MEMORY,
    DOUBLE_FREE,
    INVALID_PTR,
    CORRUPTED_HEADER,
    BAD_ALIGNMENT,
};

class MemoryManager {
public:
    MemoryManager() = default;
    MemoryManager(uintptr_t start, size_t size);

    void*  allocate(size_t size);
    void   free(void* p, size_t size);
    void   free(void* p);

    size_t get_total_size() const;
    size_t get_free_size()  const;
    size_t get_used_size()  const;

    void*  allocate_aligned(size_t size, size_t align);

    void*  reallocate(void* ptr, size_t new_size);

    void*  allocate_zero(size_t count, size_t elem_size);

    bool   is_valid_ptr(const void* p) const;

    MemoryStats get_stats()      const;
    MemError    get_last_error() const { return last_error; }
    const char* error_string(MemError e) const;

    MemoryManager(const MemoryManager&)            = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

private:
    uint8_t*  bitmap;
    uintptr_t base_addr;
    size_t    total_pages;
    size_t    free_pages;
    size_t    bitmap_pages;
    size_t    next_free_hint;

    SlabHeader* slab_heads[SLAB_CLASS_COUNT];

    size_t alloc_count;
    size_t slab_allocs;
    size_t page_allocs;

    mutable MemError last_error;

    void   mark_page    (size_t page_idx, bool used);
    bool   is_page_used (size_t page_idx) const;
    size_t find_free_pages(size_t pages_needed);
    static size_t pages_for(size_t size);

    void*  alloc_pages_raw(size_t pages_count);
    void   free_pages_raw (void* p, size_t pages_count);

    int         slab_class_for (size_t size) const;
    void*       slab_alloc     (size_t size);
    void        slab_free      (void* p, SlabHeader* slab);
    SlabHeader* slab_find_owner(void* p) const;
    SlabHeader* slab_new_page  (size_t slot_size, int cls);
};

extern MemoryManager* kernel_mem_manager;
extern MemoryManager* user_mem_manager;

void init_memory_manager(uintptr_t start, size_t size);

void* operator new  (size_t size);
void* operator new[](size_t size);
void  operator delete  (void* p) noexcept;
void  operator delete[](void* p) noexcept;
void  operator delete  (void* p, size_t size) noexcept;
void  operator delete[](void* p, size_t size) noexcept;

inline void* operator new  (size_t, void* p) noexcept { return p; }
inline void* operator new[](size_t, void* p) noexcept { return p; }
inline void  operator delete  (void*, void*) noexcept {}
inline void  operator delete[](void*, void*) noexcept {}

#endif
