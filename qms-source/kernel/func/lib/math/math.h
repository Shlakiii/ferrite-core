#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

char* add(const char* a, const char* b);
char* sub(const char* a, const char* b);
char* mul(const char* a, const char* b);
char* div(const char* a, const char* b);

int compare(const char* a, const char* b);

#ifdef __cplusplus
}
#endif

#endif
