/*
 * File: decode.h
 * 
 */

#ifndef _DECODE_H
#define _DECODE_H 1

#include <stdio.h>      // Import sprintf()
#include <stdlib.h>     // Import size_t
#include <stdint.h>     // Import uint32_t, uint64_t
#include <assert.h>

typedef unsigned int uint_t;

extern char *dbuf_slot_alloc(size_t slot);

#define DBUF_SIZE 100

/*
 * Helper functions.
 *
 * These functions are very common, because many decode*() functions
 * append strings to a fixed size buffer, and need to do it safely.
 *
 * The smaller functions are defined here, inline.  The larger
 * ones are defined in @file{util.c}.
 */

/*
 * Scan the given zstring, @var{p} to the end (null byte).
 *
 * It is assumed (not checked) that @var{p} has a null byte
 * within the bounds of the object the @var{p} points within.
 */

static inline char *
strend(char *p)
{
    while (*p) {
        ++p;
    }
    return (p);
}

/*
 * Determine if @var{pos} is contained with the buffer starting
 * at @var{buf} and of length @var{bufsz}.
 */

static inline int
in_buf(char *buf, size_t bufsz, char *pos)
{
    return (pos >= buf && pos < buf + bufsz);
}

/*
 * Append the string, @var{newstr}, to the end of the buffer starting
 * at @var{buf} and of length @var{bufsz}.  Rather than scan @var{buf}
 * for the end of string (null byte), we maintain @var{ebuf}, as we go.
 * It is an error (assertion failure) if @var{ebuf} is not contained
 * in @var{buf}.
 */

extern char *
append_buf(char *buf, size_t bufsz, char *ebuf, char const *newstr);

/*
 * Given: a buffer starting at @var{buf} of size @var{bufsz},
 * and a pointer within the buffer, @var{p}.
 *
 * Scan the buffer to the end (null byte) or until the limit, @var{bufsz},
 * whichever comes first.  
 *
 * Like @function{strend}, but buffer size is enforced.
 */
extern char *buf_end(char *buf, size_t bufsz, char *p);


extern char *decode_poll_events_r(char *buf, size_t bufsz, int events);
extern char *decode_poll_events(int events);
extern char *decode_inet_family_r(char *buf, size_t bufsz, int family);
extern char *decode_inet_family(int family);
extern char *decode_inet_endpoint_r(char *buf, size_t bufsz, void *inet_addr);
extern char *decode_inet_endpoint(void *inet_addr);
extern char *decode_inet_peer_r(char *buf, size_t bufsz, int socket);
extern char *decode_inet_peer(int socket);
extern char *decode_int_r(char *buf, size_t bufsz, int i);
extern char *decode_int(int i);
extern int   popcount_int(int);

#endif /* _DECODE_H */
