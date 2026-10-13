#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

void init_idt();
extern "C" void default_exception_handler();

#endif
