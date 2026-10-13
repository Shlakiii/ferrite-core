[Documentation]

Kernel version:  1.0.0
Architecture:    x86
License:         BSD 2-Clause
Developer:       Roit
Development help:
    C0000374


1. GENERAL DESCRIPTION

QMS OS (also known as QMS Kernel) is a hobby operating system written in
C and C++ programming languages with a small amount of inline assembly.
The system is intended exclusively for running in the QEMU emulator and
is not recommended for running on real hardware.
The kernel is loaded via the GRUB2 bootloader using the Multiboot protocol
(standard for GRUB).
Multitasking is currently absent and is planned for future releases.


2. REQUIREMENTS

To run QMS OS, a QEMU emulator version supporting qemu-system-i386 is required.
Minimum RAM: 5 MB.
Recommended: >=10 MB.
Running on real hardware is not recommended and not supported (for now).

Build tools required:
  - g++ with 32-bit compilation support
  - ld
  - nasm
  - objcopy
  - grub-mkrescue
  - xorriso
  - qemu-system-i386


3. BUILD AND RUN

make         - Build kernel and ISO, run without disk.
make iso     - Build kernel and ISO without running.
make rundisk - Build kernel and ISO, run with disk.
make clean   - Clean built files and temporary files after build


5. MEMORY MANAGEMENT

Page size: 4096 bytes.
Slab classes: 8, 16, 32, 64, 128, 256 bytes.

At startup, memory is divided into two regions:
  - Kernel heap - 1/16 of available memory, minimum 2 MB
  - User heap - the remainder

The size of both heaps is determined dynamically depending on the
amount of available memory.

Available MemoryManager operations:
  allocate(size)                - allocate memory.
  free(ptr)                     - free memory.
  reallocate(ptr, new_size)     - resize block.
  allocate_aligned(size, align) - allocate with alignment.
  allocate_zero(count, elem)    - allocate and zero.
  is_valid_ptr(ptr)             - check pointer validity.
  get_stats()                   - memory usage statistics.
  get_last_error()              - last error code.

MemError codes:
  OK, 
  OUT_OF_MEMORY, 
  DOUBLE_FREE, 
  INVALID_PTR,
  CORRUPTED_HEADER, 
  BAD_ALIGNMENT.


6. VIRTUAL FILE SYSTEM - VFS

VFS provides a unified interface for working with various file systems.

VFS_Error codes:
  VFS_OK             =  0
  VFS_ERR_NOT_FOUND  = -1
  VFS_ERR_EXISTS     = -2
  VFS_ERR_NO_SPACE   = -3
  VFS_ERR_NOT_FILE   = -4
  VFS_ERR_NOT_DIR    = -5
  VFS_ERR_NO_FS      = -6
  VFS_ERR_BAD_ARG    = -7
  VFS_ERR_IO         = -8
  VFS_ERR_TOO_BIG    = -9
  VFS_ERR_NO_FD      = -10
  VFS_ERR_BAD_FD     = -11
  VFS_ERR_NOT_IMPL   = -12
  VFS_ERR_NOT_EMPTY  = -13
  VFS_ERR_BUSY       = -14
  VFS_ERR_PERM       = -15

Supported file systems:
  QFS   - proprietary file system (ram only)
  FAT16 - FAT16 partition support (external disks)
  FAT32 - FAT32 partition support (external disks)

Main VFS API functions:
  vfs_open(path, flags)         - open file, returns fd
  vfs_close(fd)                 - close file
  vfs_read(fd, buf, size)       - read from file
  vfs_write(fd, buf, size)      - write to file
  vfs_seek(fd, offset, whence)  - seek in file
  vfs_stat_path(path, stat_out) - get file/dir info
  vfs_readdir(path)             - read directory contents
  vfs_mkdir(path)               - create directory
  vfs_rmdir(path)               - remove directory
  vfs_unlink(path)              - delete file
  vfs_getcwd()                  - get current directory
  vfs_cd(path)                  - change current directory
  vfs_mount(mp, fs, ...)        - mount file system
  vfs_umount(mp)                - unmount file system


7. DEVICE DRIVERS

7.1 ATA/IDE PIO

Driver implements ATA disk operation in PIO mode.
Supports up to 4 disks.

7.2 PCI

PCI bus initialization and device enumeration.

7.3 Keyboard (PS/2)

Two drivers supported:
  kkd_ps2_standard   - standard PS/2 keyboard
  kkd_laptop_generic - laptop keyboard (generic)

7.4 Audio

PCS driver - sound playback via internal PC Speaker.

7.5 Framebuffer

Mode: 1024x768, 32 bits per pixel (standard mode, but can be changed
in the Multiboot header).

7.6 Serial Port

Used for debug output. Functions: serial_print, read_serial, serial_received.


8. USERS AND ACCESS

The system supports two predefined users:
  user - regular user
  root - privileged user

The root password is predefined in metad.h as "12345678"

User switching is done via the su command with the -s flag in QMSH.
Privileged mode is indicated by [R] in the prompt.
Regular mode - [U].


9. COMMAND INTERPRETER QMSH

QMSH version: 3.4

Command history: 32 entries (not yet fully implemented).
Maximum command length: 256 characters.

