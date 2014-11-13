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

#include "svc_debug.h"

extern void xports_init(void);
extern void xports_free(void);
extern void xports_global_lock(void);
extern void xports_global_unlock(void);
extern void show_xports(void);
extern void show_pollfd(struct pollfd *, nfds_t);
extern void svc_getreq_poll_mt(struct pollfd *, nfds_t, int);
extern int  fd_is_busy(int fd);

extern struct pollfd *xports_pollfd;
extern int xports_max_pollfd;

/*
 * This function can be used as a signal handler
 * to terminate the server loop.
 */
void
svc_exit(void)
{
    xports_free();
}


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

        sz = sizeof(struct pollfd) * pollfd_alloc;
        new_pollfd = guard_realloc(prev_pollfd, sz);
        prev_pollfd = new_pollfd;
        prev_pollfd_alloc = pollfd_alloc;
    }

    return (prev_pollfd);
}

void
svc_run(void)
{
    struct pollfd *my_pollfd = NULL;
    int max_pollfd;
    nfds_t npoll;
    nfds_t i;
    int err;
    int poll_timeout;
    int polls_per_sec;
    int poll_trace_interval;
    int poll_trace_count;
    int poll_countdown;

    poll_timeout = 10;  /* milliseconds */
    polls_per_sec = 1000 / poll_timeout;
    poll_trace_interval = 5 * polls_per_sec;
    poll_trace_count = 0;
    poll_countdown = poll_trace_interval;
    xports_init();
    for (;;) {
        max_pollfd = xports_max_pollfd;
        if (max_pollfd == 0 && xports_pollfd == NULL)
            break;

        xports_global_lock();
        my_pollfd = pollfd_realloc(max_pollfd);

        npoll = 0;
        for (i = 0; i < max_pollfd; ++i) {
            int fd;

            fd = xports_pollfd[i].fd;
            /*
             * We do not poll all fds associated with all xprt structures,
             * because some xprts and their associated fds are busy,
             * for example, with some ongoing data transport over TCP.
             * For UDP, not so much.
             */
            if (fd != -1 && !fd_is_busy(fd)) {
                my_pollfd[npoll].fd = fd;
                my_pollfd[npoll].events = xports_pollfd[i].events;
                my_pollfd[npoll].revents = 0;
                ++npoll;
            }
        }
        xports_global_unlock();

        if (npoll == 0) {
            teprintf("npoll == 0\n");
        }

        if (opt_svc_trace && (poll_trace_count == 0 || poll_countdown <= 0)) {
            show_xports();
            pthread_mutex_lock(&trace_lock);
            trace_printf_with_lock("poll\n");
            eprintf_with_lock("  [\n");
            for (i = 0; i < npoll; ++i) {
                eprintf_with_lock("    {fd=%d, events=%s}\n",
                    my_pollfd[i].fd, decode_poll_events(my_pollfd[i].events));
            }
            eprintf_with_lock("  ]\n");
            pthread_mutex_unlock(&trace_lock);
            ++poll_trace_count;
            poll_countdown = poll_trace_interval;
        }

        i = poll(my_pollfd, npoll, poll_timeout);
        err = errno;
        --poll_countdown;
        switch (i) {
        case -1:
            if (err == EINTR)
                continue;
            svc_perror(err, "svc_run: - poll() failed");
            poll_trace_count = 0;
            poll_countdown = poll_trace_interval;
            break;
        case 0:
            continue;
        default:
            svc_getreq_poll_mt(my_pollfd, npoll, i);
            poll_trace_count = 0;
            poll_countdown = poll_trace_interval;
            continue;
        }
        break;	// Keep lint happy
    }

    free(my_pollfd);
}
