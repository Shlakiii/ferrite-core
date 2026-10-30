#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void*    memset(void* ptr, int value, size_t num);
void*    memcpy(void* destination, const void* source, size_t num);
void*    memmove(void* destination, const void* source, size_t num);
int      memcmp(const void* ptr1, const void* ptr2, size_t num);
void*    memchr(const void* ptr, int value, size_t num);
int      strcmp(const char* str1, const char* str2);
char*    strcpy(char* destination, const char* source);
int      strncmp(const char* str1, const char* str2, size_t num);
char*    strncpy(char* destination, const char* source, size_t num);
size_t   strlen(const char* str);
char*    strchr(const char* str, int character);
char*    strtok(char* str, const char* delimiters);
char*    strstr(char* str, const char* target);
char*    strcat(char* dest, const char* src);
char*    strncat(char* dest, const char* src, size_t n);
char*    strrchr(const char* str, int ch);
uint16_t intu16(int value);
int      u16int(uint16_t value);
int      toupper(int c);
int      tolower(int c);

#ifdef __cplusplus
}
#endif

#endif
