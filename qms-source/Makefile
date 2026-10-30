CC = gcc
CXX = g++
LD = ld
NASM = nasm
OBJCOPY = objcopy

KSRC = kernel
BUILD_DIR  = isodir/boot/kernel

COMMON_FLAGS = -m32 -ffreestanding -c -fno-stack-protector -fno-toplevel-reorder
INCLUDE_FLAGS = -I$(KSRC) -I$(KSRC)/drivers -I$(KSRC)/func -I$(KSRC)/func/lib -I$(KSRC)/func/lib/data -I$(KSRC)/func/io
CFLAGS = $(COMMON_FLAGS) $(INCLUDE_FLAGS)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
LUAJIT_BUILD_DIR = $(BUILD_DIR)/luajit-build
LUAJIT_BUILD_SRC = $(LUAJIT_BUILD_DIR)/src
LUAJIT_OBJECT_DIR = $(LUAJIT_BUILD_DIR)/obj
LUAJIT_STAMP = $(LUAJIT_BUILD_DIR)/.prepared
LUAJIT_SOURCE_FILES = $(shell find $(KSRC)/luajit/src $(KSRC)/luajit/dynasm -type f ! -name '*.o' ! -name '*.a' ! -name '*.so' ! -name 'luajit' ! -name 'buildvm' ! -name 'minilua') $(KSRC)/luajit/.relver
LUAJIT_C_SOURCES = $(wildcard $(KSRC)/luajit/src/lj_*.c)
LUAJIT_OBJECTS = $(patsubst $(KSRC)/luajit/src/%.c,$(LUAJIT_OBJECT_DIR)/%.o,$(LUAJIT_C_SOURCES)) $(LUAJIT_OBJECT_DIR)/lib_aux.o $(LUAJIT_OBJECT_DIR)/lib_base.o $(LUAJIT_OBJECT_DIR)/lib_math.o $(LUAJIT_OBJECT_DIR)/lib_string.o $(LUAJIT_OBJECT_DIR)/lib_buffer.o $(LUAJIT_OBJECT_DIR)/lj_vm.o
LUAJIT_FLAGS = -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -ffunction-sections -fdata-sections -DLUAJIT_OS=LUAJIT_OS_OTHER -DLUAJIT_DISABLE_JIT -DLUAJIT_DISABLE_FFI -DLUAJIT_DISABLE_PROFILE -DLUAJIT_DISABLE_VMEVENT -DLUAJIT_NO_UNWIND -DLUAJIT_SECURITY_PRNG=0 -DLUAJIT_USE_SYSMALLOC -I$(KSRC)
LIBGCC = $(shell $(CC) -m32 -print-libgcc-file-name)

LDFLAGS = -m elf_i386 -T linker.ld --gc-sections

DISK_IMG  = disk.img
DISK_SIZE = 32
DISK2_IMG = new_ferrite.img
DISK2_SIZE = 42

MKFSFAT := $(shell \
	for f in mkfs.fat mkfs.vfat; do \
		for d in "" /sbin/ /usr/sbin/ /usr/local/sbin/; do \
			if [ -x "$${d}$$f" ]; then echo "$${d}$$f"; break 2; fi; \
		done; \
	done)

C_SOURCES   := $(shell find $(KSRC) -name "*.c" \
	-not -path "$(KSRC)/luajit/*")
CPP_SOURCES := $(filter-out $(KSRC)/func/lua/luaheap_func_bridge.cpp,$(shell find $(KSRC) -name "*.cpp"))

C_OBJS   := $(patsubst $(KSRC)/%.c,  $(BUILD_DIR)/%.o, $(C_SOURCES))
CPP_OBJS := $(patsubst $(KSRC)/%.cpp,$(BUILD_DIR)/%.o, $(CPP_SOURCES))

ALL_OBJS  := $(C_OBJS) $(CPP_OBJS)
INIT_OBJ  := $(BUILD_DIR)/init.o
OTHER_OBJS:= $(filter-out $(INIT_OBJ),$(ALL_OBJS))
OBJS      := $(INIT_OBJ) $(OTHER_OBJS) $(LUAJIT_OBJECTS)

