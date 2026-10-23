#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void printg(const char *fmt, ...);
// Если ты используешь свой stdio.h, подключи его.

// 1. Создаем фиктивные объекты FILE и указатели на них.
// Значение (FILE*)1 просто чтобы они не были NULL.
static int dummy_stdout;
static int dummy_stderr;
FILE *stdout = (FILE*)&dummy_stdout;
FILE *stderr = (FILE*)&dummy_stderr;

// 2. Реализация putchar
int putchar(int c) {
    // Вызываем твою функцию ядра для вывода символа.
    // Если printg умеет в %c:
    printg("%c", (char)c);
    
    // Или, если есть прямой вывод в терминал:
    // terminal_putc((char)c);
    
    return c; 
}

// 3. Реализация fputc (используется внутри lib_base.c)
int fputc(int c, FILE *stream) {
    (void)stream; // Игнорируем поток
    return putchar(c);
}

// 4. Реализация fputs
int fputs(const char *s, FILE *stream) {
    (void)stream;
    if (!s) return -1;
    while (*s) {
        putchar(*s++);
    }
    return 0;
}

// 5. Реализация fwrite
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    (void)stream;
    if (!ptr || size == 0 || nmemb == 0) return 0;
    
    size_t total = size * nmemb;
    const char *str = (const char *)ptr;
    for (size_t i = 0; i < total; i++) {
        putchar(str[i]);
    }
    return nmemb;
}

// 6. Заглушка для fflush
int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

static int lua_errno;
static int dummy_stdin;
FILE *stdin = (FILE*)&dummy_stdin;

int *__errno_location(void) {
    return &lua_errno;
}

char *strerror(int error) {
    (void)error;
    return "file I/O unavailable";
}

FILE *fopen(const char *filename, const char *mode) {
    (void)filename;
    (void)mode;
    lua_errno = ENOSYS;
    return NULL;
}

int feof(FILE *stream) {
    (void)stream;
    return 1;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream) {
    (void)ptr;
    (void)size;
    (void)count;
    (void)stream;
    lua_errno = ENOSYS;
    return 0;
}

int ferror(FILE *stream) {
    (void)stream;
    lua_errno = ENOSYS;
    return 1;
}

int fclose(FILE *stream) {
    (void)stream;
    return 0;
}

unsigned long long strtoull(const char *text, char **end, int base) {
    const char *cursor = text;
    unsigned long long value = 0;
    int negative = 0;
    int overflow = 0;
    int digits = 0;

    while (*cursor == ' ' || (*cursor >= '\t' && *cursor <= '\r'))
        cursor++;
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        cursor++;
    }
    if ((base == 0 || base == 16) && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16;
        cursor += 2;
    }
    if (base == 0)
        base = *cursor == '0' ? 8 : 10;
    if (base < 2 || base > 36) {
        lua_errno = EINVAL;
        if (end) *end = (char *)text;
        return 0;
    }

    while (*cursor) {
        unsigned int digit;
        if (*cursor >= '0' && *cursor <= '9')
            digit = (unsigned int)(*cursor - '0');
        else if (*cursor >= 'a' && *cursor <= 'z')
            digit = (unsigned int)(*cursor - 'a') + 10;
        else if (*cursor >= 'A' && *cursor <= 'Z')
            digit = (unsigned int)(*cursor - 'A') + 10;
        else
            break;
        if (digit >= (unsigned int)base)
            break;
        digits++;
        if (value > (ULLONG_MAX - digit) / (unsigned int)base)
            overflow = 1;
        else if (!overflow)
            value = value * (unsigned int)base + digit;
        cursor++;
    }

    if (end)
        *end = (char *)(digits ? cursor : text);
    if (!digits)
        return 0;
    if (overflow) {
        lua_errno = ERANGE;
        return ULLONG_MAX;
    }
    return negative ? 0ULL - value : value;
}