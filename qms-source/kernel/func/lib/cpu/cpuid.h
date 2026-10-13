#ifndef ARCH_CPUID_H
#define ARCH_CPUID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} cpuid_result_t;

typedef struct {
    int sse3;
    int rdrand;
    int apic;
    int fpu;
    int msr;
} cpu_features_t;

int  cpuid_is_supported(void);
void cpu_id(uint32_t leaf, uint32_t subleaf, cpuid_result_t* result);
void get_cpu_vendor(char* vendor);
void get_cpu_model(char* model);
void get_cpu_features(cpu_features_t* features);

#ifdef __cplusplus
}
#endif

#endif