TARGET  = $(BUILD_DIR)/kernel.bin
QMS_ISO = qms.iso

.PHONY: all clean run rundisk run2disk run-disk iso kernel dirs disk disk-mbr disk-populate disk-ls disk-clean clean-all FORCE

all: iso run

run:
	@echo "Starting QEMU"
	qemu-system-i386 -m 64M -cdrom $(QMS_ISO)

rundisk: iso disk
	@echo "Starting QEMU"
	qemu-system-i386 -m 64M \
		-cdrom $(QMS_ISO) \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,media=disk \
		-boot order=d

run2disk: iso $(DISK2_IMG)
	@echo "Starting QEMU"
	qemu-system-i386 -m 64M \
		-cdrom $(QMS_ISO) \
		-drive file=$(DISK2_IMG),format=raw,if=ide,index=0,media=disk \
		-boot order=d

disk: $(DISK_IMG)

$(DISK_IMG):
	@echo "Creating $(DISK_IMG) ($(DISK_SIZE) MB, FAT16)"
	@if [ -z "$(MKFSFAT)" ]; then \
		echo ""; \
		echo "  ERROR: mkfs.fat / mkfs.vfat not found."; \
		echo "  Install:  sudo apt install dosfstools"; \
		echo ""; \
		exit 1; \
	fi
	@echo "$(MKFSFAT)"
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_SIZE) status=none
	$(MKFSFAT) -F 16 -S 512 $(DISK_IMG)
	@echo "ready"

FORCE:

$(DISK2_IMG): FORCE
	@if [ ! -f "$@" ]; then \
		echo "Creating $@ ($(DISK2_SIZE) MB, FAT16)"; \
		if [ -z "$(MKFSFAT)" ]; then \
			echo "ERROR: mkfs.fat / mkfs.vfat not found. Install dosfstools."; \
			exit 1; \
		fi; \
		dd if=/dev/zero of="$@" bs=1M count=$(DISK2_SIZE) status=none && \
		$(MKFSFAT) -F 16 -S 512 "$@" || exit 1; \
		echo "ready"; \
	else \
		size=$$(wc -c < "$@"); \
		target_size=$$(( $(DISK2_SIZE) * 1024 * 1024 )); \
		if [ "$$size" -lt "$$target_size" ]; then \
			echo "Expanding $@ to $(DISK2_SIZE) MB"; \
			truncate -s "$$target_size" "$@" || exit 1; \
		elif [ "$$size" -gt "$$target_size" ]; then \
			echo "ERROR: $@ is larger than $(DISK2_SIZE) MB; refusing to truncate it."; \
			exit 1; \
		fi; \
	fi

disk-mbr:
	@echo "Creating $(DISK_IMG) with MBR + FAT16 partition"
	@if [ -z "$(MKFSFAT)" ]; then echo "ERROR: mkfs.fat not found."; exit 1; fi
	
	@dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_SIZE) status=none
	
	@echo "Creating MBR partition table..."
	@printf "2048,,,6\n" | sfdisk --no-reread $(DISK_IMG) 2>/dev/null || \
	 (echo "sfdisk failed, trying parted..." && \
	  parted -s $(DISK_IMG) mklabel msdos mkpart primary fat16 1MiB 100% || \
	  (echo "ERROR: Neither sfdisk nor parted worked. Install parted: sudo apt install parted" && exit 1))
	
	@echo "Formatting FAT16 partition..."
	@$(MKFSFAT) -F 16 -S 512 --offset 2048 $(DISK_IMG)
	
	@echo "$(DISK_IMG) ready (MBR + FAT16 at LBA 2048)"

