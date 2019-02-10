/*
 * svc.c, Server-side remote procedure call interface.
 *
 * There are two sets of procedures here.  The xprt routines are
 * for handling transport handles.  The svc routines handle the
 * list of service routines.
 *
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file was derived from libc6/eglibc-2.24/sunrpc/svc.c.
 * It was written by Guy Shaw in 2011, under contract to Themis Computer,
 * http://www.themis.com.  It inherits the copyright and license
 * from the source code from which it was derived.
 *
 */

#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#include <poll.h>
#include <pthread.h>

#include "svc_config.h"
#include "svc_mtxprt.h"
#include "svc_debug.h"
#include "pthread_util.h"
#include "bitvec.h"
#include "int_limits.h"

static inline void
incr_counter(size_t *countp)
{
    __sync_fetch_and_add(countp, 1);
}

static inline void
decr_counter(size_t *countp)
{
    __sync_fetch_and_add(countp, -1);
}

// XXX Move this to the appropriate .h file
//
extern char *decode_xprt_stat(enum xprt_stat xrv);

extern void show_rate_limit_stats(void);

extern void *svc_l1_alloc(size_t sz);

/*
 * Extended scope declarations.
 *
 * The C programming language does not distinguish between:
 *   1) functions or data that is external because it is part of the
 *      ABI and is meant to be visible to the outside world, and
 *
 *   2) functions or data that need to be declared extern (not static)
 *      only because it needs to be visible to other modules
 *      (C source files) within a library that implements the API,
 *      but ought not to be visible outside the library.
 *
 * We annotate extern function and/or data with PUBLIC or LIBRARY.
 * This does not enforce anything, but can be used by a linker script
 * to hide symbols (at least in a shared library).
 *
 * PUBLIC:
 *   Part of the API.  Visible to the outside world.
 *
 * LIBRARY:
 *   Not static.  Visible to other librpc files, but not part of the API.
 *   That is, not meant to be visible to the outside world.
 *
 * UNUSED:
 *   static functions that we do not want to remove, at least not yet.
 *   The function, ref_unused(), keeps the compiler quiet when all
 *   warnings are turned on.
 *   It also acts as a quick visual way to keep track of unused functions.
 *
 */

#define PUBLIC
#define LIBRARY
#define UNUSED static


/*
 * SFR := Server Flight Recorder
 *
 * Conditionally compile code to instrument all transactions related
 * to sockets.  Each time a new socket is associated with a @type{SVCXPRT},
 * record:
 *   1) the high-resolution timestamp,
 *   2) the thread id,
 *   3) the processor id,
 */
#define SFR_SOCKET 1

extern pthread_mutex_t trace_lock;

/*
 * @var{xports} is an array of _ALL_ @type{SVCXPRT} structures,
 * both "master" and "clone" structures.  They are allocated,
 * as needed, and therefore there is no way to index into this
 * array using a file descriptor (socket).
 *
 * @var{sock_xports} is an array of only the "master" @type{SVCXPRT}s,
 * indexable directly by file descriptor (socket).
 */

static SVCXPRT **xports;
static SVCXPRT **xports_view;
static SVCXPRT **sock_xports;

#if defined(SFR_SOCKET)

#include <stdint.h>
#include "rdtsc.h"
#include "svc_tcp_impl.h"

typedef uint64_t hrtime_t;
typedef int processorid_t;

static hrtime_t t0 = 0;

struct sock_sfr {
    hrtime_t      sfr_timestamp;
    pthread_t     sfr_tid;
    processorid_t sfr_psr;
};

typedef struct sock_sfr sock_sfr_t;

static sock_sfr_t * sock_sfr;

#endif /* SFR_SOCKET */


/*
 * Forward declarations
 */
static void show_xports_hdr(size_t indent);
static void show_xport(SVCXPRT **xprtv, size_t id, size_t indent);

/*
 * We use a bit vector to keep track of set membership in the set
 * of all SVCXPRTs and the set of SVCXPRT IDs to be garbage collected.
 *
 * We use our own bitvec library, instead of fd_sets and FD_* family
 * of functions, because the fd_set operations are of limited value.
 *   1) They are limited to a fixed size, 1024;
 *   2) Indexing functions do not check their arguments;
 *   3) They are not lint clean, or even -Wall-clean.
 *
 * We also use the bitvec library to manage fd_sets, for example
 * for polling.
 *
 * But, just because we use bit vectors for both purposes, they
 * are not to be confused.  A set of SVCXPRTs is not the same
 * as a set of file descriptors.  There can be more SVCXPRTs
 * than the limit on the number of file descriptors, because,
 * for example, there can be several clone worker SVCXPRTs for
 * a single UDP connection.
 */

static bitvec_t xprtgc_idset;
static bitvec_t xports_idset;

static size_t xports_size;    // Capacity of xports array
static size_t xports_count;   // How many are currently allocated
static size_t xports_maxid;   // Max index of all allocated xports
static size_t xports_view_count;
static size_t xports_version;
static size_t xprtgc_mark_count;

struct pollfd *xports_pollfd;
nfds_t xports_pollfd_size;
nfds_t xports_max_pollfd;

pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * @var{xports_lock} protects all operations that modify
 * @var{xports} and/or @var{sock_xports}.
 *
 * Each @type{SVCXPRT} contains a lock to protect its contents,
 * but the "global" @var{xports_lock} is required to add or remove
 * @type{SVCXPRT} data structures (including cloned @type{SVCXPRT}s).
 */

pthread_mutex_t xports_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_t xports_owner;

pthread_mutex_t xports_view_lock = PTHREAD_MUTEX_INITIALIZER;
/*
 * @var{xprtgc_lock} does for the bitmap of @type{SVCXPRT}s to be
 * garbage collect, what @var{xports_lock} does for @type{SVCXPRT}s,
 * in general.  Any mark(), unmark(), or actual removal (and unmark)
 * operations must be protected by this one lock, because they can
 * modify the bitmap that accounts for the entire set of @type{SVCXPRT}s.
 */

pthread_mutex_t xprtgc_lock = PTHREAD_MUTEX_INITIALIZER;

// Control allocation of socket file descriptors for svc_tcp.
//
extern struct fd_region socket_fd_region;

/*
 * 0: single-threaded
 *        do not clone SVCXPRT;
 *        wait for @var{worker_return}
 * 1: multi-threaded, partial
 *        clone SVCXPRT for each request;
 *        single-threaded setup and teardown;
 *        but muti-threaded dispatch;
 *        after setup, do not wait
 * 2: fully multi-threaded
 *        clone SVCXPRT for each request;
 *        multi-threaded setup, dispatch, and teardown
 *
 * There is no interface for changing the mode.
 * It is for purposes of debugging and regression testing.
 */
extern int mtmode;
extern int wait_method_tcp;
extern int wait_method_udp;
extern int wait_trace_interval;
extern long jiffy;

/*
 * A production system should simply return status,
 * whether or not there is an error; the caller can
 * decide what to do in the case of error.  But,
 * while the system is being developed, it is useful
 * to die immediately upon error, so that the debugger
 * can examine the state of affairs right when an error
 * is detected, with a minimum of destruction of evidence.
 *
 * failfast == 0 -- production mode; just return status.
 * failfast == 1 -- debug mode; die immediately on error.
 */

extern int failfast;

/*
 * Counters
 * --------
 * cnt_request_recv:
 *     Total number of requests received.
 *
 * cnt_request_disp:
 *     Total number of requests received and dispatched.
 *
 * cnt_getargs:
 *     Total number of calls to svc_getargs().
 *
 * cnt_reply:
 *     Total number of calls to svc_reply().
 *
 * cnt_freeargs:
 *     Total number of calls to svc_freeargs().
 *
 * cnt_return:
 *     Total number of calls to svc_return().
 *
 */

size_t cnt_request_recv;
size_t cnt_request_disp;
size_t cnt_getargs;
size_t cnt_reply;
size_t cnt_freeargs;
size_t cnt_return;

int worker_return;


// Low-level functions to validate xport IDs, xport status, etc.

static inline bool
id_is_valid(size_t id)
{
    return (id <= xports_maxid);
}

static inline bool
xprt_stat_is_valid(enum xprt_stat xrv)
{
    return (xrv == XPRT_DIED || xrv == XPRT_MOREREQS || xrv == XPRT_IDLE);
}

static inline bool
xprt_is_clone(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;

    mtxprt = xprt_to_mtxprt(xprt);
    return (mtxprt->mtxp_parent != 0);
}

static inline bool
xprt_is_reusable(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;

    mtxprt = xprt_to_mtxprt(xprt);
    if (xprt->xp_port != 0) {
        return (false);
    }
    if (!(mtxprt->mtxp_parent == NO_PARENT)) {
        return (false);
    }
    if (mtxprt->mtxp_stat == XPRT_DIED) {
        return (false);
    }
    return (true);
}

static bool
fd_is_open(int fd)
{
    return (fcntl(fd, F_GETFD) != -1 || errno != EBADF);
}

static char *
decode_xprt_progress(SVCXPRT *xprt, size_t id)
{
    // XXX Allocate dbuf, instead of static char buf.
    static char buf[16];
    mtxprt_t *mtxprt;
    int xst;
    bool xprt_gc;

    if (xprt == BAD_SVCXPRT_PTR) {
        return ("_<BAD>__");
    }
    if (xprt == NULL) {
        return ("_<NULL>_");
    }

    mtxprt = xprt_to_mtxprt(xprt);
    xst = mtxprt->mtxp_progress;
    xprt_gc = bitvec_get_bit(&xprtgc_idset, id);
    snprintf(buf, sizeof (buf),
            "%c%c%c%c%c%c%c%c%c",
            (xst & XPRT_DISPATCH)   ? 'D' : 'd',
            (xst & XPRT_WAIT)       ? 'W' : 'w',
            (xst & XPRT_DONE_RECV)  ? 'R' : 'r',
            (xst & XPRT_DONE_READ)  ? 'R' : 'r',
            (xst & XPRT_GETARGS)    ? 'A' : 'a',
            (xst & XPRT_REPLY)      ? 'R' : 'r',
            (xst & XPRT_FREEARGS)   ? 'F' : 'f',
            (xst & XPRT_RETURN)     ? 'T' : 't',
            xprt_gc ? 'G' : 'g');
    return (buf);
}

