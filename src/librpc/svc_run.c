/*
 * Copyright (c) 2010, Oracle America, Inc.
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
 *
 * This is the rpc server side idle loop
 * Wait for input, call server program.
 */

/*
 * This file was derived from libc6/eglibc-2.11.1/sunrpc/svc_run.c.
 * It was written by Guy Shaw in 2011, under contract to Themis Computer,
 * http://www.themis.com.  It inherits the copyright and license
 * from the source code from which it was derived.
 *
 */

#include <errno.h>
#include <unistd.h>
#include <libintl.h>
#include <sys/poll.h>
#include <rpc/rpc.h>

#include "svc_mtxprt.h"
#include "svc_debug.h"

extern void xports_init(void);
extern void xports_free(void);
extern void xports_global_lock(void);
extern void xports_global_unlock(void);
extern void show_xports(void);
extern void show_pollfd(struct pollfd *, nfds_t);
extern void svc_getreq_poll_mt(struct pollfd *, nfds_t, int);
extern int  fd_is_busy(int fd);
extern size_t count_busy(void);
extern void xprt_lock(SVCXPRT *xprt);
extern void xprt_unlock(SVCXPRT *xprt);
extern SVCXPRT *socket_to_xprt(int fd);

extern struct pollfd *xports_pollfd;
extern int xports_max_pollfd;
extern int mtmode;
extern int svc_quit;

pthread_mutex_t poll_lock = PTHREAD_MUTEX_INITIALIZER;

size_t nprocessors = 0;
size_t cnt_rate_limit_waits = 0;

static struct pollfd *pollfdv;

// Poll and poll tracing time values and counters

static int poll_timeout;
static int polls_per_sec;
static int poll_trace_interval;
static int poll_trace_count;
static int poll_countdown;

static void
poll_init(void)
{
    pollfdv = NULL;
    poll_timeout = 10;  /* milliseconds */
    polls_per_sec = 1000 / poll_timeout;
    poll_trace_interval = 5 * polls_per_sec;
    poll_trace_count = 0;
    poll_countdown = poll_trace_interval;
}

/*
 * This function can be used as a signal handler
 * to terminate the server loop.
 */
void
svc_exit(void)
{
    xports_free();
}

void
show_rate_limit_stats(void)
{
    eprintf("Rate limit statistics:\n");
    eprintf("  n processors: %zu\n", nprocessors);
    eprintf("  waits (1 msec each): %zu\n", cnt_rate_limit_waits);
}

static void
rate_limit(void)
{
    size_t prev_nbusy;
    size_t nbusy;
    long sc_nprocessors = -1;

    if (nprocessors == 0) {
        sc_nprocessors = sysconf(_SC_NPROCESSORS_ONLN);

        if (sc_nprocessors < 2) {
            sc_nprocessors = 2;
        }
        nprocessors = (size_t) sc_nprocessors;
    }

    nbusy = count_busy();
    while (nbusy > nprocessors) {
        // 1,000,000 nsec == 1 millisecond
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
        nanosleep(&ts, NULL);
        ++cnt_rate_limit_waits;
        prev_nbusy = nbusy;
        nbusy = count_busy();
        if (nbusy == prev_nbusy) {
            break;
        }
    }
}

/*
 * The array of @type{struct pollfd} is allocated with some granularity.
 * Instead of reallocating the fit the exact number of pollfds we need
 * each time around the main polling loop, we reallocate only when the
 * space allocated before is insufficient, and then we reallocate with
 * a new capacity that is rounded up to the next allocation granularity.
 *
 * This just keeps us from allocating and freeing every time around.
 *
 */
#define FD_ALLOC_GRANULARITY 64

static inline size_t
fd_alloc_nchunks(size_t count)
{
    return ((count + FD_ALLOC_GRANULARITY - 1) / FD_ALLOC_GRANULARITY);
}

static inline size_t
fd_alloc_roundup(size_t count)
{
    return (fd_alloc_nchunks(count) * FD_ALLOC_GRANULARITY);
}

static struct pollfd *
pollfd_realloc(nfds_t nfd)
{
    static struct pollfd *prev_pollfd = NULL;
    static nfds_t prev_pollfd_alloc = 0;

    nfds_t pollfd_alloc;

    pollfd_alloc = (nfds_t) fd_alloc_roundup(nfd);
    if (pollfd_alloc > prev_pollfd_alloc) {
        struct pollfd *new_pollfd;
        size_t sz;

        sz = sizeof (struct pollfd) * pollfd_alloc;
        new_pollfd = (struct pollfd *)guard_realloc(prev_pollfd, sz);
        prev_pollfd = new_pollfd;
        prev_pollfd_alloc = pollfd_alloc;
    }

    return (prev_pollfd);
}