diskp: $(DISK_IMG)
	@if [ -d isodir/disk ]; then \
		echo "Copying files to $(DISK_IMG)"; \
		MTOOLS_SKIP_CHECK=1 mcopy -i $(DISK_IMG) -s isodir/disk/* ::/ 2>/dev/null || \
		(mkdir -p /tmp/qms_mnt && \
		 sudo mount -o loop,rw $(DISK_IMG) /tmp/qms_mnt && \
		 sudo cp -r isodir/disk/* /tmp/qms_mnt/ && \
		 sudo umount /tmp/qms_mnt); \
		echo "Done"; \
	else \
		echo "--- isodir/disk/ not found, skipping ---"; \
	fi

disk-ls: $(DISK_IMG)
	@echo "Contents of $(DISK_IMG)"
	MTOOLS_SKIP_CHECK=1 mdir -i $(DISK_IMG) :: 2>/dev/null || \
	(mkdir -p /tmp/qms_mnt && \
	 sudo mount -o loop,ro $(DISK_IMG) /tmp/qms_mnt && \
	 ls -la /tmp/qms_mnt && \
	 sudo umount /tmp/qms_mnt)

disk-clean:
	rm -f $(DISK_IMG)


$(STAGE1_BIN): $(STAGE1_SRC)
	@echo "Assembling Stage1..."
	$(NASM) -f bin $< -o $@

$(STAGE2_BIN): $(STAGE2_SRC)
	@echo "Assembling Stage2..."
	$(NASM) -f bin $< -o $@

kernel: dirs $(OBJS)
	@echo "Linking clean kernel..."
	$(LD) $(LDFLAGS) $(OBJS) $(LIBGCC) -o $(TARGET)
	@echo "Kernel built successfully (Clean version)."

iso: kernel
	@echo "Creating ISO..."
	grub-mkrescue -o $(QMS_ISO) isodir/


dirs:
	@echo "Preparing directories"
	@mkdir -p $(BUILD_DIR)
	@find $(KSRC) -type d | sed 's|^$(KSRC)|$(BUILD_DIR)|' | xargs mkdir -p

$(BUILD_DIR)/%.o: $(KSRC)/%.cpp
	@echo "Compiling C++: $< "
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(KSRC)/%.c
	@echo "Compiling C: $< "
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/func/io/printg.o: $(KSRC)/func/io/printg.h

$(BUILD_DIR)/func/lua/luaheap_func_bridge.o: $(KSRC)/func/lua/luaheap_func_bridge.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LUAJIT_STAMP): $(LUAJIT_SOURCE_FILES)
	@mkdir -p $(LUAJIT_BUILD_DIR)
	@cp -a $(KSRC)/luajit/src $(KSRC)/luajit/dynasm $(LUAJIT_BUILD_DIR)/
	@cp -a $(KSRC)/luajit/.relver $(LUAJIT_BUILD_DIR)/
	$(MAKE) -C $(LUAJIT_BUILD_SRC) clean
	$(MAKE) -C $(LUAJIT_BUILD_SRC) host/buildvm HOST_CC='gcc -m32' CC=gcc TARGET_FLAGS='-m32 -ffreestanding' TARGET_SYS=Other XCFLAGS='$(LUAJIT_FLAGS)'
	$(MAKE) -C $(LUAJIT_BUILD_SRC) lj_vm.S lj_bcdef.h lj_ffdef.h lj_libdef.h lj_recdef.h lj_folddef.h jit/vmdef.lua HOST_CC='gcc -m32' CC=gcc TARGET_FLAGS='-m32 -ffreestanding' TARGET_SYS=Other XCFLAGS='$(LUAJIT_FLAGS)'
	@touch $@

$(LUAJIT_OBJECT_DIR)/%.o: $(LUAJIT_STAMP)
	@mkdir -p $(LUAJIT_OBJECT_DIR)
	$(CC) $(LUAJIT_FLAGS) -I$(LUAJIT_BUILD_SRC) -c $(LUAJIT_BUILD_SRC)/$*.c -o $@

$(LUAJIT_OBJECT_DIR)/lj_vm.o: $(LUAJIT_STAMP)
	@mkdir -p $(LUAJIT_OBJECT_DIR)
	$(CC) $(LUAJIT_FLAGS) -c $(LUAJIT_BUILD_SRC)/lj_vm.S -o $@

clean:
	@echo "Full clean"
	rm -rf isodir/boot/kernel/* qms.iso isodir/boot/kernel/kernel.o

clean-all: clean disk-clean