#define NULL_SVC ((struct svc_callout *)0)

/*
 * The services list
 * Each entry represents a set of procedures (an rpc program).
 * The dispatch routine takes request structs and runs the
 * appropriate procedure.
 */

struct svc_callout {
    struct svc_callout *sc_next;
    rpcprog_t sc_prog;
    rpcvers_t sc_vers;
    void (*sc_dispatch) (struct svc_req *, SVCXPRT *);
    bool_t sc_mapped;
};

static struct svc_callout *svc_head;

UNUSED void
svc_backtrace(void)
{
    void *bt_buf[64];
    char **bt_strings;
    int bt_len;
    int i;

    bt_len = backtrace(bt_buf, 64);
    bt_strings = backtrace_symbols(bt_buf, bt_len);
    /*
     * Start with bt_buf[1], rather that bt_buf[0],
     * because we do not care about accounting for the call
     * to svc_backtrace(), itself.
     */
    for (i = 1; i < bt_len; ++i) {
        eprintf("  %s\n", bt_strings[i]);
    }
    free(bt_strings);
}

LIBRARY void
xports_global_lock(void)
{
    pthread_mutex_lock(&xports_lock);
    xports_owner = pthread_self();
}

LIBRARY void
xports_global_unlock(void)
{
    pthread_mutex_unlock(&xports_lock);
}

LIBRARY void
xports_snapshot()
{
    size_t count;

    if (xports_maxid == (size_t)(-1)) {
        xports_view_count = 0;
        return;
    }
    count = xports_maxid + 1;
    pthread_mutex_lock(&xports_view_lock);
    memcpy(xports_view, xports, count * sizeof (SVCXPRT *));
    xports_view_count = count;
    pthread_mutex_unlock(&xports_view_lock);
}

/*
 * Allocate memory for a @type{SVCXPRT}.  Allocate enough contiguous memory
 * for a "standard" SVCXPRT followed by the @type{mtxprt} extension.
 */
LIBRARY SVCXPRT *
alloc_xprt(void)
{
    void *mem;
    size_t xprt_size;

    xprt_size = sizeof (SVCXPRT) + sizeof (mtxprt_t);
    mem = guard_malloc(xprt_size);
    return ((SVCXPRT *)mem);
}

/*
 * Given the address of a standard @type{SVCXPRT}, return a pointer to its
 * @type{mtxprt}.  Don't do any checking, because this is a new SVCXPRT,
 * under construction.
 */
LIBRARY mtxprt_t *
xprt_to_mtxprt_nocheck(SVCXPRT *xprt)
{
    return ((mtxprt_t *)((char *)xprt + sizeof (SVCXPRT)));
}

/*
 * Given the address of a standard @type{SVCXPRT}, return a pointer to its
 * @type{mtxprt}, which is appended to the end of the libc @type{SVCXPRT}.
 * Check that the given pointer points to a properly constructed
 * @type{SVCXPRT}, including the @type{mtxprt}.
 *
 */
LIBRARY mtxprt_t *
xprt_to_mtxprt(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    size_t id;

    mtxprt = (mtxprt_t *)((char *)xprt + sizeof (SVCXPRT));
    id = mtxprt->mtxp_id;
    tprintf(9, "xprt=%s, id=%zu, mtxprt=%s, fd=%d\n",
        decode_addr(xprt), id, decode_addr(mtxprt), xprt->xp_sock);

    if (mtxprt->mtxp_magic != MTXPRT_MAGIC) {
        teprintf("xprt=%s -- Bad magic, %x.\n",
            decode_addr(xprt), mtxprt->mtxp_magic);
        svc_die();
    }

    if (memcmp(mtxprt->mtxp_guard, MTXPRT_GUARD, sizeof (mtxprt->mtxp_guard)) != 0) {
        teprintf("xprt=%s, id=%zu -- Guard is corrupt.\n",
            decode_addr(xprt), id);
        fprintf(stderr, "Guard=");
        fhexdump(stderr, 0, 0, mtxprt->mtxp_guard, sizeof (mtxprt->mtxp_guard));
        svc_die();
    }

    return (mtxprt);
}


/*
 * Every @type{SVCXPRT} has its own lock, which protects access to
 * its members.  This is probably more locking than is necessary,
 * since, once a SVCXPRT has been cloned, it should be used exclusively
 * by one worker thread.
 */

LIBRARY void
xprt_lock(SVCXPRT *xprt)
{
    mtxprt_t        *mtxprt;
    pthread_mutex_t *lockp;
    int ret;

    mtxprt = xprt_to_mtxprt(xprt);
    lockp = &mtxprt->mtxp_lock;
    tprintf(9, "xprt=%s, xprt_id=%zu, fd=%d\n",
        decode_addr(xprt), mtxprt->mtxp_id, xprt->xp_sock);
    ret = pthread_mutex_lock(lockp);
    if (ret != 0) {
        svc_die();
    }
}

LIBRARY void
xprt_unlock(SVCXPRT *xprt)
{
    mtxprt_t        *mtxprt;
    pthread_mutex_t *lockp;
    int ret;

    mtxprt = xprt_to_mtxprt(xprt);
    lockp = &mtxprt->mtxp_lock;
    tprintf(9, "xprt=%s, xprt_id=%zu, fd=%d\n",
        decode_addr(xprt), mtxprt->mtxp_id, xprt->xp_sock);
    ret = pthread_mutex_unlock(lockp);
    if (ret != 0) {
        svc_die();
    }
}

UNUSED int
xprt_is_locked(SVCXPRT *xprt)
{
    mtxprt_t        *mtxprt;
    pthread_mutex_t *lockp;

    mtxprt = xprt_to_mtxprt(xprt);
    lockp = &mtxprt->mtxp_lock;
    return (pthread_mutex_is_locked(lockp));
}

#ifdef USE_PROGRESS_LOCK

/*
 * xprt_progress_setbits()
 * Set some bits in progress field.  Return new value of progress.
 */

LIBRARY int
xprt_progress_setbits(SVCXPRT *xprt, int value)
{
    mtxprt_t *mtxprt;
    size_t id;
    pthread_mutex_t *lockp;
    int ret;
    int progress;
    char *vprogress;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    lockp = &mtxprt->mtxp_progress_lock;
    ret = pthread_mutex_lock(lockp);
    if (ret != 0) {
        svc_die();
    }
    progress = __sync_fetch_and_or(&(mtxprt->mtxp_progress), value);
    ret = pthread_mutex_unlock(lockp);
    if (ret != 0) {
        svc_die();
    }
    vprogress = decode_xprt_progress(xprt, size_t id);
    tprintf(8, "xprt=%s, id=%zu, value=%d, progress=0x%x=%s, fd=%d\n",
        decode_addr(xprt), id, value, progress, vprogress, xprt->xp_sock);
    return (progress);
}

/*
 * xprt_progress_clrbits()
 * Clear some bits in progress field.  Return new value of progress.
 */

LIBRARY int
xprt_progress_clrbits(SVCXPRT *xprt, int value)
{
    mtxprt_t *mtxprt;
    size_t id;
    pthread_mutex_t *lockp;
    int ret;
    int progress;
    char *vprogress;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    lockp = &mtxprt->mtxp_progress_lock;
    ret = pthread_mutex_lock(lockp);
    if (ret != 0) {
        svc_die();
    }
    progress = __sync_fetch_and_and(&(mtxprt->mtxp_progress), ~value);
    ret = pthread_mutex_unlock(lockp);
    if (ret != 0) {
        svc_die();
    }
    vprogress = decode_xprt_progress(xprt, size_t id);
    tprintf(8, "xprt=%s, id=%zu, value=%d, progress=0x%x=%s, fd=%d\n",
        decode_addr(xprt), id, value, progress, vprogress, xprt->xp_sock);
    return (progress);
}

LIBRARY int
xprt_get_progress(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    size_t id;
    pthread_mutex_t *lockp;
    int ret;
    int progress;
    char *vprogress;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    lockp = &mtxprt->mtxp_progress_lock;
    ret = pthread_mutex_lock(lockp);
    if (ret != 0) {
        svc_die();
    }
    progress = __sync_or_and_fetch(&(mtxprt->mtxp_progress), 0);
    ret = pthread_mutex_unlock(lockp);
    if (ret != 0) {
        svc_die();
    }
    vprogress = decode_xprt_progress(xprt, size_t id);
    tprintf(8, "xprt=%s, id=%zu, progress=0x%x=%s, fd=%d\n",
        decode_addr(xprt), id, progress, vprogress, xprt->xp_sock);
    return (progress);
}

#else /* USE_PROGRESS_LOCK */

/*
 * xprt_progress_setbits()
 * Set some bits in progress field.  Return new value of progress.
 */

LIBRARY int
xprt_progress_setbits(SVCXPRT *xprt, int value)
{
    mtxprt_t *mtxprt;
    size_t id;
    int progress;
    char *vprogress;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    progress = __sync_fetch_and_or(&(mtxprt->mtxp_progress), value);
    vprogress = decode_xprt_progress(xprt, id);
    tprintf(8, "xprt=%s, id=%zu, value=%d, progress=0x%x=%s, fd=%d\n",
        decode_addr(xprt), id, value, progress, vprogress, xprt->xp_sock);
    return (progress);
}

/*
 * xprt_progress_clrbits()
 * Clear some bits in progress field.  Return new value of progress.
 */

LIBRARY int
xprt_progress_clrbits(SVCXPRT *xprt, int value)
{
    mtxprt_t *mtxprt;
    size_t id;
    int progress;
    char *vprogress;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    progress = __sync_fetch_and_and(&(mtxprt->mtxp_progress), ~value);
    vprogress = decode_xprt_progress(xprt, id);
    tprintf(8, "xprt=%s, id=%zu, value=%d, progress=0x%x=%s, fd=%d\n",
        decode_addr(xprt), id, value, progress, vprogress, xprt->xp_sock);
    return (progress);
}

/*
 * Get the current value of the progress field for a given SVCXPRT.
 * Always use this method to get the value.  Never read the value directly.
 */

