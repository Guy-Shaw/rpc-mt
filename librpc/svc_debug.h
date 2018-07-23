/*
 * Filename: svc_debug.h
 * Project: rpc-mt
 * Brief: Debug/trace helper functions specific to svc
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

#ifndef _SVC_DEBUG_H
#define _SVC_DEBUG_H 1

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>            // Needed for tprintf(), teprintf()
#include <assert.h>
#include <execinfo.h>

#include <decode-impl.h>

extern pthread_mutex_t trace_lock;

#define eprintf_with_lock(fmt, ...) \
    ({ \
        dbuf_thread_reset(); \
        fflush(stderr); \
        fprintf(stderr, fmt, ## __VA_ARGS__); \
        fflush(stderr); \
    })

#define eprintf(fmt, ...) \
    ({ \
        pthread_mutex_lock(&trace_lock); \
        eprintf_with_lock(fmt, ## __VA_ARGS__); \
        pthread_mutex_unlock(&trace_lock); \
    })

#ifdef __x86_64__
#define trace_printf_with_lock(fmt, ...) \
    ({ \
        dbuf_thread_reset(); \
        fflush(stderr); \
        fprintf(stderr, "\n@%#lx:%s:%u:%s: " fmt, \
            (uintptr_t)pthread_self(), __FILE__, __LINE__, __FUNCTION__, \
            ## __VA_ARGS__); \
        fflush(stderr); \
    })
#else
#define trace_printf_with_lock(fmt, ...) \
    ({ \
        dbuf_thread_reset(); \
        fflush(stderr); \
        fprintf(stderr, "\n@%#x:%s:%u:%s: " fmt, \
            (uintptr_t)pthread_self(), __FILE__, __LINE__, __FUNCTION__, \
            ## __VA_ARGS__); \
        fflush(stderr); \
    })
#endif

#define trace_printf(fmt, ...) \
    ({ \
        pthread_mutex_lock(&trace_lock); \
        trace_printf_with_lock(fmt, ## __VA_ARGS__); \
        pthread_mutex_unlock(&trace_lock); \
    })

#ifdef __x86_64__
#define teprintf_with_lock(fmt, ...) \
    ({ \
        dbuf_thread_reset(); \
        fflush(stderr); \
        fprintf(stderr, "\n@%#lx:%s:%u:%s: ***ERROR***\n    " fmt, \
            (uintptr_t)pthread_self(), __FILE__, __LINE__, __FUNCTION__, \
            ## __VA_ARGS__); \
        fflush(stderr); \
    })
#else
#define teprintf_with_lock(fmt, ...) \
    ({ \
        dbuf_thread_reset(); \
        fflush(stderr); \
        fprintf(stderr, "\n@%#x:%s:%u:%s: ***ERROR***\n    " fmt, \
            (uintptr_t)pthread_self(), __FILE__, __LINE__, __FUNCTION__, \
            ## __VA_ARGS__); \
        fflush(stderr); \
    })
#endif

#define teprintf(fmt, ...) \
    ({ \
        pthread_mutex_lock(&trace_lock); \
        teprintf_with_lock(fmt, ## __VA_ARGS__); \
        pthread_mutex_unlock(&trace_lock); \
    })

#define tprintf(lvl, fmt, ...) \
    ({ if (opt_svc_trace >= lvl) { trace_printf(fmt, ## __VA_ARGS__); } })

#define BAD_SVCXPRT_PTR ((SVCXPRT *)(-1))

extern void show_xports(void);
extern void svc_trace(unsigned int lvl);
extern void svc_die(void);
extern void svc_perror(int err, const char *s);

extern unsigned int opt_svc_trace;

extern int ssize_to_int(ssize_t ssz);

#ifdef  __cplusplus
}
#endif

#endif /* _SVC_DEBUG_H */
