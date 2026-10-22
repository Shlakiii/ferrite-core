#include "string.h"

#include <stdint.h>
#include <stdbool.h>

char* strcpy(char* destination, const char* source) {
    char* original_dest = destination;
    while ((*destination++ = *source++));
    return original_dest;
}

char* strncpy(char* destination, const char* source, size_t num) {
    size_t i;
    for (i = 0; i < num && source[i] != '\0'; i++) {
        destination[i] = source[i];
    }
    for (; i < num; i++) {
        destination[i] = '\0';
    }

    return destination;
}

int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }

    return *(const unsigned char*)str1 - *(const unsigned char*)str2;
}

int strncmp(const char* str1, const char* str2, size_t num) {
    if (num == 0) {
        return 0;
    }
    while (num-- != 0) {
        if (*str1 != *str2) {
            return (unsigned char)*str1 - (unsigned char)*str2;
        }
        if (*str1 == '\0') {
            break;
        }
        str1++;
        str2++;
    }

    return 0;
}

size_t strlen(const char* str) {
    size_t length = 0;
    while (str[length] != '\0') {
        length++;
    }

    return length;
}

void* memset(void* ptr, int value, size_t num) {
    unsigned char* p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; ++i) {
        p[i] = (unsigned char)value;
    }

    return ptr;
}

void* memcpy(void* destination, const void* source, size_t num) {
    char* dest = (char*)destination;
    const char* src = (const char*)source;
    for (size_t i = 0; i < num; ++i) {
        dest[i] = src[i];
    }

    return destination;
}

void* memmove(void* destination, const void* source, size_t num) {
    unsigned char* dest = (unsigned char*)destination;
    const unsigned char* src = (const unsigned char*)source;

    if ((uintptr_t)dest < (uintptr_t)src) {
        for (size_t i = 0; i < num; ++i)
            dest[i] = src[i];
    } else if ((uintptr_t)dest > (uintptr_t)src) {
        while (num > 0) {
            --num;
            dest[num] = src[num];
        }
    }

    return destination;
}

int memcmp(const void* ptr1, const void* ptr2, size_t num) {
    const unsigned char* p1 = (const unsigned char*)ptr1;
    const unsigned char* p2 = (const unsigned char*)ptr2;

    for (size_t i = 0; i < num; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }

    return 0;
}

char* strchr(const char* str, int character) {
    char c = (char)character;
    while (*str) {
        if (*str == c) {
            return (char*)str;
        }
        str++;
    }
    if (c == '\0') {
        return (char*)str;
    }

    return (char*)0;
}

static char* strtok_last = NULL;

char* strtok(char* str, const char* delimiters) {
    if (str == NULL) {
        str = strtok_last;
    }
    if (str == NULL) {
        return NULL;
    }

    while (*str) {
        bool is_delim = false;
        for (const char* d = delimiters; *d; d++) {
            if (*str == *d) { is_delim = true; break; }
        }
        if (!is_delim) break;
        str++;
    }

    if (*str == '\0') {
        strtok_last = NULL;
        return NULL;
    }

    char* start = str;
    while (*str) {
        bool is_delim = false;
        for (const char* d = delimiters; *d; d++) {
            if (*str == *d) { is_delim = true; break; }
        }
        if (is_delim) {
            *str = '\0';
            strtok_last = str + 1;
            return start;
        }
        str++;
    }

    strtok_last = NULL;
    return start;
}

char* strstr(char* str, const char* target) {
    if (!*target) return str;
    for (; *str; str++) {
        if (*str != *target) continue;
        char *p1 = str, *p2 = (char*)target;
        while (*p1 && *p2 && *p1 == *p2) {
            p1++; p2++;
        }
        if (!*p2) return str;
    }

    return NULL;
}

char* strcat(char* dest, const char* src) {
    char* ptr = dest;

    while (*ptr != '\0') {
        ptr++;
    }

    while (*src != '\0') {
        *ptr++ = *src++;
    }

    *ptr = '\0';
    return dest;
}

uint16_t intu16(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 65535) {
        return 65535;
    }

    return (uint16_t)value;
}

int u16int(uint16_t value) {
    return (int)value;
}

char* strrchr(const char* str, int ch) {
    const char* last = NULL;
    while (*str) {
        if (*str == (char)ch)
            last = str;
        str++;
    }
    if (ch == '\0')
        return (char*)str;
    return (char*)last;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

char* strncat(char* dest, const char* src, size_t n) {
    size_t dest_len = strlen(dest);
    size_t i;
    for (i = 0 ; i < n && src[i] != '\0' ; i++)
        dest[dest_len + i] = src[i];
    dest[dest_len + i] = '\0';
    return dest;
}
