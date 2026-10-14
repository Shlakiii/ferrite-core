#include "serial.h"

#include <io/printg.h>
#include <io/io.h>

int init_serial() {
    log(3, "Serial: initialized...");
    outb(PORT + 1, 0x00);
    outb(PORT + 3, 0x80);
    outb(PORT + 0, 0x03);
    outb(PORT + 1, 0x00);
    outb(PORT + 3, 0x03);
    outb(PORT + 2, 0xC7);
    outb(PORT + 4, 0x0B);
    outb(PORT + 4, 0x1E);
    outb(PORT + 0, 0xAE);

    if(inb(PORT + 0) != 0xAE) {
        return 1;
    }

    outb(PORT + 4, 0x0F);
    log(3, "Serial: init done.");
    
    return 0;
}

int serial_received() {
    return inb(PORT + 5) & 1;
}

char read_serial() {
    while (serial_received() == 0);
    return inb(PORT);
}

int is_transmit_empty() {
    return inb(PORT + 5) & 0x20;
}

void write_serial(char a) {
    while (is_transmit_empty() == 0);
    outb(PORT, a);
}

void serial_print(const char* str) {
    while (*str) {
        write_serial(*str++); // вывод симвала
    }
}

void serial_println(const char* str) {
    while (*str) {
        write_serial(*str++); // вывод симвала
    }
    write_serial('\r'); // возврат каретки
    write_serial('\n'); // новая строка
}

void serial_return_cur(int steps) {
    for (int i = 0; i < steps; i++) {
        write_serial('\b'); // возврат каретки
    }
}

void serial_push_cur(int steps) {
    for (int i = 0; i < steps; i++) {
        serial_print("\033[1C"); // движение курсора вправо на 1 позицию
    }
}

void serial_full_return_cur(){
    write_serial('\r'); // возврат каретки
}
void serial_new_line_manual(){
    write_serial('\r'); // возврат каретки
    write_serial('\n'); // новая строка
}