[Документация]

Версия ядра:  1.0.0
Архитектура:  x86
Лицензия:     BSD 2-Clause
Разработчик:  Roit
Помощь по разработке:
	C0000374


1. ОБЩЕЕ ОПИСАНИЕ

QMS OS (или же QMS Kernel) - хобби операционная система, написанная на языках
программирования C и C++ с использованием небольшого количества встроенного
ассемблера. Система предназначена исключительно для запуска в
эмуляторе QEMU и не рекомендуется для запуска на реальном железе.
Ядро загружается через загрузчик GRUB2 с использованием протокола Multiboot (тандартный для GRUB).
Многозадачность в текущей версии отсутствует и планируется в будущих выпусках.


2. ТРЕБОВАНИЯ

Для запуска QMS OS необходим эмулятор QEMU версии, поддерживающей
qemu-system-i386. Минимальный объём оперативной памяти: 5 МБ.
Рекомендуемый объём: >=10 МБ.
Запуск на реальном железе не рекомендуется и не поддерживается (пока что).

Для сборки системы необходимы инструменты:
  - g++ с поддержкой 32-битной компиляции
  - ld
  - nasm
  - objcopy
  - grub-mkrescue
  - xorriso
  - qemu-system-i386


3. СБОРКА И ЗАПУСК

make         - Сборка ядра и iso и запуск без диска.
make iso     - Сборка ядра и iso без запуска.
make rundisk - Сборка ядра и iso и запск с диском.
make clean   - Очистка собраных файлов и временых файлов после сборки


5. УПРАВЛЕНИЕ ПАМЯТЬЮ

Размер страницы: 4096 байт.
Slab классы: 8, 16, 32, 64, 128, 256 байт.

При старте память делится на два раздела:
  - Ядерная куча (kernel heap) - 1/16 от доступной памяти, минимум 2 МБ
  - Пользовательская куча (user heap) - остаток

Объём обеих куч определяется динамически в зависисмости от размера доступной памяти.

Доступные операции MemoryManager:
  allocate(size)                - выделить память.
  free(ptr)                     - освободить память.
  reallocate(ptr, new_size)     - изменить размер блока.
  allocate_aligned(size, align) - выделить с выравниванием.
  allocate_zero(count, elem)    - выделить и обнулить.
  is_valid_ptr(ptr)             - проверить корректность указателя.
  get_stats()                   - статистика использования памяти.
  get_last_error()              - код последней ошибки.

Коды ошибок MemError:
  OK, 
  OUT_OF_MEMORY, 
  DOUBLE_FREE, 
  INVALID_PTR,
  CORRUPTED_HEADER, 
  BAD_ALIGNMENT.


6. ВИРТУАЛЬНАЯ ФАЙЛОВАЯ СИСТЕМА - VFS

VFS предоставляет единый интерфейс для работы с различными файловыми
системами.

Коды ошибок VFS_Error:
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

Поддерживаемые файловые системы:
  QFS   - собственная файловая система (только для ram)
  FAT16 - поддержка FAT16 разделов (внешние диски)
  FAT32 - поддержка FAT32 разделов (внешние диски)

Основные функции VFS API:
  vfs_open(path, flags)         - открыть файл возвращает fd
  vfs_close(fd)                 - закрыть файл
  vfs_read(fd, buf, size)       - читать из файла
  vfs_write(fd, buf, size)      - писать в файл
  vfs_seek(fd, offset, whence)  - переместить позицию
  vfs_stat_path(path, stat_out) - получить информацию о файле/папке
  vfs_readdir(path)             - прочитать содержимое директории
  vfs_mkdir(path)               - создать директорию
  vfs_rmdir(path)               - удалить директорию
  vfs_unlink(path)              - удалить файл
  vfs_getcwd()                  - получить текущую директорию
  vfs_cd(path)                  - сменить текущую директорию
  vfs_mount(mp, fs, ...)        - монтировать файловую систему
  vfs_umount(mp)                - размонтировать файловую систему


7. ДРАЙВЕРЫ УСТРОЙСТВ

7.1 ATA/IDE PIO

Драйвер реализует работу с ATA дисками в режиме PIO.
Поддерживается до 4 дисков.

7.2 PCI

Инициализация PCI-шины и перечисление устройств.

7.3 Клавиатура (PS/2)

Поддерживаются два драйвера:
  kkd_ps2_standard   - стандартная PS/2 клавиатура
  kkd_laptop_generic - ноутбучная клавиатура (generic)

7.4 Аудио

Драйвер PCS - воспроизведение звука через встроенный PC Speaker.

7.5 Фреймбуфер

Режим: 1024x768, 32 бит на пиксель (стандартный режим, но можно изменить в Multiboot заголовке).

7.6 Последовательный порт (Serial)

Используется для отладочного вывода. Функции: serial_print, read_serial, serial_received.


8. ПОЛЬЗОВАТЕЛИ И ДОСТУП

Система поддерживает двух предопределённых пользователей:
  user - обычный пользователь
  root - привилегированный пользователь

Пароль для входа в root зарание определен в metad.h это "12345678"

Переключение пользователя осуществляется командой su с флагом -s в QMSH.
Привилегированный режим обозначается меткой [R] в строке приглашения.
Обычный режим - [U].


9. КОМАНДНЫЙ ИНТЕРПРЕТАТОР QMSH

Версия QMSH: 3.4 

История команд: 32 записи (в пока полностю не реализована).
Максимальная длина команды: 256 символов.

Строка приглашения имеет формат:
  <имя_пользователя>[R|U]@QMS[<текущая_директория>]#