LIBRARY int
xprt_get_progress(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    size_t id;
    int progress;
    char *vprogress;

    if (xprt == BAD_SVCXPRT_PTR) {
        return (0);
    }

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    progress = __sync_or_and_fetch(&(mtxprt->mtxp_progress), 0);
    vprogress = decode_xprt_progress(xprt, id);
    tprintf(8, "xprt=%s, id=%zu, progress=0x%x=%s, fd=%d\n",
        decode_addr(xprt), id, progress, vprogress, xprt->xp_sock);
    return (progress);
}

#endif /* USE_PROGRESS_LOCK */


LIBRARY void
xprt_set_busy(SVCXPRT *xprt, int value)
{
    mtxprt_t *mtxprt;

    mtxprt = xprt_to_mtxprt(xprt);
    pthread_mutex_lock(&(mtxprt->mtxp_progress_lock));
    mtxprt->mtxp_busy = value;
    pthread_mutex_unlock(&(mtxprt->mtxp_progress_lock));
    tprintf(9, "xprt=%s, value=%d, fd=%d\n",
        decode_addr(xprt), value, xprt->xp_sock);
}

static void
xprt_reuse(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    pthread_mutex_t *lockp;
    size_t id;
    int ret;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    tprintf(7, "id=%zu\n", id);
    lockp = &mtxprt->mtxp_progress_lock;
    ret = pthread_mutex_lock(lockp);
    if (ret != 0) {
        svc_die();
    }
    mtxprt->mtxp_progress = 0;
    mtxprt->mtxp_busy = 0;
    ret = pthread_mutex_unlock(lockp);
    if (ret != 0) {
        svc_die();
    }

    pthread_mutex_init(&(mtxprt->mtxp_mtready), NULL);
    xprt_set_busy(xprt, 0);
}

/*
 * For all tcp sockets that are supposed to be busy,
 * check to see if the socket fd is really open.
 * If not, then there is something wrong.  So, die.
 *
 * Alternative policy: complain, but reap the xprt.
 */

static void
fsck_busy(void)
{
    size_t id;
    int busy;

    for (id = 0; id <= xports_maxid; ++id) {
        SVCXPRT *xprt;
        mtxprt_t *mtxprt;
        int fd;

        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        mtxprt = xprt_to_mtxprt(xprt);
        xprt_lock(xprt);
        fd = xprt->xp_sock;
        busy = mtxprt->mtxp_busy;
        if (busy && !fd_is_open(fd)) {
            eprintf("*** ERROR *** xprt %zu is busy\n"
                   " but its file descriptor, %d, is not open.\n",
                   id, fd);
            svc_die();
        }
        xprt_unlock(xprt);
    }
}

static void
fsck_gc(void)
{
}

#if 0

static void
fsck_gc(void)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    size_t id;
    size_t idset_count;
    size_t gflag_count;

    count = 0;
    for (id = 0; id <= xports_maxid; ++id) {
        if (bitvec_get_bit(&xprtgc_idset, id)) {
            ++idset_count;
        }
        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        mtxprt = xprt_to_mtxprt(xprt);
        if (bitvec_get_bit(&xprtgc_idset, id)) {
            ++gflag_count;
        }
    }
}

#endif

/*
 * Count how many SVCXPRT have been dispatched and are busy.
 * That is, they have unfinished business.
 * The number of busy transports is a rough measure of load
 * or concurrency.
 */


LIBRARY size_t
count_busy(void)
{
    size_t nbusy;
    size_t id;
    int busy;

    nbusy = 0;
    for (id = 0; id <= xports_maxid; ++id) {
        SVCXPRT *xprt;
        mtxprt_t *mtxprt;

        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        mtxprt = xprt_to_mtxprt(xprt);
        busy = mtxprt->mtxp_busy;
        if (busy) {
           ++nbusy;
        }
    }

    if (nbusy >= 2) {
        tprintf(7, "nbusy=%zu\n", nbusy);
    }
    return (nbusy);
}

static void
xprt_gc_mark(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    size_t id;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    pthread_mutex_lock(&xprtgc_lock);
    tprintf(2, "xprt=%s, id=%zu, fd=%d\n",
        decode_addr(xprt), id, xprt->xp_sock);
    if (!bitvec_get_bit(&xprtgc_idset, id)) {
        bitvec_set_bit(&xprtgc_idset, id);
        incr_counter(&xprtgc_mark_count);
    }
    pthread_mutex_unlock(&xprtgc_lock);
}

void
xprt_destroy_all_udp_clones(void)
{
    size_t id;

    for (id = 0; id <= xports_maxid; ++id) {
        SVCXPRT *xprt;
        mtxprt_t *mtxprt;

        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        mtxprt = xprt_to_mtxprt(xprt);
        if (mtxprt->mtxp_parent == NO_PARENT) {
            continue;
        }
        
        fflush(stderr);
        fprintf(stderr, "\n *** Destroy UDP clone SVCXPRT[%zu].\n", id);
        // XXX sleep(1);
        SVC_DESTROY(xprt);
        xports[id] = BAD_SVCXPRT_PTR;
    }
}

void
xprt_destroy_all_tcp_rendezvous(void)
{
    size_t id;

    for (id = 0; id <= xports_maxid; ++id) {
        SVCXPRT *xprt;

        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        if (xprt->xp_port != 0) {
            continue;
        }
        
        fflush(stderr);
        fprintf(stderr, "\n *** Destroy TCP rendezvous SVCXPRT[%zu].\n", id);
        // XXX sleep(1);
        SVC_DESTROY(xprt);
        xports[id] = BAD_SVCXPRT_PTR;
    }
}

/*
 * Destroy all SVCXPRT structures.
 * The only time this makes sense is when we are shuting down
 * the entire service, and preparing to exit the process.
 *
 * We could just exit, but this helps to ensure that we can
 * verify that we are capable of exiting cleanly, and allow
 * memory checking tools to verify that we do not leak anything
 * accidently.
 *
 * SVCXPRTs are destroyed in a definite prtial order.
 *   1) UDP clones,
 *   2) TCP rendezvous,
 *   3) Connection creators.
 *
 * UDP clones refer back to their parent connection SVXXPRT,
 * so it would be trickier to destroy the parent SVCXPRT, first.
 *
 * TCP rendezvous SVCXPRTs do not have 'parents', not in the same way,
 * but they still are suborinate to the SVCXPRT that created them.
 *
 */
void
xprt_destroy_all(void)
{
    size_t id;

    xprt_destroy_all_udp_clones();
    xprt_destroy_all_tcp_rendezvous();

    for (id = 0; id <= xports_maxid; ++id) {
        SVCXPRT *xprt;

        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        fflush(stderr);
        fprintf(stderr, "\n *** Destroy SVCXPRT[%zu].\n", id);
        // XXX sleep(1);
        SVC_DESTROY(xprt);
        xports[id] = BAD_SVCXPRT_PTR;
    }
}

static size_t
xprt_gc_reap_one(size_t id)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    size_t count = 0;

    if (!bitvec_get_bit(&xprtgc_idset, id)) {
        return (0);
    }

    if (xports[id] == BAD_SVCXPRT_PTR) {
        return (0);
    }

    pthread_mutex_lock(&xprtgc_lock);
    if (bitvec_get_bit(&xprtgc_idset, id)) {
        xprt = xports[id];
        mtxprt = xprt_to_mtxprt(xprt);
        if (opt_svc_trace >= 2) {
            tprintf(2, "xprt=%s, id=%zu, fd=%d\n",
                decode_addr(xprt), id, xprt->xp_sock);
            show_xports_hdr(4);
            show_xport(xports, id, 4);
        }
#if 0
        (void) mtxprt;
        SVC_DESTROY(xprt);
        xports[id] = BAD_SVCXPRT_PTR;
        count = 1;
#else
        /*
         * mtxprt->mtxp_parent != NO_PARENT  .iff.  it is a clone
         * of UDP connection, because only UDP connections get cloned.
         *
         * mtxprt->mtxp_refcnt == 0  .iff. it is a clone of a UDP
         * connection, because a parent UDP connection would have
         * a reference count.
         */
        if (mtxprt->mtxp_parent != NO_PARENT || mtxprt->mtxp_refcnt == 0) {
            SVC_DESTROY(xprt);
            xports[id] = BAD_SVCXPRT_PTR;
            count = 1;
        }
#endif
        bitvec_clr_bit(&xprtgc_idset, id);
        decr_counter(&xprtgc_mark_count);
    }
    pthread_mutex_unlock(&xprtgc_lock);

    return (count);
}

/*
 * Do actual destruction of @type{SVCXPRT}s that have been used,
 * and then marked for garbage collection.
 */

int
xprt_gc_reap_all(void)
{
    size_t id;
    size_t count;

    fsck_gc();
    fsck_busy();
    tprintf(4, "%zu SVCXPRT to be destroyed\n", xprtgc_mark_count);
    count = 0;
    for (id = 0; id <= xports_maxid && xprtgc_mark_count != 0; ++id) {
        if (bitvec_get_bit(&xprtgc_idset, id)) {
            count += xprt_gc_reap_one(id);
        }
    }
    return (count);
}

/*
 * Get the current value of the mtxp_busy field for a given SVCXPRT.
 * Always use this method to get the value.  Never read the value directly.
 */

static int
xprt_get_busy(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    size_t id;
    int busy;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    busy = mtxprt->mtxp_busy;
    tprintf(9, "xprt=%s, id=%zu, busy=%d, fd=%d\n",
        decode_addr(xprt), id, busy, xprt->xp_sock);
    if (!(busy == 0 || busy == 1)) {
        teprintf("xprt=%s, id=%zu, busy=%d, fd=%d\n",
            decode_addr(xprt), id, busy, xprt->xp_sock);
        teprintf("Invalid value for busy\n");
        svc_die();
    }
    return (busy);
}

static int
xprt_is_busy(SVCXPRT *xprt)
{
    int busy;

    if (xprt == BAD_SVCXPRT_PTR) {
        return (0);
    }

    busy = xprt_get_busy(xprt);
    return (busy);
}

LIBRARY SVCXPRT *
socket_to_xprt(int fd)
{
    if (fd < 0 || (size_t) fd > xports_size) {
        teprintf("fd (%d) out of range.\n", fd);
        svc_die();
    }
    return (sock_xports[fd]);
}

LIBRARY int
fd_is_busy(int fd)
{
    return (xprt_is_busy(sock_xports[fd]));
}

#ifdef CHECK_CREDENTIALS

