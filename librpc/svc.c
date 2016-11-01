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
 * This file was derived from libc6/eglibc-2.11.1/sunrpc/svc.c.
 * It was written by Guy Shaw in 2011, under contract to Themis Computer,
 * http://www.themis.com.  It inherits the copyright and license
 * from the source code from which it was derived.
 *
 */

#include <errno.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#include <poll.h>
#include <pthread.h>

#include "svc_mtxprt.h"
#include "svc_debug.h"
#include "util.h"

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

pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;

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

#endif

static fd_set xports_fdset;
static fd_set xprtgc_fdset;
static int xports_size;
static int xports_count;
static int xports_view_count;
static unsigned long long xports_version;
static int xprtgc_mark_count;

struct pollfd *xports_pollfd;
nfds_t xports_pollfd_size;
nfds_t xports_max_pollfd;

pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * @var{xports_lock} protects all operations that modify
 * @var{xports} and/or @var{sock_xports}.
 *
 * Each @type{SVCXPRT} contains a lock to protects its contents,
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

/*
 * 0: single-threaded -- do not clone SVCXPRT; wait for @var{worker_return}
 * 1: multi-threaded  -- clone SVCXPRT for each request; do not wait
 *
 * There is no interface for changing the mode.
 * It is for purposes of debugging and regression testing.
 */
int mtmode = 1;

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

int failfast = 1;

int worker_return;

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

void
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

void
xports_global_lock(void)
{
    pthread_mutex_lock(&xports_lock);
    xports_owner = pthread_self();
}

void
xports_global_unlock(void)
{
    pthread_mutex_unlock(&xports_lock);
}

void
xports_snapshot()
{
    pthread_mutex_lock(&xports_view_lock);
    memcpy(xports_view, xports, xports_size * sizeof (SVCXPRT *));
    pthread_mutex_unlock(&xports_view_lock);
}

void
xports_update_view(int id)
{
    pthread_mutex_lock(&xports_view_lock);
    xports_view[id] = xports[id];
    xports_view_count = xports_count;
    pthread_mutex_unlock(&xports_view_lock);
}

void
xprt_gc_mark(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    int id;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    pthread_mutex_lock(&xprtgc_lock);
    tprintf("xprt=%s, id=%d\n", decode_addr(xprt), id);
    FD_SET(id, &xprtgc_fdset);
    ++xprtgc_mark_count;
    pthread_mutex_unlock(&xprtgc_lock);
}

static int
xprt_gc_reap_one(int id)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    int count = 0;

    if (!FD_ISSET(id, &xprtgc_fdset)) {
        return (0);
    }

    pthread_mutex_lock(&xprtgc_lock);
    if (FD_ISSET(id, &xprtgc_fdset)) {
        xprt = xports[id];
        mtxprt = xprt_to_mtxprt(xprt);
        if (mtxprt->mtxp_parent != NO_PARENT || mtxprt->mtxp_refcnt == 0) {
            tprintf("xprt=%s\n", decode_addr(xprt));
            SVC_DESTROY(xprt);
            count = 1;
        }
        FD_CLR(id, &xprtgc_fdset);
        --xprtgc_mark_count;
    }
    pthread_mutex_unlock(&xprtgc_lock);

    return (count);
}

/*
 * Do actual destruction of @type{SVCXPRT}s that have been used,
 * and then marked for garbage collection.
 */

static int
xprt_gc_reap_all(void)
{
    int id;
    int count;

    tprintf("%d SVCXPRT to be destroyed\n", xprtgc_mark_count);
    count = 0;
    for (id = 0; id < xports_size && xprtgc_mark_count != 0; ++id) {
        if (FD_ISSET(id, &xprtgc_fdset)) {
            count += xprt_gc_reap_one(id);
        }
    }
    return (count);
}

/*
 * Allocate memory for a @type{SVCXPRT}.  Allocate enough contiguous memory
 * for a "standard" SVCXPRT followed by the @type{mtxprt} extension.
 */
