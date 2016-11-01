/*
 * Filename: src/rdtsc.h
 * Project: test-rdtsc
 * Brief: Interface and inline implementation of rdtsc()
 *
 * Copyright (C) 2016 Guy Shaw
 * Written by Guy Shaw <gshaw@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>

static __inline__ uint64_t
rdtsc(void) {
    uint32_t lo, hi;

    __asm__ __volatile__ (      // serialize
    "xorl %%eax,%%eax \n        cpuid"
    ::: "%rax", "%rbx", "%rcx", "%rdx");
    /*
     * We cannot use "=A", since this would use %rax on x86_64
     * and return only the lower 32bits of the TSC
     */
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (((uint64_t)hi << 32) | (uint64_t)lo);
}