static size_t
qcksum(const char *mem, size_t sz)
{
    size_t len;
    size_t pos;
    size_t s;

    s = 0;
    pos = 0;
    len = sz;
    while (len != 0) {
        s = (s * 33) + mem[pos];
        ++pos;
        --len;
    }

    return (s);
}
#endif

static void
show_xport(SVCXPRT **xprtv, size_t id, size_t indent)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    size_t parent_id;
    int busy;

    xprt = xprtv[id];
    if (xprt == BAD_SVCXPRT_PTR) {
        return;
    }

    eprintf("%*s", (int)indent, "");

    if (xprt == (SVCXPRT *)0) {
        eprintf("%5zu NULL\n", id);
        return;
    }

    mtxprt = xprt_to_mtxprt_nocheck(xprt);
    eprintf("%5zu %14s ", id, decode_addr(xprt));
    parent_id = mtxprt->mtxp_parent;
    if (parent_id == XPRT_ID_INVALID) {
        eprintf(" n/a ");
    }
    else {
        eprintf("%4zu ", parent_id);
    }
    busy = xprt_is_busy(xprt);
    eprintf("%4d %4d %4d %5d ",
        mtxprt->mtxp_refcnt,
        busy,
        xprt->xp_sock,
        xprt->xp_port);
#ifdef CHECK_CREDENTIALS
    eprintf("0x%08zx ", qcksum(mtxprt->mtxp_cred, sizeof (mtxprt->mtxp_cred)));
#endif
    eprintf("%s", decode_xprt_progress(xprt, id));
    eprintf("\n");
}

static void
show_xports_hdr(size_t indent)
{
    eprintf("%*s", (int)indent, "");
    eprintf("   id    addr        prnt rcnt busy sock  port\n");
    eprintf("%*s", (int)indent, "");
    eprintf("----- -------------- ---- ---- ---- ---- -----\n");
    //       12345 12345678901234 1234 1234 1234 1234 12345
}

LIBRARY void
show_xportv(SVCXPRT **xprtv, size_t size)
{
    size_t id;

    if (xprtv == NULL) {
        eprintf("\nxprtv == NULL\n");
        return;
    }

    if (size == 0) {
        eprintf("\nsize == 0\n");
        return;
    }

    eprintf("\nxports[]:\n");
    show_xports_hdr(0);

    for (id = 0; id < size; ++id) {
        show_xport(xprtv, id, 0);
    }
    eprintf("\n");
}

LIBRARY void
show_xports(void)
{
    xports_snapshot();
    show_xportv(xports_view, xports_view_count);
}

static void
show_xports_pollfd(void)
{
    struct pollfd *pollfdv;
    nfds_t slot;

    pollfdv = xports_pollfd;
    eprintf("\n");
    eprintf("xports_pollfd:\n");
    if (pollfdv == NULL) {
        eprintf("<NULL>\n");
        return;
    }
    eprintf("   slot  fd\n");
    eprintf("  ----- ---\n");
    for (slot = 0; slot < xports_max_pollfd; ++slot) {
        if (pollfdv[slot].fd != -1) {
            eprintf("  %5lu %3u\n", slot, pollfdv[slot].fd);
        }
    }
}

static void
show_xports_idset(void)
{
    size_t id;
    size_t count;

    eprintf("\n");
    eprintf("xports_idset:\n");
    count = 0;
    for (id = 0; id < xports_size; ++id) {
        if (bitvec_get_bit(&xports_idset, id)) {
            eprintf("%s%zu", count ? "," : "  ", id);
            ++count;
        }
    }
    if (count == 0) {
        eprintf("  <empty>");
    }
    eprintf("\n");
}

/*
 * Perform some validity checks on what is claimed to be a @type{SVCXPRT *}.
 *
 * Report any problems, but do not abort.
 * Instead, return status {0 = not valid, 1 = valid}.
 *
 */

static int
is_valid_svcxprt(SVCXPRT *xprt)
{
    if (xprt == (SVCXPRT *)NULL) {
        teprintf("Bad SVCXPRT ptr (NULL)\n");
        return (0);
    }

    if (xprt == BAD_SVCXPRT_PTR) {
        teprintf("Bad SVCXPRT ptr (%s)\n", decode_addr(xprt));
        return (0);
    }
    return (1);
}

/*
 * Validate what is claimed to be a @type{SVCXPRT *}.
 * If it not valid, then explain, dump several data structures
 * that might be handy, and call svc_die().
 *
 */
static void
check_svcxprt(SVCXPRT *xprt)
{
    if (!is_valid_svcxprt(xprt)) {
        show_xports();
        show_xports_pollfd();
        show_xports_idset();
        svc_die();
    }
}

/*
 * Validate a @type{SVCXPRT *} and assert that it exist in @var{xports}.
 *
 * The expected usage is that @function{xprt_register} is the only
 * caller of just plain @function{check_svcxprt}, and that all other
 * functions that do any checking would call @function{check_svcxprt_exists}.
 *
 */
static void
check_svcxprt_exists(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    int xprt_id;

    check_svcxprt(xprt);

    if (xports == NULL)
        return;

    mtxprt = xprt_to_mtxprt(xprt);
    xprt_id = mtxprt->mtxp_id;
    if (xports[xprt_id] != xprt) {
        teprintf("INTERNAL ERROR: xprt %s is not at xports[%d].\n",
            decode_addr(xprt), xprt_id);
        show_xports();
        show_xports_pollfd();
        show_xports_idset();
        svc_die();
    }
}

static int
check_xports_duplicates(void)
{
    SVCXPRT *xprt1, *xprt2;
    size_t id1, id2;
    int err;

    /*
     * Check for duplicate pointers in the set of all SVCXPRTs.
     */
    err = 0;
    for (id1 = 0; id1 <= xports_maxid; ++id1) {
        xprt1 = xports[id1];
        if (xprt1 != BAD_SVCXPRT_PTR) {
            for (id2 = id1 + 1; id2 <= xports_maxid; ++id2) {
                xprt2 = xports[id2];
                if (xprt2 != BAD_SVCXPRT_PTR) {
                    if (xprt2 == xprt1) {
                        eprintf("xports[%zu] == xports[%zu]\n", id2, id1);
                        err = 1;
                    }
                    else if (xprt2->xp_pad == xprt1->xp_pad) {
                        eprintf("xports[%zu]->xp_pad == xports[%zu]->xp_pad\n",
                            id2, id1);
                        err = 1;
                    }
                }
            }
        }
    }

    return (err);
}

/*
 * Perform a variety of consistency checks on the data structure
 * that manages @type{SVCXPRT}s, consisting of:
 *     xports, sock_xports, xports_idset.
 *
 * Sort of like @command{fsck}.
 *
 */

static int
check_xports(void)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    size_t id;
    int err;

    if (xports == NULL) {
        return (1);
    }

    assert(pthread_mutex_is_locked(&xports_lock));
    err = check_xports_duplicates();

    for (id = 0; id <= xports_maxid; ++id) {
        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        if (!is_valid_svcxprt(xprt)) {
            err = 1;
            continue;
        }
        mtxprt = xprt_to_mtxprt_nocheck(xprt);
        mtxprt->mtxp_fsck_refcnt = 0;
    }

    for (id = 0; id <= xports_maxid; ++id) {
        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        if (!is_valid_svcxprt(xprt)) {
            err = 1;
            continue;
        }
        mtxprt = xprt_to_mtxprt_nocheck(xprt);
        if (mtxprt->mtxp_parent == NO_PARENT) {
            SVCXPRT *sock_xprt;

            sock_xprt = sock_xports[xprt->xp_sock];
            if (!is_valid_svcxprt(sock_xprt)) {
                err = 1;
            }
            if (!bitvec_get_bit(&xports_idset, id)) {
                eprintf("id=%zu not in xports_idset.\n", id);
                err = 1;
            }
        }
        else {
            SVCXPRT *parent_xprt;
            mtxprt_t *parent_mtxprt;

            parent_xprt = xports[mtxprt->mtxp_parent];
            if (!is_valid_svcxprt(parent_xprt)) {
                err = 1;
            }
            parent_mtxprt = xprt_to_mtxprt(parent_xprt);
            ++parent_mtxprt->mtxp_fsck_refcnt;
        }
    }

    for (id = 0; id <= xports_maxid; ++id) {
        xprt = xports[id];
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }
        if (!is_valid_svcxprt(xprt)) {
            err = 1;
            continue;
        }
        mtxprt = xprt_to_mtxprt_nocheck(xprt);
        if (mtxprt->mtxp_refcnt != mtxprt->mtxp_fsck_refcnt) {
            eprintf("id=%zu -- expect ref count=%d, got %d.\n",
                id, mtxprt->mtxp_refcnt, mtxprt->mtxp_fsck_refcnt);
            err = 1;
        }
    }

    return (err == 0);
}


/* ***************  SVCXPRT related stuff **************** */

/*
 * Initialize the given array of SVCXPRTs to a known pattern,
 * for debugging purposes.
 */
static void
init_xports(SVCXPRT **xportv, size_t count)
{
    size_t id;

    for (id = 0; id < count; ++id) {
        xportv[id] = BAD_SVCXPRT_PTR;
    }
}

static void
create_xports(void)
{
    size_t size;

    size = (size_t) _rpc_dtablesize();
    if (size > FD_SETSIZE) {
        size = FD_SETSIZE;
    }
    xports_size = size;
    xports = (SVCXPRT **) guard_malloc(size * sizeof (SVCXPRT *));
    xports_view = (SVCXPRT **) guard_malloc(size * sizeof (SVCXPRT *));
    sock_xports = (SVCXPRT **) guard_malloc(size * sizeof (SVCXPRT *));
#ifdef SFR_SOCKET
    sock_sfr = (sock_sfr_t *) guard_malloc(size * sizeof (sock_sfr_t));
    memset((void *)sock_sfr, 0, size * sizeof (sock_sfr_t));
#endif
    init_xports(xports, size);
    init_xports(xports_view, size);
    init_xports(sock_xports, size);
    bitvec_init(&xports_idset, size);
    bitvec_init(&xprtgc_idset, size);
    xports_version = 0;
    xports_count = 0;
    xports_maxid = (size_t)(-1);
    xports_view_count = 0;
    xports_pollfd = (struct pollfd *) guard_malloc(size * sizeof (struct pollfd));
    xports_pollfd_size = size;
    xports_max_pollfd = 0;
}