Список встроенных команд:
  help               - показать список команд
  whoami             - показать имя текущего пользователя
  ls                 - список файлов в директории
  cd <path>          - сменить директорию
  cp <path> <path>   - копировать файл или директорию
  mv <src> <dst>     - перемещение или преименование файла или директории
  pwd                - показать текущую директорию
  touch <file>       - создать пустой файл
  mkdir <dir>        - создать директорию
  rm <path>          - удалить файл или директорию
  cat <file>         - вывести содержимое файла
  run <file.qex>     - запустить QEX приложение (если команда была запущена с root привилегиями то и приложение будет запущено с root привилегией)
  su                 - переключиться в root (требует пароль)
  lsblk              - показать ATA-диски
  mount <disk> <mp>  - монтировать файловую систему
  umount <mp>        - размонтировать файловую систему
  dd if=<disk> of=<file> bs=512 count=N skip=N  - дамп диска
  format <disk> [fat16|fat32]  - форматировать раздел
  moudump            - показать список точек монтирования
  clear              - очистить экран
  mem                - статистика использования памяти
  sinfo              - системная информация
  about              - информация о QMS OS
  reboot             - перезагрузить систему
  shutdown           - выключить систему


10. ПРИЛОЖЕНИЯ И SBI SDK

QMS OS поддерживает запуск пользовательских QEX приложений.
Формат основан на ELF изза его удобства.

10.1 Формат заголовка SBI:
  magic          - магическое число 0x514245 (SBI_MAGIC)
  sdk_ver        - версия SDK (текущая: 4)
  is_root        - признак root-приложения (0 или 1)
  app_name[16]   - имя приложения
  entry_point    - адрес точки входа
  noroot_tab_off - смещение таблицы функций без root
  root_tab_off   - смещение таблицы root-функций

Изменение magic sdk_ver entry_point root_tab_off произведет до несовместимости со старыми приложениями.
Поддерживаемые версии SDK: 3, 4.

10.2 Коды ошибок загрузчика 
  SBIL_OK               =  0  - успех
  SBIL_ERR_BAD_MAGIC    = -1  - неверный magic
  SBIL_ERR_SDK_INCOMPAT = -2  - несовместимая версия SDK
  SBIL_ERR_NO_MEMORY    = -3  - нет памяти
  SBIL_ERR_BAD_PATH     = -4  - неверный путь
  SBIL_ERR_NO_SLOT      = -5  - нет слота
  SBIL_ERR_FS_READ      = -6  - ошибка чтения ФС
  SBIL_ERR_ROOT_DENIED  = -7  - требуются права root
  SBIL_ERR_FILE_TOOBIG  = -8  - файл слишком большой
  SBIL_ERR_BAD_ENTRY    = -9  - неверная точка входа

10.3 Доступные функции SDK (no root - обычные приложения)
  get_arg, clear_arg, set_arg     - работа с аргументами
  log(level, fmt, ...)            - вывод в лог
  printg(fmt, ...)                - вывод текста
  printgc(t, b, fmt, ...)         - вывод текста с цветом (fg, bg)
  clear_screen()                  - очистить экран
  goto_xy(x, y)                   - переместить курсор
  tscreen_h(), tscreen_w()        - размеры экрана
  tcursor_x(), tcursor_y()        - позиция курсора
  serial_print, read_serial, serial_received - работа с COM-портом
  get_char()                      - считать символ с клавиатуры
  kbd_get_modifiers()             - модификаторы клавиатуры
  get_special_key()               - специальная клавиша
  math_add, math_sub, math_mul, math_div, math_compare - арифметика
  memset, memcpy, memcmp          - работа с памятью
  strcmp, strncmp, strlen, strcpy, strncpy, strcat, strchr, strstr, strtok
  malloc(size), free(ptr)         - пользовательская память
  get_cpu_vendor, get_cpu_model   - информация о процессоре
  get_core_name, get_core_ver, get_core_tag, get_core_arch - получение метаданных ядра
  fload_set, fload_get_count, fload_get_name_index - загрузка шрифтов
  vfs_open, vfs_close, vfs_read, vfs_write, vfs_seek
  vfs_stat_path, vfs_readdir, vfs_mkdir, vfs_rmdir, vfs_unlink
  vfs_getcwd, vfs_cd              - файловая система
  show_gbanner, show_banner       - отображение баннера
  sbil_run(path)                  - запустить другое QEX приложение
  sbil_check(path)                - проверить QEX без запуска

10.4 Дополнительные функции SDK (доступны только c root)
  inb, outb, inw, outw            - прямой доступ к портам ввода вывода
  k_allocate, k_free, k_realloc   - куча ядра
  vfs_register_driver             - регистрация драйвера ФС
  vfs_mount, vfs_umount, vfs_force_init - монтирование
  fload_set_data                  - загрузка данных шрифта
  fb_putpixel, fb_clear, fb_draw_rect, fb_draw_char, fb_draw_image - пряое рисование на экране через фреймбуффер
  reboot(), shutdown()            - управление питанием

10.5 Сборка приложения
Для сборки используется шаблон qapp.
Структура приложения:
  src/main.c           - основной код
  src/clib/qlibc/      - заголовки SDK (sbi.h, io.h, vfs.h и тд.)

Минимальный пример:
  #include <qlibc/sbi.h>
  const char* appname = "myapp";
  int main(void) {
      os.printg("Hello World");
      return 0;
  }

Сборка:
  make APP_NAME=myapp

Результат: bin/myapp.qex


11. ШРИФТЫ И КОДИРОВКА

Система поддерживает растровые шрифты в формате PSF подобных структур.
Встроенные шрифты: cp866, ascii.


12. ЛИЦЕНЗИЯ

QMS OS распространяется под лицензией BSD 2-Clause.

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


14. УЧАСНИКИ ПРОЕКТА

Разработчик:           Roit
Помощь по разработке:  C0000374
