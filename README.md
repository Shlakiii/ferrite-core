# Ferrite-Core (In Development)

**Ferrite-Core** — это экспериментальный форк ОС [QMS-Kernel](https://github.com/hecer-hub/qms-kernel), созданный для интеграции среды выполнения **LuaJIT** непосредственно на уровень 32-битного x86 ядра.

## Вектор развития (Что планируется изменить):
1. **Портирование LuaJIT:** Внедрение виртуальной машины Lua в ядро для исполнения скриптов с JIT-компиляцией.
2. **Скриптовое окружение:** Дополнение стандартного шелла на Lua REPL.

---

*Ниже приведено описание базовой архитектуры QMS-Kernel, которая используется как фундамент для Ferrite-Core:*

# QMS OS V1.0.0

*A simple 32-bit hobby operating system built from scratch.*

---

QMS OS is a lightweight educational operating system written in C and C++ for the i386 architecture. It features its own terminal, basic userspace, and multiple filesystem support.

![Screenshot](assets/images/qms_os_run.png)

> **Important**  
> QMS OS works reliably **only in QEMU**. Real hardware support is not guaranteed and may lead to instability or data corruption.

---

## Features

### Core
- 32-bit protected mode
- Framebuffer terminal with scrollback buffer
- Basic memory management
- IDT and interrupt handling
- ATA PIO driver
- QEX Aplications

### Filesystems
- QFS (native simple filesystem)
- FAT16 (read/write)
- FAT32 (read/write)

### Drivers
- PS/2 keyboard (standard and laptop layouts)
- Serial port (COM)
- LFB mode

---

## Building

```
make
```

To build only the kernel:

```
make kernel
```

### Running in QEMU:

```
make run     # build and run (no disk)
make rundisk # build and run
```

---

## Project Structure

```
qms-src/
├── kernel/          - Kernel source code
├── assets/          - Documentation and images
├── disk.img         - Bootable disk image
└── Makefile         - Build make script
 ...
```

---

## Documentation

Detailed documentation is available in the [`assets/doc/`](assets/doc/) directory:

---

## Disclaimer

This is an **early experimental release** (Version 1.0.0).  
The system is developed for learning and hobby purposes.

---

## Лицензия и происхождение
Проект **Ferrite-Core** основан на кодовой базе **QMS-Kernel** (автор: [Roit](https://github.com/hecer-hub)). 
Оригинальный код и текущие изменения распространяются под лицензией **BSD 2-Clause**. Все оригинальные копирайты сохранены.