void
destroy_xports(void)
{
    if (xports != NULL) {
        free(xports);
    }
    if (xports_view != NULL) {
        free(xports_view);
    }
    if (sock_xports != NULL) {
        free(sock_xports);
    }
    if (xports_pollfd != NULL) {
        free(xports_pollfd);
    }

    bitvec_free(&xports_idset);
    bitvec_free(&xprtgc_idset);

#ifdef SFR_SOCKET
    if (sock_sfr != NULL) {
        free(sock_sfr);
    }
#endif
}

LIBRARY void
xports_init(void)
{
    if (xports == NULL) {
        create_xports();
    }
}

LIBRARY void
xports_free(void)
{
    if (xports != NULL) {
        free(xports);
        xports = NULL;
    }
    if (xports_pollfd != NULL) {
        free(xports_pollfd);
        xports_pollfd = NULL;
    }
    xports_max_pollfd = 0;
}

static void
init_read_pollfd(struct pollfd *pollfdv, int slot, int fd)
{
    pollfdv[slot].fd = fd;
    pollfdv[slot].events = (POLLIN | POLLPRI);
    pollfdv[slot].revents = 0;
}

static int
init_pollfd(int fd)
{
    struct pollfd *pollfdv;
    nfds_t slot;

    /* Check if we have an empty slot */
    pollfdv = xports_pollfd;
    for (slot = 0; slot < xports_max_pollfd; ++slot) {
        if (pollfdv[slot].fd == -1) {
            init_read_pollfd(pollfdv, slot, fd);
            return (0);
        }
    }

    if (xports_max_pollfd > xports_pollfd_size) {
        teprintf("EBADF -- fd=%d, xports_max_pollfd=%ld\n",
            fd, xports_max_pollfd);
        return (EBADF);
    }

    xports_max_pollfd = slot + 1;

    init_read_pollfd(xports_pollfd, slot, fd);
    return (0);
}

/*
 * Allocator for @type{SVCXPRT} ids.
 *
 * Note that, since SVCXPRT structures can be cloned, the socket cannot
 * be used as a unique ID number.  So, we just allocate and free ID numbers,
 * independent of the socket.
 */
static size_t
xprt_id_alloc(void)
{
    size_t id;

    assert(pthread_mutex_is_locked(&xports_lock));
    if (xports_maxid == (size_t)(-1)) {
        id = 0;
        xports_maxid = 0;
    }
    else {
        for (id = 0; id <= xports_maxid; ++id) {
            if (!bitvec_get_bit(&xports_idset, id)) {
                break;
            }
        }
        if (id > xports_maxid) {
            xports_maxid = id;
        }
    }

    if (id >= xports_size) {
        /*
         * If this becomes a problem, and we really want to have
         * more outstanding SVCXPRT clones at one time, then switch
         * to an allocator that can handle ID numbers bigger than
         * @var{xports_size}.  vmem(), perhaps.
         */
        teprintf("Ran out of xprt IDs.  xports_size=%zu\n", xports_size);
        svc_die();
        return (XPRT_ID_INVALID);
    }

    bitvec_set_bit(&xports_idset, id);
    incr_counter(&xports_count);
    return (id);
}

#ifdef SFR_SOCKET

static inline void
sfr_track_xprt_socket(int sock, SVCXPRT *xprt)
{
    if (t0 == 0) {
        t0 = rdtsc();
    }

    sock_sfr_t *sfr = &sock_sfr[sock];

    sfr->sfr_timestamp = 0;     // Not valid
    sock_xports[sock] = xprt;
    sfr->sfr_psr = sched_getcpu();
    sfr->sfr_tid = pthread_self();

    // Record timestamp, and at the same time, mark sfr record as valid
    sfr->sfr_timestamp = rdtsc() - t0;
}

#endif /* SFR_SOCKET */


/*
 * Sockets already in use are accounted for in the array, sock_xports[].
 *
 * A socket is available for reuse if its entry in that array is
 * NULL or BAD_SVCXPRT_PTR.  It is also OK to reuse a socket if its
 * entry in sock_xports[] refers to a SVCXPRT structure that has
 * completed, but has not yet been destroyed.  Presumably, that
 * SVCXPRT is on the GC list.
 */
static int
socket_xprt_is_available(SVCXPRT *sxprt)
{
    mtxprt_t *mtxprt;

    if (sxprt == NULL) {
        return (1);
    }

    if (sxprt == BAD_SVCXPRT_PTR) {
        return (1);
    }

    mtxprt = xprt_to_mtxprt(sxprt);
    if (mtxprt->mtxp_progress & XPRT_RETURN) {
        return (1);
    }

    return (0);
}

/*
 * Add a @type{SVCXPRT} data structure to @var{xports} and @var{sock_xports}.
 * This operation needs to be protected by @var{xports_lock}.
 *
 */

LIBRARY int
xprt_register_with_lock(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    size_t xprt_id;
    int sock;
    int err;

    assert(pthread_mutex_is_locked(&xports_lock));
    mtxprt = xprt_to_mtxprt(xprt);
    xprt_id = mtxprt->mtxp_id;
    sock = xprt->xp_sock;

    if (xports == NULL) {
        create_xports();
    }

    if (xports == NULL) {    /* Don't add handle */
        return (0);
    }

    if (xprt_id == XPRT_ID_INVALID) {
        xprt_id = xprt_id_alloc();
        mtxprt->mtxp_id = xprt_id;
    }

    if (opt_svc_trace >= 1) {
        show_xports();
        show_rate_limit_stats();
    }

    tprintf(2, "xprt=%s, xprt_id=%zu, sock=%d, parent=%s",
        decode_addr(xprt),
        xprt_id,
        sock,
        decode_xid(mtxprt->mtxp_parent, "none"));

    if (xprt_id >= xports_size) {
        teprintf("xprt_id >= xports_size (%zu)\n", xports_size);
        svc_die();
    }

    xports[xprt_id] = xprt;
    incr_counter(&xports_version);

    err = 0;
    if (mtxprt->mtxp_parent == NO_PARENT) {
        SVCXPRT *sxprt;

        sxprt = sock_xports[sock];
        if (!socket_xprt_is_available(sxprt)) {
            teprintf("sock_xports[sock]=%s -- should be vacant.\n",
                decode_addr(sxprt));
            svc_die();
        }

#if defined(SFR_SOCKET)
        sfr_track_xprt_socket(sock, xprt);
#endif /* SFR_SOCKET */

        err = init_pollfd(sock);
    }
    else {
        SVCXPRT *parent_xprt;
        mtxprt_t *parent_mtxprt;

        parent_xprt = xports[mtxprt->mtxp_parent];
        parent_mtxprt = xprt_to_mtxprt(parent_xprt);
        ++parent_mtxprt->mtxp_refcnt;
    }

    return (err);
}

/*
 * Activate a transport handle.
 */
LIBRARY void
xprt_register(SVCXPRT *xprt)
{
    int err;

    check_svcxprt(xprt);
    pthread_mutex_lock(&io_lock);
    xports_global_lock();
    err = xprt_register_with_lock(xprt);
    xports_global_unlock();
    pthread_mutex_unlock(&io_lock);
    if (err) {
        svc_die();
    }
}

/*
 * Remove all occurrences of the given socket fd, @var{fd},
 * from a @type{struct pollfd}.
 *
 * Do not actually reduce the size of the @type{struct pollfd};
 * just replace all occurrences of @var{fd} with -1.
 */

static void
pollfd_remove(struct pollfd *pollfdv, nfds_t pollfdsz, int sock)
{
    nfds_t slot;

    for (slot = 0; slot < pollfdsz; ++slot) {
        if (pollfdv[slot].fd == sock) {
            pollfdv[slot].fd = -1;
        }
    }
}

static void
unregister_id(size_t id)
{
    if (!id_is_valid(id)) {
        teprintf("Bad id: %zu\n", id);
        svc_die();
    }

    assert(pthread_mutex_is_locked(&xports_lock));

    xports[id] = BAD_SVCXPRT_PTR;
    incr_counter(&xports_version);

    tprintf(7, "free id=%zu\n", id);
    bitvec_clr_bit(&xports_idset, id);
    decr_counter(&xports_count);

#if 0
    if (xports_maxid > 0) {
        for (id = xports_maxid; id != 0; --id) {
            if (xports[id] == NULL || xports[id] == BAD_SVCXPRT_PTR) {
                decr_counter(&xports_maxid);
            }
        }
    }
#endif
}

/*
 * De-activate a transport handle.
 */

LIBRARY int
xprt_unregister_with_lock(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    size_t id;

    check_svcxprt_exists(xprt);
    mtxprt = xprt_to_mtxprt(xprt);

    id = mtxprt->mtxp_id;

    if (!id_is_valid(id)) {
        teprintf("Bad id: %zu\n", id);
        svc_die();
    }

    if (xports[id] != xprt) {
        teprintf("xports[%zu] != xprt(%s)\n", id, decode_addr(xprt));
        svc_die();
    }

    if (mtxprt->mtxp_parent == NO_PARENT) {
        int sock;

        sock = xprt->xp_sock;
        pollfd_remove(xports_pollfd, xports_max_pollfd, sock);
        sock_xports[sock] = BAD_SVCXPRT_PTR;
    }
    else {
        SVCXPRT *parent_xprt;
        mtxprt_t *parent_mtxprt;

        parent_xprt = xports[mtxprt->mtxp_parent];
        parent_mtxprt = xprt_to_mtxprt(parent_xprt);
        --parent_mtxprt->mtxp_refcnt;
    }
    unregister_id(id);
    return (0);
}

LIBRARY void
xprt_unregister(SVCXPRT *xprt)
{
    int err;

    assert(pthread_mutex_is_locked(&xports_lock));
    err = xprt_unregister_with_lock(xprt);
    if (err) {
        svc_die();
    }
}

/* ********************** CALLOUT list related stuff ************* */

/*
 * Search the callout list for a program number;
 * return the callout struct.
 */
static struct svc_callout *
svc_find(rpcprog_t prog, rpcvers_t vers, struct svc_callout **prev)
{
    struct svc_callout *s, *p;

    p = NULL_SVC;
    for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
        if ((s->sc_prog == prog) && (s->sc_vers == vers)) {
            break;
        }
        p = s;
    }

    *prev = p;
    return (s);
}

