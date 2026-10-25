#ifndef _METAD_H
#define _METAD_H

#ifdef __cplusplus
extern "C" {
#endif

const char* get_core_name();
const char* get_core_ver();
const char* get_core_tag();
const char* get_core_arch();

struct User {
    const char* name;
    int access;
};

extern User users_list[];
extern int users_count;

#define OS_NAME            "QMS OS"
#define CORE_VER           "1.0.0"
#define BUILD_TAG          "Release"
#define AUTHOR             "Roit"
#define CARCH              "x86"
#define LICENSE            "BSD-2"
#define SH_NAME            "QMSH"
#define SH_VER             "3.5"
#define PASSWORD           "12345678"

#define FERR_NAME          "Ferrite Core"
#define FERR_VER           "1.1.2"
#define FERR_B_TA          "Beta"
#define FERR_BUILD_TAG     "Security VFS"
#define FERR_AUTHOR        "Shlakiii"
#define FERR_LICENSE       "Apache 2.0"
#define FERR_SH_NAME       "FERR-C"
#define FERR_SH_VER        "0.2"
#define FERR_BUILD_DATE    "Oct 25, 2026, 17:39"

#define FUSION_NAME        "FERRITE-QMS"
#define SH_FUSION_NAME     "FERR-QMS"

#ifdef __cplusplus
}
#endif

#endif
