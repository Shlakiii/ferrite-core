#ifndef LUAHEAP_FUNC_BRIDGE_H
#define LUAHEAP_FUNC_BRIDGE_H

// Подключаем системный stdio.h, чтобы взять оттуда тип FILE и size_t
#include <stdio.h> 

#ifdef __cplusplus
extern "C" {
#endif

// Объявляем функции ТОЧНО ТАК ЖЕ, как в stdio.h (с типом FILE*)
int putchar(int c);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);

// Переменные потоков
extern FILE *stdout;
extern FILE *stderr;

#ifdef __cplusplus
}
#endif

#endif // LUAHEAP_FUNC_BRIDGE_H