Prompt format:
  <username>[R|U]@QMS[<current_directory>]#

Built-in commands:
  help               - show command list
  whoami             - show current username
  ls                 - list files in directory
  cd <path>          - change directory
  cp <path> <path>   - copy file or directory
  mv <src> <dst>     - move or rename file or directory
  pwd                - show current directory
  touch <file>       - create empty file
  mkdir <dir>        - create directory
  rm <path>          - delete file or directory
  cat <file>         - display file contents
  run <file.qex>     - run QEX application (if command was run with root privileges, the app will also run with root privileges)
  su                 - switch to root (requires password)
  lsblk              - show ATA disks
  mount <disk> <mp>  - mount file system
  umount <mp>        - unmount file system
  dd if=<disk> of=<file> bs=512 count=N skip=N  - disk dump
  format <disk> [fat16|fat32]  - format partition
  moudump            - show mount points list
  clear              - clear screen
  mem                - memory usage statistics
  sinfo              - system information
  about              - QMS OS information
  reboot             - reboot system
  shutdown           - shut down system


10. APPLICATIONS AND SBI SDK

QMS OS supports running user QEX applications.
The format is based on ELF for convenience.

10.1 SBI header format:
  magic          - magic number 0x514245 (SBI_MAGIC)
  sdk_ver        - SDK version (current: 4)
  is_root        - root application flag (0 or 1)
  app_name[16]   - application name
  entry_point    - entry point address
  noroot_tab_off - non-root function table offset
  root_tab_off   - root function table offset

Changing magic, sdk_ver, entry_point, or root_tab_off will break
compatibility with older applications.
Supported SDK versions: 3, 4.

10.2 Loader error codes:
  SBIL_OK               =  0  - success
  SBIL_ERR_BAD_MAGIC    = -1  - invalid magic
  SBIL_ERR_SDK_INCOMPAT = -2  - incompatible SDK version
  SBIL_ERR_NO_MEMORY    = -3  - no memory
  SBIL_ERR_BAD_PATH     = -4  - invalid path
  SBIL_ERR_NO_SLOT      = -5  - no slot
  SBIL_ERR_FS_READ      = -6  - FS read error
  SBIL_ERR_ROOT_DENIED  = -7  - root privileges required
  SBIL_ERR_FILE_TOOBIG  = -8  - file too large
  SBIL_ERR_BAD_ENTRY    = -9  - invalid entry point

10.3 Available SDK functions (no root - regular applications)
  get_arg, clear_arg, set_arg     - argument handling
  log(level, fmt, ...)            - log output
  printg(fmt, ...)                - text output
  printgc(t, b, fmt, ...)         - colored text output (fg, bg)
  clear_screen()                  - clear screen
  goto_xy(x, y)                   - move cursor
  tscreen_h(), tscreen_w()        - screen dimensions
  tcursor_x(), tcursor_y()        - cursor position
  serial_print, read_serial, serial_received - COM port operations
  get_char()                      - read character from keyboard
  kbd_get_modifiers()             - keyboard modifiers
  get_special_key()               - special key
  math_add, math_sub, math_mul, math_div, math_compare - arithmetic
  memset, memcpy, memcmp          - memory operations
  strcmp, strncmp, strlen, strcpy, strncpy, strcat, strchr, strstr, strtok
  malloc(size), free(ptr)         - user memory
  get_cpu_vendor, get_cpu_model   - CPU info
  get_core_name, get_core_ver, get_core_tag, get_core_arch - get kernel metadata
  fload_set, fload_get_count, fload_get_name_index - font loading
  vfs_open, vfs_close, vfs_read, vfs_write, vfs_seek
  vfs_stat_path, vfs_readdir, vfs_mkdir, vfs_rmdir, vfs_unlink
  vfs_getcwd, vfs_cd              - file system
  show_gbanner, show_banner       - banner display
  sbil_run(path)                  - run another QEX application
  sbil_check(path)                - check QEX without running

10.4 Additional SDK functions (root only)
  inb, outb, inw, outw            - direct port I/O
  k_allocate, k_free, k_realloc   - kernel heap
  vfs_register_driver             - FS driver registration
  vfs_mount, vfs_umount, vfs_force_init - mounting
  fload_set_data                  - font data loading
  fb_putpixel, fb_clear, fb_draw_rect, fb_draw_char, fb_draw_image - direct framebuffer drawing
  reboot(), shutdown()            - power management

10.5 Application build
The qapp template is used for building.
Application structure:
  src/main.c           - main code
  src/clib/qlibc/      - SDK headers (sbi.h, io.h, vfs.h, etc.)

Minimal example:
  #include <qlibc/sbi.h>
  const char* appname = "myapp";
  int main(void) {
      os.printg("Hello World");
      return 0;
  }

Build:
  make APP_NAME=myapp

Result: bin/myapp.qex


11. FONTS AND ENCODING

The system supports bitmap fonts in PSF-like format.
Built-in fonts: cp866, ascii.


12. LICENSE

QMS OS is distributed under the BSD 2-Clause license.

Copyright (c) Roit
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


14. PROJECT CONTRIBUTORS

Developer:            Roit
Development help:     C0000374