static bool_t
svc_is_mapped(rpcprog_t prog, rpcvers_t vers)
{
    struct svc_callout *prev;
    struct svc_callout *s;

    s = svc_find(prog, vers, &prev);
    return (s != NULL_SVC && s->sc_mapped);
}


/*
 * Add a service program to the callout list.
 *
 * The dispatch routine will be called when a rpc request
 * for this program number comes in.
 */
PUBLIC bool_t
svc_register(SVCXPRT *xprt, rpcprog_t prog, rpcvers_t vers,
    void (*dispatch) (struct svc_req *, SVCXPRT *), rpcproc_t protocol)
{
    struct svc_callout *prev;
    struct svc_callout *s;

    check_svcxprt_exists(xprt);

    if ((s = svc_find(prog, vers, &prev)) != NULL_SVC) {
        if (s->sc_dispatch == dispatch) {
            /* he is registering another xptr */
            goto pmap_it;
        }
        return (FALSE);
    }
    s = (struct svc_callout *)svc_l1_alloc(sizeof (struct svc_callout));

    s->sc_prog = prog;
    s->sc_vers = vers;
    s->sc_dispatch = dispatch;
    s->sc_next = svc_head;
    s->sc_mapped = FALSE;
    svc_head = s;

  pmap_it:
    /* now register the information with the local binder service */
    if (protocol) {
        if (!pmap_set(prog, vers, protocol, xprt->xp_port)) {
            return (FALSE);
        }
        s->sc_mapped = TRUE;
    }

    return (TRUE);
}

/*
 * Remove a service program from the callout list.
 */
PUBLIC void
svc_unregister(rpcprog_t prog, rpcvers_t vers)
{
    struct svc_callout *prev;
    struct svc_callout *s;

    s = svc_find(prog, vers, &prev);
    if (s == NULL_SVC)
        return;

    if (prev == NULL_SVC)
        svc_head = s->sc_next;
    else
        prev->sc_next = s->sc_next;

    s->sc_next = NULL_SVC;
    free(s);
    /*
     * Now, unregister the information with the local binder service.
     */
    if (!svc_is_mapped(prog, vers))
        pmap_unset(prog, vers);
}

/* ******************* REPLY GENERATION ROUTINES  ************ */

/*
 * Send a reply to an rpc request
 */
PUBLIC bool_t
svc_sendreply(SVCXPRT *xprt, xdrproc_t xdr_results, caddr_t xdr_location)
{
    struct rpc_msg rply;

    check_svcxprt_exists(xprt);
    rply.rm_direction = REPLY;
    rply.rm_reply.rp_stat = MSG_ACCEPTED;
    rply.acpted_rply.ar_verf = xprt->xp_verf;
    rply.acpted_rply.ar_stat = SUCCESS;
    rply.acpted_rply.ar_results.where = xdr_location;
    rply.acpted_rply.ar_results.proc = xdr_results;
    return (SVC_REPLY(xprt, &rply));
}

/*
 * No procedure error reply
 */
PUBLIC void
svcerr_noproc(SVCXPRT *xprt)
{
    struct rpc_msg rply;

    check_svcxprt_exists(xprt);
    rply.rm_direction = REPLY;
    rply.rm_reply.rp_stat = MSG_ACCEPTED;
    rply.acpted_rply.ar_verf = xprt->xp_verf;
    rply.acpted_rply.ar_stat = PROC_UNAVAIL;
    SVC_REPLY(xprt, &rply);
}

/*
 * Can't decode args error reply
 */
PUBLIC void
svcerr_decode(SVCXPRT *xprt)
{
    struct rpc_msg rply;

    check_svcxprt_exists(xprt);
    rply.rm_direction = REPLY;
    rply.rm_reply.rp_stat = MSG_ACCEPTED;
    rply.acpted_rply.ar_verf = xprt->xp_verf;
    rply.acpted_rply.ar_stat = GARBAGE_ARGS;
    SVC_REPLY(xprt, &rply);
}

/*
 * Some system error
 */
PUBLIC void
svcerr_systemerr(SVCXPRT *xprt)
{
    struct rpc_msg rply;

    check_svcxprt_exists(xprt);
    rply.rm_direction = REPLY;
    rply.rm_reply.rp_stat = MSG_ACCEPTED;
    rply.acpted_rply.ar_verf = xprt->xp_verf;
    rply.acpted_rply.ar_stat = SYSTEM_ERR;
    SVC_REPLY(xprt, &rply);
}

/*
 * Authentication error reply
 */
LIBRARY void
svcerr_auth(SVCXPRT *xprt, enum auth_stat why)
{
    struct rpc_msg rply;

    tprintf(2, "xprt=%s\n", decode_addr(xprt));
    check_svcxprt_exists(xprt);
    rply.rm_direction = REPLY;
    rply.rm_reply.rp_stat = MSG_DENIED;
    rply.rjcted_rply.rj_stat = AUTH_ERROR;
    rply.rjcted_rply.rj_why = why;
    SVC_REPLY(xprt, &rply);
}

/*
 * Auth too weak error reply
 */
LIBRARY void
svcerr_weakauth(SVCXPRT *xprt)
{
    check_svcxprt_exists(xprt);
    svcerr_auth(xprt, AUTH_TOOWEAK);
}

/*
 * Program unavailable error reply
 */
LIBRARY void
svcerr_noprog(SVCXPRT *xprt)
{
    struct rpc_msg rply;

    check_svcxprt_exists(xprt);
    rply.rm_direction = REPLY;
    rply.rm_reply.rp_stat = MSG_ACCEPTED;
    rply.acpted_rply.ar_verf = xprt->xp_verf;
    rply.acpted_rply.ar_stat = PROG_UNAVAIL;
    SVC_REPLY(xprt, &rply);
}

/*
 * Program version mismatch error reply
 */
LIBRARY void
svcerr_progvers(SVCXPRT *xprt, rpcvers_t low_vers, rpcvers_t high_vers)
{
    struct rpc_msg rply;

    check_svcxprt_exists(xprt);
    rply.rm_direction = REPLY;
    rply.rm_reply.rp_stat = MSG_ACCEPTED;
    rply.acpted_rply.ar_verf = xprt->xp_verf;
    rply.acpted_rply.ar_stat = PROG_MISMATCH;
    rply.acpted_rply.ar_vers.low = low_vers;
    rply.acpted_rply.ar_vers.high = high_vers;
    SVC_REPLY(xprt, &rply);
}

/* ******************* SERVER INPUT STUFF ******************* */

/*
 * Get server side input from some transport.
 *
 * Statement of authentication parameters management:
 * This function owns and manages all authentication parameters, specifically
 * the "raw" parameters (msg.rm_call.cb_cred and msg.rm_call.cb_verf) and
 * the "cooked" credentials (rqst->rq_clntcred).
 * However, this function does not know the structure of the cooked
 * credentials, so it makes the following assumptions:
 *   a) the structure is contiguous (no pointers), and
 *   b) the cred structure size does not exceed RQCRED_SIZE bytes.
 * In all events, all three parameters are freed upon exit from this routine.
 * The storage is trivially management on the call stack in user land, but
 * is mallocated in kernel land.
 */

PUBLIC void
svc_getreq(int rdfds)
{
    fd_set readfds;

    FD_ZERO(&readfds);
    if (rdfds != 1) {
        teprintf("\n");
        svc_die();
    }

    FD_SET(0, &readfds);
    // readfds.fds_bits[0] = rdfds;
    svc_getreqset(&readfds);
}

PUBLIC void
svc_getreqset(fd_set *readfds)
{
    __fd_mask mask;
    __fd_mask *maskp;
    int setsize;
    int sock_grp;
    int sock;
    int bit;

    setsize = xports_size;

    /*
     * This code assumes that an fd_set is a pure array of some integral type,
     * which comprises a bignum-like mask for a potentially large set of
     * of file descriptors.  It breaks if @type{fd_set} contains any other
     * data, especially if it causes the bitmap to be located anywhere
     * other than at offset 0.
     *
     * It also assumes that @type{__fd_mask} is long.
     *
     */

    maskp = (__fd_mask *)&readfds;
    for (sock_grp = 0; sock_grp < setsize; sock_grp += __NFDBITS) {
        for (mask = *maskp++; (bit = ffsl(mask)); mask ^= (1L << (bit - 1))) {
            sock = sock_grp + bit - 1;
            tprintf(2, "sock=%d\n", sock);
            svc_getreq_common(sock);
        }
    }
}

LIBRARY void
svc_getreq_poll_mt(struct pollfd *pfdp, nfds_t npoll, int pollretval)
{
    int fds_found;
    nfds_t i;

    if (pollretval == 0)
        return;

    tprintf(2, "npoll=%ld, pollretval=%d\n", npoll, pollretval);

    fds_found = 0;
    for (i = 0; i < npoll; ++i) {
        struct pollfd *p;

        p = &pfdp[i];
        if (p->fd != -1 && p->revents) {
            ++fds_found;
        }
    }

    if (fds_found < pollretval) {
        tprintf(2, "fds_found=%d, pollretval=%d\n", fds_found, pollretval);
        pollretval = fds_found;
    }

    fds_found = 0;
    for (i = 0; i < npoll; ++i) {
        struct pollfd *p;

        p = &pfdp[i];
        if (p->fd != -1 && p->revents) {
            /* fd has input waiting */
            if (p->revents & POLLNVAL) {
                tprintf(2, "POLLNVAL: p->fd=%d, fds_found=%d\n",
                    p->fd, fds_found);
            }
            else {
                svc_getreq_common(p->fd);
            }

            ++fds_found;
            if (fds_found >= pollretval)
                break;
        }
    }

    /*
     * Do xprt garbage collection after all the calls to svc_getreq_common().
     * We retire xprts at the start of servicing a new request, because
     * it is safe to do it there, while we are briefly in single-thread
     * mode, before mtmode dispatch allocates resources and dispatches
     * a new request.  But, if we do garbage collection _only_ there,
     * then some leftovers can stay not destroyed for some time,
     * depending on the arrival of a new request.
     */
    if (mtmode != 0) {
         (void) xprt_gc_reap_all();
    }
}

