CC = g++
LD = ld
NASM = nasm
OBJCOPY = objcopy

KSRC = kernel
BUILD_DIR  = isodir/boot/kernel

CFLAGS = -m32 -ffreestanding -c -fno-exceptions -fno-rtti -fno-stack-protector -fno-toplevel-reorder
CFLAGS += -I$(KSRC) -I$(KSRC)/drivers -I$(KSRC)/func -I$(KSRC)/func/lib -I$(KSRC)/func/lib/data -I$(KSRC)/func/io

LDFLAGS = -m elf_i386 -T linker.ld

DISK_IMG  = disk.img
DISK_SIZE = 32

MKFSFAT := $(shell \
	for f in mkfs.fat mkfs.vfat; do \
		for d in "" /sbin/ /usr/sbin/ /usr/local/sbin/; do \
			if [ -x "$${d}$$f" ]; then echo "$${d}$$f"; break 2; fi; \
		done; \
	done)

C_SOURCES   := $(shell find $(KSRC) -name "*.c")
CPP_SOURCES := $(shell find $(KSRC) -name "*.cpp")

C_OBJS   := $(patsubst $(KSRC)/%.c,  $(BUILD_DIR)/%.o, $(C_SOURCES))
CPP_OBJS := $(patsubst $(KSRC)/%.cpp,$(BUILD_DIR)/%.o, $(CPP_SOURCES))

ALL_OBJS  := $(C_OBJS) $(CPP_OBJS)
INIT_OBJ  := $(BUILD_DIR)/init.o
OTHER_OBJS:= $(filter-out $(INIT_OBJ),$(ALL_OBJS))
OBJS      := $(INIT_OBJ) $(OTHER_OBJS)

TARGET  = $(BUILD_DIR)/kernel.bin
QMS_ISO = qms.iso

.PHONY: all clean run run-disk iso kernel dirs disk disk-mbr disk-populate disk-ls disk-clean clean-all

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
	$(LD) $(LDFLAGS) $(OBJS) -o $(TARGET)
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
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(KSRC)/%.c
	@echo "Compiling C: $< "
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

clean:
	@echo "Full clean"
	rm -rf isodir/boot/kernel/* qms.iso isodir/boot/kernel/kernel.o

clean-all: clean disk-clean
