#ifndef _BIN_SHELL_H
#define _BIN_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

extern bool shrboot;
extern bool shsdown;

extern void cmd_mount(char* args, bool is_elevated);

void run_shell();

#ifdef __cplusplus
}
#endif

#endif