/*
 * Clone a @type{SVCXPRT}.
 * Delegate the details to svcudp_xprt_clone() or svctcp_xprt_clone(),
 * or whatever.  The underlying method is in the extension, @type{mtxprt}.
 */
static SVCXPRT *
svc_xprt_clone(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;

    mtxprt = xprt_to_mtxprt(xprt);
    return ((*(mtxprt->mtxp_clone))(xprt));
}

#if 0

/*
 * Wait for a given SVCXPRT to get to a certain milestone.
 * Wait for a "jiffy" at a time.
 * The value of a "jiffy" is configurable.
 * Default is 10 microseconds.
 *
 * If tracing is turned on and trace level >= 8,
 * then update waiting message every @var{wait_trace_interval} seconds.
 * Default is 5 seconds.
 */

static void
wait_on_busy(SVCXPRT *xprt)
{
    const struct timespec ts_jiffy = { 0, jiffy };
    const long jiffies_per_second = 1000000000 / jiffy;
    mtxprt_t *mtxprt;
    size_t id;
    long jiffies_per_interval;
    size_t wait_seconds;

    xprt_progress_setbits(xprt, XPRT_WAIT);
    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    tprintf(8, "xprt=%s, id=%zu, fd=%d\n",
	    decode_addr(xprt), id, xprt->xp_sock);
    if (opt_svc_trace >= 8) {
        show_xports();
    }

    jiffies_per_interval = wait_trace_interval * jiffies_per_second;
    wait_seconds = 0;
    for (;;) {
        long i;

        for (i = 0; i < jiffies_per_interval; ++i) {
            int progress;
            int busy;

            progress = xprt_get_progress(xprt);
            if ((progress & XPRT_RETURN) != 0) {
                return;
            }
            busy = xprt_get_busy(xprt);
            if (busy != 0) {
                xprt_progress_clrbits(xprt, XPRT_WAIT);
                return;
            }
            nanosleep(&ts_jiffy, NULL);
        }

        wait_seconds += wait_trace_interval;
        tprintf(8, "Waiting for %zu seconds.\n", wait_seconds);
    }
}

#endif

/*
 * Wait for a given SVCXPRT to get to the milestone, 'getargs'.
 * Also, wait for the milestone, 'return', because the worker thread
 * could complete the entire task, quickly, before we even start
 * to wait.
 *
 * Wait for a "jiffy" at a time.
 * The value of a "jiffy" is configurable.
 * Default is 10 microseconds.
 *
 * If tracing is turned on and trace level >= 8,
 * then update waiting message every @var{wait_trace_interval} seconds.
 * Default is 5 seconds.
 */

static void
wait_on_getargs_usleep(SVCXPRT *xprt)
{
    const struct timespec ts_jiffy = { 0, jiffy };
    const long jiffies_per_second = 1000000000 / jiffy;
    mtxprt_t *mtxprt;
    size_t id;
    long jiffies_per_interval;
    size_t wait_seconds;

    xprt_progress_setbits(xprt, XPRT_WAIT);
    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    tprintf(7, "xprt=%s, id=%zu, fd=%d\n",
	    decode_addr(xprt), id, xprt->xp_sock);
    if (opt_svc_trace >= 8) {
        show_xports();
    }

    jiffies_per_interval = wait_trace_interval * jiffies_per_second;
    wait_seconds = 0;
    for (;;) {
        int progress = 0;
        int mask;
        int progress_of_interest;
        long i;

        for (i = 0; i < jiffies_per_interval; ++i) {
            progress = xprt_get_progress(xprt);
            mask = XPRT_GETARGS | XPRT_RETURN;
            progress_of_interest = progress & mask;
            tprintf(9, "progress=0x%x, mask=0x%x, progress_of_interest=0x%x.\n",
                progress, mask, progress_of_interest);
            if (progress_of_interest) {
                xprt_progress_clrbits(xprt, XPRT_WAIT);
                return;
            }
            nanosleep(&ts_jiffy, NULL);
        }

        wait_seconds += wait_trace_interval;
        tprintf(7, "Waiting for %zu seconds - progress=0x%x=%s.\n",
            wait_seconds, progress, decode_xprt_progress(xprt, id));
    }
}

/*
 * Wait for a given SVCXPRT to get to the milestone, 'getargs'.
 * Wait using pthread_timed_lock().
 * The lock must be locked when the SVCXPRT is created.
 * Then, it must be unlocked by svc_getargs().
 *
 * We could just use pthread_mutex_lock().  That would work.
 * The reason for using pthread_timed_lock() is that we want
 * to show status and/or some sort of progress messages, periodically.
 * The time interval between messages would normally be very long
 * compared to the average wait time.  So, one should hardly ever
 * see progress messages ... but, just in case.
 *
 * If tracing is turned on and trace level >= 8,
 * then update waiting message every @var{wait_trace_interval} seconds.
 * Default is 5 seconds.
 */

static void
wait_on_getargs_mutex(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    pthread_mutex_t *lockp;
    size_t id;
    size_t wait_seconds;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    tprintf(7, "xprt=%s, id=%zu, fd=%d\n",
	    decode_addr(xprt), id, xprt->xp_sock);
    lockp = &mtxprt->mtxp_mtready;

    wait_seconds = 0;
    for (;;) {
        struct timespec ts;
        int rv;

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += wait_trace_interval;
        rv = pthread_mutex_timedlock(lockp, &ts);
        switch (rv) {
        case 0:
            return;
        case ETIMEDOUT:
            wait_seconds += wait_trace_interval;
            tprintf(7, "Waiting on getargs for %zu seconds.\n", wait_seconds);
            break;
        default:
            teprintf("pthread_mutex_timedlock() error %d.\n", rv);
            svc_die();
            break;
        }
    }
}

static void
wait_on_getargs(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    size_t id;
    int sock;
    int wait_method;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    sock = xprt->xp_sock;
    if (mtxprt->mtxp_parent == NO_PARENT) {
        wait_method = wait_method_tcp;
    }
    else {
        wait_method = wait_method_udp;
    }
    tprintf(2, "wait_on_getargs(id=%zu), fd=%d\n", id, sock);
    if (wait_method == WAIT_MUTEX) {
        wait_on_getargs_mutex(xprt);
    }
    else {
        wait_on_getargs_usleep(xprt);
    }
    tprintf(2, "wait done: id=%zu, fd=%d\n", id, sock);
}

/*
 * Wait for the single worker thread to return.
 * This only makes sense in mtmode == 0, that is single-threaded mode.
 *
 * Wait for a "jiffy" at a time.
 * The value of a "jiffy" is configurable.
 * Default is 10 microseconds.
 *
 * If tracing is turned on and trace level >= 8,
 * then update waiting message every @var{wait_trace_interval} seconds.
 * Default is 5 seconds.
 */

static void
wait_on_return(void)
{
    const struct timespec ts_jiffy = { 0, jiffy };
    const long jiffies_per_second = 1000000000 / jiffy;
    long jiffies_per_interval;
    size_t wait_seconds;

    tprintf(8, "Wait for event RETURN.\n");
    if (opt_svc_trace >= 8) {
        show_xports();
    }

    jiffies_per_interval = wait_trace_interval * jiffies_per_second;
    wait_seconds = 0;
    for (;;) {
        long i;

        for (i = 0; i < jiffies_per_interval; ++i) {
            if (worker_return != 0) {
                return;
            }
            nanosleep(&ts_jiffy, NULL);
        }

        wait_seconds += wait_trace_interval;
        tprintf(8, "Waiting for event RETURN for %zu seconds.\n", wait_seconds);
    }
}

/*
 * PROGRAM LOGIC
 * -------------
 * Summary of program logic for request, lookup, and dispatch
 *
 * |struct req| is a context object that is used only for keeping
 * track of information about the current request being processed.
 *
 * Call graph starting at svc_getreq_common()
 *
 *  svc_getreq_common(const int fd)
 *      |
 *      +- svc_getreq_common_rv()
 *             |
 *             +- get_single_request()
 *                    |
 *                    +- request_lookup()
 *                           |
 *                           +- request_match_prog_version()
 *                                  |
 *                                  +- request_dispatch()
 *
 * The struct req gets passed down
 * from get_single_request() ... request_dispatch(),
 * accumulating more information needed to service the request,
 * on the way down, and carrying status that percolates back up.
 *
 * This is just to make it palatable to express the program logic
 * as a collection of small functions instead of a giant loop with
 * lots of C |break|, |continue|, |goto| for flow of control,
 * but without having to pass a big mess of arguments at each
 * function call.
 *
 */

struct req {
    int fd;
    SVCXPRT  *xprt;
    mtxprt_t *mtxprt;
    SVCXPRT  *worker_xprt;
    struct rpc_msg *msgp;
    struct svc_req *rqstp;

    int rv;
    int  err;
    enum xprt_stat xrv;
};

typedef struct req req_t;

static void
request_dispatch(req_t *reqp, struct svc_callout *s)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    struct svc_req *xprt_rqstp;
    struct svc_req *rqstp;

    incr_counter(&cnt_request_disp);

    // Determine if we will be dispatching on the original SVCXPRT
    // or on a clone.
    xprt = reqp->xprt;
    if (mtmode) {
        // In either mtmode==1 or mtmode==2,
        // we dispatch on the clone, if there is one.
        mtxprt = xprt_to_mtxprt(xprt);
        if (mtxprt->mtxp_clone != NULL) {
            reqp->worker_xprt = svc_xprt_clone(xprt);
        }
        else {
            reqp->worker_xprt = xprt;
        }
    }
    else {
        // Not multi-threaded.  There are no clone xprts.
        // So, our worker xprt is just the original.
        reqp->worker_xprt = xprt;
        worker_return = 0;
    }

    // Now that we know wich is the worker SVCXPRT,
    // set @var{xprt} and @var{mtxprt} based on that.
    xprt = reqp->worker_xprt;
    mtxprt = xprt_to_mtxprt(xprt);
    xprt_rqstp = &(mtxprt->mtxp_rqst);

    if (mtxprt->mtxp_progress & XPRT_RETURN) {
        if (mtmode == 0) {
            SVC_DESTROY(reqp->worker_xprt);
            return;
        }
        else if (xprt_is_reusable(xprt)) {
            mtxprt->mtxp_progress = 0;
            mtxprt->mtxp_busy = 0;
        }
        else {
            xprt_gc_mark(xprt);
            return;
        }
    }

    rqstp = reqp->rqstp;
    tprintf(2, "> dispatch: prog=%d proc=%d fd=%d\n",
        (int)rqstp->rq_prog, (int)rqstp->rq_proc, reqp->fd);
    xprt_progress_setbits(xprt, XPRT_DISPATCH);
    (*s->sc_dispatch)(xprt_rqstp, xprt);
    tprintf(2, "< dispatch: prog=%d proc=%d fd=%d\n",
        (int)rqstp->rq_prog, (int)rqstp->rq_proc, reqp->fd);

    switch (mtmode) {
      default:
        teprintf(
            "Invalid value for mtmode, %d.\n"
            "Valid values are 0, 1, 2.\n",
            mtmode);
        svc_die();
        break;
      case 0:
        wait_on_return();
        break;
      case 1:
        wait_on_getargs(xprt);
        break;
      case 2:
        // Do not wait
        break;
    }
}