SVCXPRT *
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
mtxprt_t *
xprt_to_mtxprt_nocheck(SVCXPRT *xprt)
{
    return ((mtxprt_t *)((char *)xprt + sizeof (SVCXPRT)));
}

/*
 * Given the address of a standard @type{SVCXPRT}, return a pointer to its
 * @type{mtxprt}.  Check that the given pointer points to a properly
 * constructed @type{SVCXPRT}, including the @type{mtxprt}.
 *
 */
mtxprt_t *
xprt_to_mtxprt(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;

    mtxprt = (mtxprt_t *)((char *)xprt + sizeof (SVCXPRT));
    if (mtxprt->mtxp_magic != MTXPRT_MAGIC) {
        tprintf("xprt=%s -- Bad magic, %x.\n",
            decode_addr(xprt), mtxprt->mtxp_magic);
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

void
xprt_lock(SVCXPRT *xprt)
{
    mtxprt_t        *mtxprt;
    pthread_mutex_t *lockp;
    int ret;

    mtxprt = xprt_to_mtxprt(xprt);
    lockp = &mtxprt->mtxp_lock;
    tprintf("xprt=%s, xprt_id=%d\n",
        decode_addr(xprt), mtxprt->mtxp_id);
    ret = pthread_mutex_lock(lockp);
    if (ret != 0) {
        svc_die();
    }
}

void
xprt_unlock(SVCXPRT *xprt)
{
    mtxprt_t        *mtxprt;
    pthread_mutex_t *lockp;
    int ret;

    mtxprt = xprt_to_mtxprt(xprt);
    lockp = &mtxprt->mtxp_lock;
    tprintf("xprt=%s, xprt_id=%d\n",
        decode_addr(xprt), mtxprt->mtxp_id);
    ret = pthread_mutex_unlock(lockp);
    if (ret != 0) {
        svc_die();
    }
}

int
xprt_is_locked(SVCXPRT *xprt)
{
    mtxprt_t        *mtxprt;

    mtxprt = xprt_to_mtxprt(xprt);
    return (pthread_mutex_is_locked(&(mtxprt->mtxp_lock)));
}

void
xprt_set_busy(SVCXPRT *xprt, int value)
{
    mtxprt_t *mtxprt;

    mtxprt = xprt_to_mtxprt(xprt);
    pthread_mutex_lock(&(mtxprt->mtxp_progress_lock));
    mtxprt->mtxp_busy = value;
    pthread_mutex_unlock(&(mtxprt->mtxp_progress_lock));
}

int
xprt_is_busy(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    int busy;

    if (xprt == BAD_SVCXPRT_PTR) {
        return (0);
    }

    mtxprt = xprt_to_mtxprt(xprt);
    pthread_mutex_lock(&(mtxprt->mtxp_progress_lock));
    busy = mtxprt->mtxp_busy;
    pthread_mutex_unlock(&(mtxprt->mtxp_progress_lock));
    return (busy);
}

int
fd_is_busy(int fd)
{
    return (xprt_is_busy(sock_xports[fd]));
}

/*
 * Get the current value of the progress field for a given SVCXPRT.
 * Always use this method to get the value.  Never read the value directly.
 */

int
xprt_get_progress(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    int progress;

    mtxprt = xprt_to_mtxprt(xprt);
    pthread_mutex_lock(&(mtxprt->mtxp_progress_lock));
    progress = mtxprt->mtxp_progress;
    pthread_mutex_unlock(&(mtxprt->mtxp_progress_lock));
    return (progress);
}

/*
 * Return new value of progress.
 */

int
xprt_progress_setbits(SVCXPRT *xprt, int value)
{
    mtxprt_t *mtxprt;
    int progress;

    mtxprt = xprt_to_mtxprt(xprt);
    pthread_mutex_lock(&(mtxprt->mtxp_progress_lock));
    progress = mtxprt->mtxp_progress | value;
    mtxprt->mtxp_progress = progress;
    pthread_mutex_unlock(&(mtxprt->mtxp_progress_lock));
    return (progress);
}

int
xprt_progress_clrbits(SVCXPRT *xprt, int value)
{
    mtxprt_t *mtxprt;
    int progress;

    mtxprt = xprt_to_mtxprt(xprt);
    pthread_mutex_lock(&(mtxprt->mtxp_progress_lock));
    progress = mtxprt->mtxp_progress & ~value;
    mtxprt->mtxp_progress = progress;
    pthread_mutex_unlock(&(mtxprt->mtxp_progress_lock));
    return (progress);
}

void
show_xportv(SVCXPRT **xprtv, int size)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    int count;
    int id;

    if (xprtv == NULL) {
        eprintf("\nxprtv == NULL\n");
        return;
    }

    if (size == 0) {
        eprintf("\nsize == 0\n");
        return;
    }

    count = 0;
    eprintf("\nxports[]:\n");
    eprintf("   id    addr    prnt rcnt sock  port\n");
    eprintf("----- ---------- ---- ---- ---- -----\n");
    //       12345 1234567890 1234 1234 1234 12345
    for (id = 0; id < size; ++id) {
        xprt = xprtv[id];
        if (xprt != BAD_SVCXPRT_PTR) {
            if (xprt == (SVCXPRT *)0) {
                eprintf("%5u NULL\n", id);
            }
            else {
                mtxprt = xprt_to_mtxprt_nocheck(xprt);
                eprintf("%5u %10s %4d %4d %4d %5d\n",
                    id, decode_addr(xprt), mtxprt->mtxp_parent,
                    mtxprt->mtxp_refcnt, xprt->xp_sock, xprt->xp_port);
                ++count;
                if (count >= size) {
                    break;
                }
            }
        }
    }
    eprintf("\n");
}

void
show_xports(void)
{
    pthread_mutex_lock(&xports_view_lock);
    show_xportv(xports_view, xports_view_count);
    pthread_mutex_unlock(&xports_view_lock);
}

void
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

void
show_xports_fdset(void)
{
    int fd;
    int count;

    eprintf("\n");
    eprintf("xports_fdset:\n");
    count = 0;
    for (fd = 0; fd < xports_size; ++fd) {
        if (FD_ISSET(fd, &xports_fdset)) {
            eprintf("%s%u", count ? "," : "  ", fd);
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

int
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
void
check_svcxprt(SVCXPRT *xprt)
{
    if (!is_valid_svcxprt(xprt)) {
        show_xports();
        show_xports_pollfd();
        show_xports_fdset();
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
void
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
        show_xports_fdset();
        svc_die();
    }
}

int
check_xports_duplicates(void)
{
    SVCXPRT *xprt1, *xprt2;
    int id1, id2;
    int err;

    /*
     * Check for duplicate pointers in the set of all SVCXPRTs.
     */
    err = 0;
    for (id1 = 0; id1 < xports_size; ++id1) {
        xprt1 = xports[id1];
        if (xprt1 != BAD_SVCXPRT_PTR) {
            for (id2 = id1 + 1; id2 < xports_size; ++id2) {
                xprt2 = xports[id2];
                if (xprt2 != BAD_SVCXPRT_PTR) {
                    if (xprt2 == xprt1) {
                        eprintf("xports[%d] == xports[%d]\n", id2, id1);
                        err = 1;
                    }
                    else if (xprt2->xp_pad == xprt1->xp_pad) {
                        eprintf("xports[%d]->xp_pad == xports[%d]->xp_pad\n",
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
 *     xports, sock_xports, xports_fdset.
 *
 * Sort of like @command{fsck}.
 *
 */

int
check_xports(void)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    int id;
    int err;

    if (xports == NULL) {
        return (1);
    }

    assert(pthread_mutex_is_locked(&xports_lock));
    err = check_xports_duplicates();

    for (id = 0; id < xports_size; ++id) {
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

    for (id = 0; id < xports_size; ++id) {
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
            if (!FD_ISSET(id, &xports_fdset)) {
                eprintf("id=%d not in xports_fdset.\n", id);
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

    for (id = 0; id < xports_size; ++id) {
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
            eprintf("id=%d -- expect ref count=%d, got %d.\n",
                id, mtxprt->mtxp_refcnt, mtxprt->mtxp_fsck_refcnt);
            err = 1;
        }
    }

    return (err == 0);
}


/* ***************  SVCXPRT related stuff **************** */

/*
 * Initialize @var{exports} to a known pattern,
 * for debugging purposes.
 */
static void
init_xports(SVCXPRT **xportv, int size)
{
    int id;

    for (id = 0; id < size; ++id) {
        xportv[id] = BAD_SVCXPRT_PTR;
    }
}

static void
create_xports(void)
{
    size_t size;

    size = (size_t)_rpc_dtablesize();
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
    xports_version = 0;
    xports_count = 0;
    xports_view_count = 0;
    xports_pollfd = (struct pollfd *) guard_malloc(size * sizeof (struct pollfd));
    xports_pollfd_size = size;
    xports_max_pollfd = 0;
}

void
xports_init(void)
{
    if (xports == NULL) {
        create_xports();
    }
}

void
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

int
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
static int
xprt_id_alloc(void)
{
    int id;

    assert(pthread_mutex_is_locked(&xports_lock));
    for (id = 0; id < xports_size; ++id) {
        if (!FD_ISSET(id, &xports_fdset)) {
            FD_SET(id, &xports_fdset);
            ++xports_count;
            break;
        }
    }

    if (id >= xports_size) {
        /*
         * If this becomes a problem, and we really want to have
         * more outstanding SVCXPRT clones at one time, then switch
         * to an allocator than can handle ID numbers bigger than
         * @var{xports_size}.  vmem(), perhaps.
         */
        teprintf("Ran out of xprt IDs.  xports_size=%d\n", xports_size);
        svc_die();
        return (-1);
    }
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

    sfr->sfr_timestamp = 0;	// Not valid
    sock_xports[sock] = xprt;
    sfr->sfr_psr = sched_getcpu();
    sfr->sfr_tid = pthread_self();

    // Record timestamp, and at the same time, mark sfr record as valid
    sfr->sfr_timestamp = rdtsc() - t0;
}

#else

static inline void
sfr_track_xprt_socket(int sock, SVCXPRT *xprt)
{
    sock_xports[sock] = xprt;
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
    if (mtxprt->mtxp_progress & XPRT_DONE_RETURN) {
        return (1);
    }

    return (0);
}

/*
 * Add a @type{SVCXPRT} data structure to @var{xports} and @var{sock_xports}.
 * This operation needs to be protected by @var{xports_lock}.
 *
 */

int
xprt_register_with_lock(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    int xprt_id;
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

    if (xprt_id == -1) {
        xprt_id = xprt_id_alloc();
        mtxprt->mtxp_id = xprt_id;
    }

    if (opt_svc_trace) {
        show_xports();
    }

    tprintf("xprt=%s, xprt_id=%d, sock=%d, parent=%d\n",
        decode_addr(xprt), xprt_id, sock, mtxprt->mtxp_parent);

    if (xprt_id >= xports_size) {
        tprintf("xprt_id >= xports_size (%d)\n", xports_size);
        svc_die();
    }

    xports[xprt_id] = xprt;
    xports_update_view(xprt_id);
    ++xports_version;

    err = 0;
    if (mtxprt->mtxp_parent == NO_PARENT) {
        SVCXPRT *sxprt;

        sxprt = sock_xports[sock];
        if (!socket_xprt_is_available(sxprt)) {
            teprintf("sock_xports[sock]=%s -- should be vacant.\n",
                decode_addr(sxprt));
            svc_die();
        }

        sfr_track_xprt_socket(sock, xprt);
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
void
xprt_register(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    int id;
    int err;

    check_svcxprt(xprt);
    pthread_mutex_lock(&io_lock);
    xports_global_lock();
    err = xprt_register_with_lock(xprt);
    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    xports_update_view(id);
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

void
unregister_id(int id)
{
    if (!(id >= 0 && id < xports_size)) {
        tprintf("Bad id: %d\n", id);
        svc_die();
    }

    assert(pthread_mutex_is_locked(&xports_lock));

    xports[id] = BAD_SVCXPRT_PTR;
    xports_update_view(id);
    ++xports_version;

    tprintf("free id=%d\n", id);
    FD_CLR(id, &xports_fdset);
    --xports_count;
}

/*
 * De-activate a transport handle.
 */

int
xprt_unregister_with_lock(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    int id;

    check_svcxprt_exists(xprt);
    mtxprt = xprt_to_mtxprt(xprt);

    id = mtxprt->mtxp_id;

    if (!(id >= 0 && id < xports_size)) {
        tprintf("Bad id: %d\n", id);
        svc_die();
    }

    if (xports[id] != xprt) {
        tprintf("xports[%d] != xprt(%s)\n", id, decode_addr(xprt));
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

void
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

    s = svc_find (prog, vers, &prev);
    return (s != NULL_SVC && s->sc_mapped);
}


/*
 * Add a service program to the callout list.
 *
 * The dispatch routine will be called when a rpc request
 * for this program number comes in.
 */
bool_t
svc_register(SVCXPRT *xprt, rpcprog_t prog, rpcvers_t vers, void (*dispatch) (struct svc_req *, SVCXPRT *), rpcproc_t protocol)
{
    struct svc_callout *prev;
    struct svc_callout *s;

    check_svcxprt_exists(xprt);

    if ((s = svc_find(prog, vers, &prev)) != NULL_SVC) {
        if (s->sc_dispatch == dispatch)
            goto pmap_it;       /* he is registering another xptr */
        return (FALSE);
    }
    s = (struct svc_callout *)guard_malloc(sizeof (struct svc_callout));

    s->sc_prog = prog;
    s->sc_vers = vers;
    s->sc_dispatch = dispatch;
    s->sc_next = svc_head;
    s->sc_mapped = FALSE;
    svc_head = s;

  pmap_it:
    /* now register the information with the local binder service */
    if (protocol) {
        if (!pmap_set(prog, vers, protocol, xprt->xp_port))
            return (FALSE);

        s->sc_mapped = TRUE;
    }

    return (TRUE);
}

/*
 * Remove a service program from the callout list.
 */
void
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
bool_t
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
void
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
void
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
void
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
void
svcerr_auth(SVCXPRT *xprt, enum auth_stat why)
{
    struct rpc_msg rply;

    tprintf("xprt=%s\n", decode_addr(xprt));
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
void
svcerr_weakauth(SVCXPRT *xprt)
{
    check_svcxprt_exists(xprt);
    svcerr_auth(xprt, AUTH_TOOWEAK);
}

/*
 * Program unavailable error reply
 */
void
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
void
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

void
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

void
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
            tprintf("sock=%d\n", sock);
            svc_getreq_common(sock);
        }
    }
}

void
svc_getreq_poll_mt(struct pollfd *pfdp, nfds_t npoll, int pollretval)
{
    int fds_found;
    nfds_t i;

    if (pollretval == 0)
        return;

    tprintf("npoll=%ld, pollretval=%d\n", npoll, pollretval);

    fds_found = 0;
    for (i = 0; i < npoll; ++i) {
        struct pollfd *p;

        p = &pfdp[i];
        if (p->fd != -1 && p->revents) {
            ++fds_found;
        }
    }

    if (fds_found < pollretval) {
        teprintf("fds_found=%d, pollretval=%d\n", fds_found, pollretval);
        pollretval = fds_found;
    }

    fds_found = 0;
    for (i = 0; i < npoll; ++i) {
        struct pollfd *p;

        p = &pfdp[i];
        if (p->fd != -1 && p->revents) {
            /* fd has input waiting */
            if (p->revents & POLLNVAL) {
                tprintf("POLLNVAL: p->fd=%d, fds_found=%d\n",
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

/*
 * Wait for a given SVCXPRT to get to a certain milestone.
 * Wait for a millisecond at a time.
 * If tracing is turned on, update waiting message every
 * 5 seconds.
 */

void
wait_on_progress(SVCXPRT *xprt, int mask, const char *desc)
{
    const struct timespec ms = { 0, 1000000 };
    mtxprt_t *mtxprt;
    int id;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    for (;;) {
        int i;

        for (i = 0; i < 5000; ++i) {
            if ((xprt_get_progress(xprt) & mask) != 0) {
                return;
            }
            nanosleep(&ms, NULL);
        }
        tprintf("xprt=%s, id=%d, waiting on %s\n", decode_addr(xprt), id, desc);
    }
}

int
svc_getreq_common_rv(const int fd)
{
    enum xprt_stat stat;
    struct rpc_msg *msgp;
    SVCXPRT *xprt;
    SVCXPRT *worker_xprt;
    mtxprt_t *mtxprt;

    xports_global_lock();
    check_xports();
    xports_global_unlock();

    (void)xprt_gc_reap_all();

    xprt = sock_xports[fd];

    {
        tprintf("fd=%d, xprt=%s\n", fd, decode_addr(xprt));
    }

    /* Do we control fd? */
    if (xprt == NULL) {
        return (0);
    }

    if (xprt == BAD_SVCXPRT_PTR) {
        return (0);
    }

    check_svcxprt_exists(xprt);

    /* Now receive msgs from xprt (support batch calls) */
    do {
        (void)xprt_gc_reap_all();
        mtxprt = xprt_to_mtxprt(xprt);
        msgp = &mtxprt->mtxp_msg;
        msgp->rm_call.cb_cred.oa_base = &(mtxprt->mtxp_cred[0]);
        msgp->rm_call.cb_verf.oa_base = &(mtxprt->mtxp_cred[MAX_AUTH_BYTES]);

        // In case we fail before xprt is cloned.
        worker_xprt = xprt;

        if (SVC_RECV(xprt, msgp)) {
            /* now find the exported program and call it */
            struct svc_callout *s;
            struct svc_req *rqstp;
            enum auth_stat why;
            rpcvers_t low_vers;
            rpcvers_t high_vers;
            int prog_found;
            int err;

            err = 0;
            rqstp = &(mtxprt->mtxp_rqst);
            rqstp->rq_clntcred = &(mtxprt->mtxp_cred[2 * MAX_AUTH_BYTES]);
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
            } else if ((why = _authenticate(rqstp, msgp)) != AUTH_OK) {
                tprintf("\n");
                svcerr_auth(xprt, why);
                err = 1;
            }

            if (err) {
                goto call_done;
            }

            /* now match message with a registered service */
            prog_found = FALSE;
            low_vers = 0 - 1;
            high_vers = 0;

            for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
                if (s->sc_prog == rqstp->rq_prog) {
                    if (s->sc_vers == rqstp->rq_vers) {
                        mtxprt_t *mtxprt;
                        struct svc_req *xprt_rqstp;

                        if (mtmode) {
                            mtxprt = xprt_to_mtxprt(xprt);
                            if (mtxprt->mtxp_clone != NULL) {
                                worker_xprt = svc_xprt_clone(xprt);
                            }
                            else {
                                worker_xprt = xprt;
                            }
                        }
                        else {
                            worker_xprt = xprt;
                            worker_return = 0;
                        }
                        mtxprt = xprt_to_mtxprt(worker_xprt);
                        xprt_rqstp = &(mtxprt->mtxp_rqst);
                        if (mtxprt->mtxp_parent == NO_PARENT) {
                            xprt_set_busy(worker_xprt, 1);
                        }
                        pthread_mutex_lock(&(mtxprt->mtxp_progress_lock));
                        if (mtxprt->mtxp_progress & XPRT_DONE_RETURN) {
                            mtxprt->mtxp_progress = 0;
                        }
			pthread_mutex_unlock(&(mtxprt->mtxp_progress_lock));
                        tprintf("> dispatch\n");
                        (*s->sc_dispatch)(xprt_rqstp, worker_xprt);
                        tprintf("< dispatch\n");
                        if (mtmode) {
                            wait_on_progress(worker_xprt, XPRT_DONE_GETARGS, "XPRT_DONE_GETARGS");
                        }
                        else {
                            while (worker_return == 0) {
                                const struct timespec ms = { 0, 1000000 };
                                nanosleep(&ms, NULL);
                            }
                        }
                        goto call_done;
                    }
                    /* found correct version */
                    prog_found = TRUE;
                    if (s->sc_vers < low_vers)
                        low_vers = s->sc_vers;
                    if (s->sc_vers > high_vers)
                        high_vers = s->sc_vers;
                }
                /* found correct program */
            }
            /* if we got here, the program or version
               is not served ... */
            if (prog_found) {
                tprintf("svcerr_progvers()\n");
                svcerr_progvers(worker_xprt, low_vers, high_vers);
            }
            else {
                tprintf("svcerr_noprog()\n");
                svcerr_noprog(worker_xprt);
            }
            /* Fall through to ... */
        }
        tprintf("fall through to @label{call_done}\n");
      call_done:
        tprintf("call_done:\n");
        if ((stat = SVC_STAT(worker_xprt)) == XPRT_DIED) {
            int sock;

            tprintf("XPRT_DIED.\n"
                " worker_xprt=%s\n", decode_addr(worker_xprt));
            sock = worker_xprt->xp_sock;
            if (sock != -1) {
                // lock
                // sock_xports[sock] = BAD_SVCXPRT_PTR;
                // unlock
            }
            xprt_gc_mark(worker_xprt);
            break;
        }
    }
    while (stat == XPRT_MOREREQS);

    return (0);
}

void
svc_getreq_common(const int fd)
{
    int err;

    err = svc_getreq_common_rv(fd);
    if (err) {
        svc_die();
    }
}

/*
 * In single-threaded mode, svc_getreq() waits for the worker thread
 * to indicate that it is done, before proceeding with the next
 * request.  In single-threaded mode, we just ignore the @argument{xprt};
 * there is only one thing to wait on.
 *
 * In true multi-threaded mode, svc_return() tells us when a worker thread
 * is done using a (clone) xprt, and it is safe to destroy it.
 *
 */

void
svc_return(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;

    mtxprt = xprt_to_mtxprt(xprt);
    if (mtmode) {
        if (mtxprt->mtxp_clone != NULL) {
            if (mtxprt->mtxp_parent == NO_PARENT) {
                teprintf("xprt=%s is not a clone.\n", decode_addr(xprt));
                svc_die();
            }
            xprt_gc_mark(xprt);
        }
    }
    else {
        worker_return = 1;
    }
    xprt_set_busy(xprt, 0);
    xprt_progress_setbits(xprt, XPRT_DONE_RETURN);
}

#ifdef _RPC_THREAD_SAFE_

void
__rpc_thread_svc_cleanup(void)
{
    struct svc_callout *svcp;

    while ((svcp = svc_head) != NULL) {
        svc_unregister(svcp->sc_prog, svcp->sc_vers);
    }
}

#endif /* _RPC_THREAD_SAFE_ */
