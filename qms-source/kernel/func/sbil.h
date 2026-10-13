#ifndef SBIL_H
#define SBIL_H

#include <stdint.h>
#include "sbik.h"

#ifdef __cplusplus
extern "C" {
#endif

static char current_app_arg[256] = {0};

SbilError sbil_exec(const char* path, int system_root, SbilAppDesc* out_desc);
SbilError sbil_run(const char* path);
SbilError sbil_check(const char* path);

const char* sbil_strerror(SbilError err);
int sbil_sdk_supported(uint16_t sdk_ver);

const char* sbil_get_arg(void);
void        sbil_clear_arg(void);
void        sbil_set_arg(const char* arg);

#ifdef __cplusplus
}
#endif

#endif