/*
 * Request has been received and then authenticated.
 * Now, match message with a registered service.
 */
static void
request_match_prog_version(req_t *reqp)
{
    struct svc_callout *s;
    rpcvers_t low_vers;
    rpcvers_t high_vers;
    bool prog_found;

    low_vers = __MAX(rpcvers_t);
    high_vers = __MIN(rpcvers_t);
    prog_found = false;
    for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
        if (s->sc_prog == reqp->rqstp->rq_prog) {
            prog_found = true;
            if (s->sc_vers == reqp->rqstp->rq_vers) {
                // Found correct version
                request_dispatch(reqp, s);
                return;
            }
            if (s->sc_vers < low_vers) {
                low_vers = s->sc_vers;
            }
            if (s->sc_vers > high_vers) {
                high_vers = s->sc_vers;
            }
        }
    }

    if (!prog_found) {
        tprintf(2, "svcerr_noprog()\n");
        svcerr_noprog(reqp->worker_xprt);
        return;
    }

    tprintf(2, "svcerr_progvers()\n");
    svcerr_progvers(reqp->worker_xprt, low_vers, high_vers);
}

/*
 * We received a request.  Now, find the exported program and call it.
 */
static void
request_lookup(req_t *reqp)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    struct rpc_msg *msgp;
    struct svc_req *rqstp;
    enum auth_stat why;

    // Populate local variables from reqp
    xprt   = reqp->xprt;
    mtxprt = reqp->mtxprt;
    msgp   = reqp->msgp;

    // Start with clean status
    reqp->rv  = 0;
    reqp->err = 0;
    reqp->xrv = 0;

    rqstp = &(mtxprt->mtxp_rqst);
#ifdef CHECK_CREDENTIALS
    rqstp->rq_clntcred = &(mtxprt->mtxp_cred[2 * MAX_AUTH_BYTES]);
#else
    rqstp->rq_clntcred = NULL;
#endif
    rqstp->rq_xprt = xprt;
    rqstp->rq_prog = msgp->rm_call.cb_prog;
    rqstp->rq_vers = msgp->rm_call.cb_vers;
    rqstp->rq_proc = msgp->rm_call.cb_proc;
    rqstp->rq_cred = msgp->rm_call.cb_cred;

    /* first authenticate the message */
    /* Check for null flavor and bypass these calls if possible */

    if (msgp->rm_call.cb_cred.oa_flavor == AUTH_NULL) {
        rqstp->rq_xprt->xp_verf.oa_flavor = _null_auth.oa_flavor;
        rqstp->rq_xprt->xp_verf.oa_length = 0;
    }
    else if ((why = _authenticate(rqstp, msgp)) != AUTH_OK) {
        tprintf(2, "\n");
        svcerr_auth(xprt, why);
        reqp->rv  = -1;
        reqp->err = 1;
        return;
    }

    reqp->rqstp = rqstp;
    request_match_prog_version(reqp);
}

static void
get_single_request(req_t *reqp)
{
    struct rpc_msg *msgp;
    mtxprt_t *mtxprt;
    int done = 0;

#if 0
    if (mtmode != 0) {
        (void) xprt_gc_reap_all();
    }
#endif

    reqp->mtxprt = xprt_to_mtxprt(reqp->xprt);
    mtxprt = reqp->mtxprt;
    msgp = &mtxprt->mtxp_msg;
#ifdef CHECK_CREDENTIALS
    msgp->rm_call.cb_cred.oa_base = &(mtxprt->mtxp_cred[0]);
    msgp->rm_call.cb_verf.oa_base = &(mtxprt->mtxp_cred[MAX_AUTH_BYTES]);
#else
    msgp->rm_call.cb_cred.oa_base = NULL;
    msgp->rm_call.cb_verf.oa_base = NULL;
#endif

    // In case we fail before xprt is cloned.
    reqp->worker_xprt = reqp->xprt;

    reqp->rv = 0;
    if (SVC_RECV(reqp->xprt, msgp)) {
        incr_counter(&cnt_request_recv);
        reqp->msgp = msgp;
        request_lookup(reqp);
    }

    reqp->xrv = SVC_STAT(reqp->worker_xprt);

    if (!xprt_stat_is_valid(reqp->xrv)) {
        teprintf("Invalid xptr_stat, %d.\n", reqp->xrv);
        svc_die();
    }
    tprintf(2, "SVC_STAT() => %d=%s.\n",
        reqp->xrv, decode_xprt_stat(reqp->xrv));

    mtxprt = xprt_to_mtxprt(reqp->worker_xprt);
    mtxprt->mtxp_stat = reqp->xrv;
    if (reqp->xrv == XPRT_DIED) {
        done = 1;
    }

    if (done) {
        int sock;

        tprintf(2, "XPRT_DIED.\n"
            " worker_xprt=%s\n", decode_addr(reqp->worker_xprt));
        sock = reqp->worker_xprt->xp_sock;
        if (sock != -1) {
            // lock
            // sock_xports[sock] = BAD_SVCXPRT_PTR;
            // unlock
        }
        if (mtmode == 0) {
            SVC_DESTROY(reqp->worker_xprt);
        }
        else {
            xprt_gc_mark(reqp->worker_xprt);
        }
    }
    else if (reqp->rv != 0) {
        tprintf(1, "SVC_RECV: rv=%d.\n", reqp->rv);
    }
}

LIBRARY int
svc_getreq_common_rv(const int fd)
{
    SVCXPRT *xprt;

    xports_global_lock();
    check_xports();
    xports_global_unlock();

    tprintf(2, "Request # %zu\n", cnt_request_recv);

    if (mtmode != 0) {
        (void) xprt_gc_reap_all();
    }

    xprt = sock_xports[fd];

    tprintf(2, "fd=%d, xprt=%s\n", fd, decode_addr(xprt));

    /* Do we control fd? */
    if (xprt == NULL) {
        return (0);
    }

    if (xprt == BAD_SVCXPRT_PTR) {
        return (0);
    }

    check_svcxprt_exists(xprt);

    /* Now receive msgs from xprt (support batch calls) */
    for (;;) {
        req_t req;

        memset((void *)&req, 0, sizeof (req));
        req.fd = fd;
        req.xprt = xprt;

        get_single_request(&req);
        if (req.rv != 0) {
            return (req.rv);
        }

        if (req.xrv != XPRT_MOREREQS) {
            break;
        }
    }

    return (0);
}

PUBLIC void
svc_getreq_common(const int fd)
{
    int err;

    err = svc_getreq_common_rv(fd);
    if (err) {
        svc_die();
    }
}

/*
 * In single-threaded mode, svc_return() waits for the worker thread
 * to indicate that it is done, before proceeding with the next
 * request.  In single-threaded mode, we just ignore the @argument{xprt};
 * there is only one thing to wait on.
 *
 * In true multi-threaded mode, svc_return() tells us when a worker thread
 * is done using a (clone) xprt, and it is safe to destroy it.
 *
 */

PUBLIC void
svc_return(SVCXPRT *xprt)
{
    extern void dbuf_thread_reset(void);
    extern void dbuf_thread_cleanup(void);

    mtxprt_t *mtxprt;
    size_t id;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;

    tprintf(2, "xprt=%s, id=%zu, fd=%d\n",
        decode_addr(xprt), id, xprt->xp_sock);

    incr_counter(&cnt_return);
    xprt_set_busy(xprt, 1);

    switch (mtmode) {
      default:
          teprintf(
              "Invalid value for mtmode, %d.\n"
              "Valid values are 0, 1, 2.\n",
              mtmode);
          svc_die();
          break;
      case 0:
        worker_return = 1;
        break;
      case 1:
      case 2:
        xprt_progress_setbits(xprt, XPRT_RETURN);
        if (mtxprt->mtxp_clone != NULL) {
            if (mtxprt->mtxp_parent == NO_PARENT) {
                teprintf("xprt=%s, id=%zu, is not a clone.\n",
                    decode_addr(xprt), id);
                svc_die();
            }
            xprt_gc_mark(xprt);
        }
        break;
    }
    xprt_set_busy(xprt, 0);
    dbuf_thread_reset();
    dbuf_thread_cleanup();
}

/*
 * If there are no file descriptors available, then accept will fail.
 * We want to delay here so the connection request can be dequeued;
 * otherwise we can bounce between polling and accepting, never giving the
 * request a chance to dequeue and eating an enormous amount of cpu time
 * in svc_run if we're polling on many file descriptors.
 */

void
svc_accept_failed(void)
{
    // 1,000,000 nanoseconds == 1 millisecond
    const long ns_milliseconds = 1000000;

    if (errno == EMFILE) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 50 * ns_milliseconds };
        nanosleep(&ts, NULL);
    }
}

#ifdef _RPC_THREAD_SAFE_

UNUSED void
rpc_thread_svc_cleanup(void)
{
    struct svc_callout *svcp;

    while ((svcp = svc_head) != NULL) {
        svc_unregister(svcp->sc_prog, svcp->sc_vers);
    }
}

#endif /* _RPC_THREAD_SAFE_ */

#define UNUSED_FUNCTION(fname) (void)(fp = &fname);

LIBRARY void
ref_unused(void)
{
    void *fp;

    UNUSED_FUNCTION(rpc_thread_svc_cleanup);
    UNUSED_FUNCTION(xprt_is_locked);
    UNUSED_FUNCTION(svc_backtrace);
}
