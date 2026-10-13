#include "metad.h"

User users_list[] = {
    {"user",  0},
    {"root",  1}
};
int users_count = 2;

const char* get_core_name() {
    return OS_NAME;
}

const char* get_core_ver() {
    return CORE_VER;
}

const char* get_core_tag() {
    return BUILD_TAG;
}

const char* get_core_arch() {
	return CARCH;
}