static void
show_pollfds(struct pollfd *pollfd, nfds_t npoll)
{
    nfds_t i;

    show_xports();
    pthread_mutex_lock(&trace_lock);
    trace_printf_with_lock("poll\n");
    eprintf_with_lock("  [\n");
    for (i = 0; i < npoll; ++i) {
        int pe = pollfd[i].events;
        eprintf_with_lock("    {fd=%d, events=%s}\n",
            pollfd[i].fd, decode_poll_events(pe));
    }
    eprintf_with_lock("  ]\n");
    pthread_mutex_unlock(&trace_lock);
}

// Poll all "active" connections - just one time around

void
svc_poll(nfds_t max_pollfd)
{
    nfds_t npoll;
    nfds_t i;
    int poll_rv;
    int err;

    xports_global_lock();
    pollfdv = pollfd_realloc(max_pollfd);
    npoll = 0;
    for (i = 0; i < max_pollfd; ++i) {
        SVCXPRT *xprt;
        mtxprt_t *mtxprt;
        int fd;

        fd = xports_pollfd[i].fd;
        if (fd == -1) {
            continue;
        }

        xprt = socket_to_xprt(fd);
        if (xprt == BAD_SVCXPRT_PTR) {
            continue;
        }

        mtxprt = xprt_to_mtxprt(xprt);

        /*
         * What do we do if this SVCXPORT has returned?
         */

        if (mtmode == 0 && (mtxprt->mtxp_progress & XPRT_RETURN) != 0) {
            /*
             * In single-threaded mode, we do not create clones
             * of SVCXPORT data structures, because we do not need
             * to keep transport data for multiple threads.
             * So, if we see that the SVCXPORT has returned,
             * we just reuse it.
             */
            mtxprt->mtxp_progress = 0;
            mtxprt->mtxp_busy = 0;
        }

#if 0
        if (mtmode != 0) {
            if ((mtxprt->mtxp_progress & XPRT_RETURN) != 0) {
                continue;
            }
        }
#endif

        /*
         * We do not poll all fds associated with all xprt structures,
         * because some xprts and their associated fds are busy,
         * for example, with some ongoing data transport over TCP.
         * For UDP, not so much.
         */
        if (mtmode == 0 || mtxprt->mtxp_busy == 0) {
            pollfdv[npoll].fd = fd;
            pollfdv[npoll].events = xports_pollfd[i].events;
            pollfdv[npoll].revents = 0;
            ++npoll;
        }
    }
    xports_global_unlock();

    if (npoll == 0) {
        teprintf("npoll == 0\n");
    }

    if (opt_svc_trace && (poll_trace_count == 0 || poll_countdown <= 0)) {
        show_pollfds(pollfdv, npoll);
        ++poll_trace_count;
        poll_countdown = poll_trace_interval;
    }

    poll_rv = poll(pollfdv, npoll, poll_timeout);
    err = errno;
    --poll_countdown;
    switch (poll_rv) {
    case -1:
        if (err == EINTR) {
            break;
        }
        svc_perror(err, "svc_run: - poll() failed");
        poll_trace_count = 0;
        poll_countdown = poll_trace_interval;
        break;
    case 0:
        break;
    default:
        pthread_mutex_unlock(&poll_lock);
        svc_getreq_poll_mt(pollfdv, npoll, poll_rv);
        poll_trace_count = 0;
        poll_countdown = poll_trace_interval;
        break;
    }
}

void
svc_run_cleanup(void)
{
    if (pollfdv != NULL) {
        free(pollfdv);
    }
}

/*
 * Main loop.  Keep polling "active" connections
 */

void
svc_run(void)
{
    extern int xprt_gc_reap_all(void);

    nfds_t max_pollfd;

    poll_init();
    xports_init();
    while (svc_quit == 0) {
        max_pollfd = xports_max_pollfd;
        if (max_pollfd == 0 && xports_pollfd == NULL) {
            break;
        }

        (void) xprt_gc_reap_all();
        rate_limit();

        pthread_mutex_lock(&poll_lock);
        svc_poll(max_pollfd);
        pthread_mutex_unlock(&poll_lock);
    }

    svc_run_cleanup();
}
