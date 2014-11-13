
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>             // Import sleep()
#include <assert.h>

#include "svc_debug.h"

int opt_svc_trace = 0;

void
svc_trace(void)
{
    opt_svc_trace = 1;
}

/*
 *  Macros for printing `size_t'
 *
 * Things that should have been defined in inttypes.h, but weren't
 *
 */
#define PRIdSIZE PRIdPTR
#define PRIuSIZE PRIuPTR
#define PRIxSIZE PRIxPTR
#define PRIXSIZE PRIXPTR

char *
str_size_r(char *buf, size_t size)
{
    sprintf(buf, "%" PRIuSIZE, size);
    return (buf);
}

void
svc_die()
{
    fflush(stdout);
    fflush(stderr);
    sleep(1);
    fflush(stdout);
    fflush(stderr);
    tprintf("\n");
    abort();
}

void *
guard_malloc(size_t size)
{
    void *mem;

    mem = malloc(size);
    if (mem == (void *)0) {
        teprintf("malloc(%u) failed.\n", size);
        svc_die();
    }
    return (mem);
}

void *
guard_realloc(void *old_mem, size_t size)
{
    void *new_mem;

    new_mem = realloc(old_mem, size);
    if (new_mem == (void *)0) {
        teprintf("realloc(size=%u) failed.\n", size);
        svc_die();
    }
    return (new_mem);
}
