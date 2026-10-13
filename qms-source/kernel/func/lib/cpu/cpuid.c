#include "cpuid.h"

void cpu_id(uint32_t leaf, uint32_t subleaf, cpuid_result_t* result) {
    asm volatile(
        "cpuid"
        : "=a"(result->eax), "=b"(result->ebx), "=c"(result->ecx), "=d"(result->edx)
        : "a"(leaf), "c"(subleaf)
    );
}

int cpuid_is_supported(void) {
    uint32_t supported;
    asm volatile(
        "pushfl\n\t"
        "pushfl\n\t"
        "xorl $0x00200000, (%%esp)\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %0\n\t"
        "xorl (%%esp), %0\n\t"
        "popfl\n\t"
        "andl $0x00200000, %0\n\t"
        : "=r"(supported)
    );
    return supported != 0;
}

void get_cpu_vendor(char* vendor) {
    cpuid_result_t res;
    cpu_id(0, 0, &res);
    *(uint32_t*)(vendor + 0) = res.ebx;
    *(uint32_t*)(vendor + 4) = res.edx;
    *(uint32_t*)(vendor + 8) = res.ecx;
    vendor[12] = '\0';
}

void get_cpu_model(char* model) {
    cpuid_result_t res;
    cpu_id(0x80000000, 0, &res);
    
    if (res.eax < 0x80000004) {
        const char* unknown = "Unknown CPU Model";
        int i = 0;
        while(unknown[i]) { model[i] = unknown[i]; i++; }
        model[i] = '\0';
        return;
    }

    uint32_t* m = (uint32_t*)model;
    for (uint32_t i = 0; i < 3; i++) {
        cpu_id(0x80000002 + i, 0, &res);
        m[i*4 + 0] = res.eax;
        m[i*4 + 1] = res.ebx;
        m[i*4 + 2] = res.ecx;
        m[i*4 + 3] = res.edx;
    }
    model[48] = '\0';
}

void get_cpu_features(cpu_features_t* f) {
    cpuid_result_t res;
    cpu_id(1, 0, &res);
    f->fpu    = (res.edx & (1 << 0));
    f->msr    = (res.edx & (1 << 5));
    f->apic   = (res.edx & (1 << 9));
    f->sse3   = (res.ecx & (1 << 0));
    f->rdrand = (res.ecx & (1 << 30));
}
