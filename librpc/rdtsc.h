#ifndef _RDTSC_H

#define _RDTSC_H

#include <stdint.h>

__inline__ uint64_t
rdtsc() {
    uint32_t lo, hi;

    __asm__ __volatile__ (      // serialize
    "xorl %%eax,%%eax \n        cpuid"
    ::: "%rax", "%rbx", "%rcx", "%rdx");
    /*
     * We cannot use "=A", since this would use %rax on x86_64
     * and return only the lower 32bits of the TSC
     */
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32 | lo);
}

#endif /* _RDTSC_H */