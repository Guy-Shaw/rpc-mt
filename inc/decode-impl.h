/*
 * Filename: decode-impl.h
 * Project: libcscript
 * Brief: Interface common to implementation of decode_*() family of functions
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

#ifndef _DECODE_IMPL_H
#define _DECODE_IMPL_H 1

#ifdef  __cplusplus
extern "C" {
#endif

#include <decode.h>

#include <stdint.h>     // Import uint32_t, uint64_t

typedef unsigned int uint_t;


/*
 * Allocator functions.
 */
extern void *guard_malloc(size_t size);
extern void *guard_calloc(size_t nelem, size_t size);
extern void *guard_realloc(void *old_mem, size_t size);

/*
 * Specialized allocator functions for per-thread "pseudo-static"
 * decode buffers.  Buffers are allocated on the heap, in one pool
 * per thread.  They persist until they are all destroyed explicitly
 * by calling dbuf_thread_reset(), which is called by teprintf().
 * That way, non-reentrant decode_*() functions return buffer addresses
 * that are safe to use while arguments to teprintf() are being evaluated.
 * The buffer addresses are safe, even if several decode_*() functions
 * are called as part of evaluating arguments, and even if the same
 * decode_*() function is called more than once.  After all argument
 * to teprintf() are evaluated and then teprintf() itself is called,
 * then all decode buffers for the current thread can be freed, at once.
 * Or, the freeing of all decode buffers can be done just before a call
 * to teprintf(); that way, if something goes wrong, then the debugger
 * has access to all the decode buffers.
 *
 * Anything in this description that applies to the function, teprintf()
 * can also apply to any similar function that needs to use decode_*()
 * functions, especially any that could call decode_*() functions an arbitrary
 * number of times while evaluating arguments to some other tracing function.
 *
 */

extern void  dbuf_thread_reset(void);
extern char *dbuf_thread_alloc(size_t sz);

extern char *dbuf_slot_alloc(size_t slot);


/*
 * Helper functions.
 *
 * These functions are very common, because many decode*() functions
 * append strings to a fixed size buffer, and need to do it safely.
 *
 * The smaller functions are defined here, inline.
 * The larger functions are not inlined,
 * and are defined in separate C source files.
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
 * Section: Helper functions that deal with bounded string buffers.
 */

/*
 * Determine if @var{pos} is contained within the buffer starting
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
append_buf(char *buf, size_t bufsz, char *ebuf, const char *newstr);

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


/*
 * Miscellaneous helper functions, used by top-level decode_*() functions.
 */

extern char *sisfx_scaled_r(char *result, uint_t n, uint_t mag);
extern char *sisfx32_r(char *res, uint_t n);
extern char *sisfx64_r(char *result, uint64_t n64);

extern int   popcount_int(int);

extern uint_t int_to_uint(int i);

#ifdef  __cplusplus
}
#endif

#endif /* _DECODE_IMPL_H */
