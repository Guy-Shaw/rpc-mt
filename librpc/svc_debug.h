/*
 * File: svc_debug.h
 * 
 */

#ifndef _SVC_DEBUG_H
#define _SVC_DEBUG_H 1

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>            // Needed for tprintf(), teprintf()
#include <assert.h>
#include <execinfo.h>

#include "decode.h"

extern pthread_mutex_t trace_lock;

#define eprintf_with_lock(fmt, ...) \
    ({ \
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

#define trace_printf_with_lock(fmt, ...) \
    ({ \
        fflush(stderr); \
        fprintf(stderr, "\n@%#lx:%s:%u:%s: " fmt, \
            (uintptr_t)pthread_self(), __FILE__, __LINE__, __FUNCTION__, \
            ## __VA_ARGS__); \
        fflush(stderr); \
    })

#define trace_printf(fmt, ...) \
    ({ \
        pthread_mutex_lock(&trace_lock); \
        trace_printf_with_lock(fmt, ## __VA_ARGS__); \
        pthread_mutex_unlock(&trace_lock); \
    })

#define teprintf_with_lock(fmt, ...) \
    ({ \
        fflush(stderr); \
        fprintf(stderr, "\n@%#lx:%s:%u:%s: ***ERROR***\n    " fmt, \
            (uintptr_t)pthread_self(), __FILE__, __LINE__, __FUNCTION__, \
            ## __VA_ARGS__); \
        fflush(stderr); \
    })

#define teprintf(fmt, ...) \
    ({ \
        pthread_mutex_lock(&trace_lock); \
        teprintf_with_lock(fmt, ## __VA_ARGS__); \
        pthread_mutex_unlock(&trace_lock); \
    })

#define tprintf(fmt, ...) \
    ({ if (opt_svc_trace) { trace_printf(fmt, ## __VA_ARGS__); } })

#define BAD_SVCXPRT_PTR ((void *)(-1))

extern void show_xports(void);
extern void svc_trace(void);
extern void svc_die(void);
extern void *guard_malloc(size_t size);
extern void *guard_realloc(void *old, size_t size);
extern void svc_perror(int err, const char *s);

extern int opt_svc_trace;

char *decode_addr_r(char *buf, size_t bufsz, void *addr);
char *decode_poll_events_r(char *buf, size_t bufsz, int events);
char * decode_esym_r(char *buf, size_t bufsz, int err);


#define decode_addr(addr) \
    ({ \
        void *_tmp_addr = (addr); \
        void *_tmp_buf = alloca(32); \
        decode_addr_r((char *)_tmp_buf, 32, _tmp_addr); \
    })

static inline int
ssize_to_int(ssize_t size)
{
    int isize;
    ssize_t vsize;

    isize = size;
    vsize = isize;
    if (vsize != size) {
        tprintf("Data loss converting from ssize to int.\n");
        abort();
    }
    return (isize);
}

#endif /* _SVC_DEBUG_H */
