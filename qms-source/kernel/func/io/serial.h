#ifndef SERIAL_H
#define SERIAL_H

#define PORT 0x3f8

char read_serial();
int init_serial();
int serial_received();
int is_transmit_empty();
void write_serial(char a);
void serial_print(const char* str);

#endif
