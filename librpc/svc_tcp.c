/*
 * svc_tcp.c, Server side for TCP/IP based RPC.
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
 *
 * Actually implements two flavors of transporter -
 * a tcp rendezvouser (a listener and connection establisher)
 * and a record/tcp stream.
 */

/*
 * This file was derived from libc6/eglibc-2.11.1/sunrpc/svc.c.
 * It was written by Guy Shaw in 2011, under contract to Themis Computer,
 * http://www.themis.com.  It inherits the copyright and license
 * from the source code from which it was derived.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <libintl.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef USE_IN_LIBIO
# include <wchar.h>
# include <libio/iolibio.h>
#endif

#include "svc_mtxprt.h"
#include "svc_debug.h"

#define UNUSED(x) (void)(x)

extern void xports_global_lock(void);
extern void xports_global_unlock(void);
extern SVCXPRT *alloc_xprt(void);
extern void xprt_lock(SVCXPRT *);
extern void xprt_unlock(SVCXPRT *);
extern int  xprt_get_progress(SVCXPRT *);
extern int  xprt_progress_setbits(SVCXPRT *, int);
extern int  xprt_progress_clrbits(SVCXPRT *, int);
extern void xprt_set_busy(SVCXPRT *, int);
extern int  xprt_is_busy(SVCXPRT *);

extern void svc_perror(int, const char *);

extern int failfast;

extern pthread_mutex_t io_lock;

static inline void
xdr_enter(void)
{
}

static inline void
xdr_exit(void)
{
}

/*
 * Ops vector for TCP/IP based rpc service handle
 */
static bool_t svctcp_recv(SVCXPRT *, struct rpc_msg *);
static enum xprt_stat svctcp_stat(SVCXPRT *);
static bool_t svctcp_getargs(SVCXPRT *, xdrproc_t, caddr_t);
static bool_t svctcp_reply(SVCXPRT *, struct rpc_msg *);
static bool_t svctcp_freeargs(SVCXPRT *, xdrproc_t, caddr_t);
static void svctcp_destroy(SVCXPRT *);

static const xp_ops_t svctcp_op = {
    svctcp_recv,
    svctcp_stat,
    svctcp_getargs,
    svctcp_reply,
    svctcp_freeargs,
    svctcp_destroy
};

/*
 * Ops vector for TCP/IP rendezvous handler
 */
static bool_t rendezvous_request(SVCXPRT *, struct rpc_msg *);
static enum xprt_stat rendezvous_stat(SVCXPRT *);
static void svctcp_rendezvous_abort(void) __attribute__ ((__noreturn__));

/*
 * This function makes sure abort() relocation goes through PLT
 * and thus can be lazy bound.
 */
static void
svctcp_rendezvous_abort(void)
{
    abort();
}

static const xp_ops_t svctcp_rendezvous_op = {
    rendezvous_request,
    rendezvous_stat,
    (bool_t (*) (SVCXPRT *, xdrproc_t, caddr_t)) svctcp_rendezvous_abort,
    (bool_t (*) (SVCXPRT *, struct rpc_msg *)) svctcp_rendezvous_abort,
    (bool_t (*) (SVCXPRT *, xdrproc_t, caddr_t)) svctcp_rendezvous_abort,
    svctcp_destroy
};

/*
 * The functions, readtcp() and writetcp() should return a size_t.
 * But, these functions are used by other modules, indirectly through
 * function pointers, so we keep the int length and return value.
 * They are declared static, but they are exported to other modules.
 */
static int readtcp(char *, char *, int);
static int writetcp(char *, char *, int);
static SVCXPRT *makefd_xprt(int, u_int, u_int);

/* kept in xprt->xp_p1 */
struct tcp_rendezvous {
    u_int sendsize;
    u_int recvsize;
};

/* kept in xprt->xp_p1 */
struct tcp_conn {
    enum xprt_stat strm_stat;
    u_long x_id;
    XDR xdrs;
    char verf_body[MAX_AUTH_BYTES];
};

/*
 * Usage:
 *      xprt = svctcp_create(sock, send_buf_size, recv_buf_size);
 *
 * Creates, registers, and returns a (rpc) tcp based transporter.
 * Once *xprt is initialized, it is registered as a transporter
 * see (svc.h, xprt_register).  This routine returns
 * a NULL if a problem occurred.
 *
 * If sock<0 then a socket is created, else sock is used.
 * If the socket, @var{sock}, is not bound to a port then svctcp_create
 * binds it to an arbitrary port.  The routine then starts a tcp
 * listener on the socket's associated port.  In any (successful) case,
 * xprt->xp_sock is the registered socket number and xprt->xp_port is the
 * associated port number.
 *
 * Since tcp streams do buffered io similar to stdio, the caller can specify
 * how big the send and receive buffers are via the second and third parms;
 * 0 => use the system default.
 */
SVCXPRT *
svctcp_create_with_lock(int sock, u_int sendsize, u_int recvsize)
{
    bool_t madesock;
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    struct tcp_rendezvous *r;
    struct sockaddr_in addr;
    socklen_t len;
    int ret;
    int err;

#ifdef DEBUG_BUFSIZE_8K
    if (sendsize == 0) {
        sendsize = 8192;        // Magic number.  For debugging only.
    }
    if (recvsize == 0) {
        recvsize = 8192;        // Magic number.  For debugging only.
    }
#endif /* DEBUG_BUFSIZE_8K */

    tprintf("sock=%d, sendsize=%u, recvsize=%u\n", sock, sendsize, recvsize);
    madesock = FALSE;
    len = sizeof(struct sockaddr_in);

    if (sock == RPC_ANYSOCK) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        tprintf("socket() => %d\n", sock);
        if (sock < 0) {
            svc_perror(errno, "svc_tcp.c - tcp socket creation problem");
            return ((SVCXPRT *)NULL);
        }
        madesock = TRUE;
    }
    bzero((char *)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    if (bindresvport(sock, &addr)) {
        addr.sin_port = 0;
        (void)bind(sock, (struct sockaddr *)&addr, len);
    }

    ret = getsockname(sock, (struct sockaddr *)&addr, &len);
    if (ret != 0) {
        err = errno;
        svc_perror(err, "svc_tcp.c - getsockname(...) failed");
    }

    if (ret == 0) {
        ret = listen(sock, SOMAXCONN);
        if (ret != 0) {
            err = errno;
            svc_perror(err, "svc_tcp.c - listen() failed");
        }
    }

    if (ret != 0) {
        if (madesock) {
            (void)close(sock);
        }
        return ((SVCXPRT *)NULL);
    }

    r = (struct tcp_rendezvous *)guard_malloc(sizeof (*r));
    xprt = alloc_xprt();

    /*
     * Constructor for @type{SVCXPRT}, including the additional @type{mtxprt_t}
     * Order of construction is important.
     * We want to create a lock for each @type{SVCXPRT}.
     * The rest of the contructor should all be done while the lock is held.
     * Even if it is not _really_ necessary, it will keep Valgrind/Helgrind
     * happy.  And they are our friends.
     * Second step is to initialize the "magic" value, so that other
     * helper functions that validate it will be happy.
     */

    mtxprt = xprt_to_mtxprt_nocheck(xprt);

    if (pthread_mutex_init(&(mtxprt->mtxp_lock), NULL) != 0) {
        abort();
    }

    /*
     * Do not use xport_lock(xprt) here.
     * The constructor has not progressed far enough, yet.
     */
    if (pthread_mutex_lock(&(mtxprt->mtxp_lock)) != 0) {
        abort();
    }

    /*
     * Set "magic", right away.
     * Other functions validate it.  Keep them happy.
     */
    mtxprt->mtxp_magic = MTXPRT_MAGIC;

    r->sendsize = sendsize;
    r->recvsize = recvsize;
    xprt->xp_p2 = NULL;
    xprt->xp_p1 = (caddr_t)r;
    xprt->xp_verf = _null_auth;
    xprt->xp_ops = &svctcp_rendezvous_op;
    xprt->xp_port = ntohs(addr.sin_port);
    xprt->xp_sock = sock;

    mtxprt->mtxp_creator = pthread_self();
    mtxprt->mtxp_id = -1;
    mtxprt->mtxp_clone  = NULL;
    mtxprt->mtxp_parent = NO_PARENT;
    mtxprt->mtxp_refcnt = 0;
    if (pthread_mutex_init(&(mtxprt->mtxp_progress_lock), NULL) != 0) {
        abort();
    }
    mtxprt->mtxp_progress = 0;
    xprt_set_busy(xprt, 0);
    xprt_unlock(xprt);
    xprt_register(xprt);
    return (xprt);
}

SVCXPRT *
svctcp_create(int sock, u_int sendsize, u_int recvsize)
{
    SVCXPRT *xprt;

    xprt = svctcp_create_with_lock(sock, sendsize, recvsize);
    return (xprt);
}

/*
 * Like svctcp_create(), except the routine takes any *open* UNIX file
 * descriptor as its first input.
 */
SVCXPRT *
svcfd_create(int fd, u_int sendsize, u_int recvsize)
{
    tprintf("fd=%d, sendsize=%u, recvsize=%u\n", fd, sendsize, recvsize);
    return (makefd_xprt(fd, sendsize, recvsize));
}

static SVCXPRT *
makefd_xprt_with_lock(int fd, u_int sendsize, u_int recvsize)
{
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    struct tcp_conn *cd;

    tprintf("fd=%d, sendsize=%u, recvsize=%u\n", fd, sendsize, recvsize);
    xprt = alloc_xprt();
    cd = (struct tcp_conn *)guard_malloc(sizeof(struct tcp_conn));
    cd->strm_stat = XPRT_IDLE;
    xdrrec_create(&(cd->xdrs), sendsize, recvsize, (caddr_t)xprt, readtcp, writetcp);

    /*
     * Constructor for @type{SVCXPRT}, including the additional @type{mtxprt_t}
     * Order of construction is important.
     * We want to create a lock for each SVCXPRT.
     * The rest of the contructor should all be done while the lock is held.
     * Even if it is not _really_ necessary, it will keep Valgrind/Helgrind
     * happy.  And they are our friends.
     * Second step is to initialize the "magic" value, so that other
     * helper functions that validate it will be happy.
     */

    mtxprt = xprt_to_mtxprt_nocheck(xprt);

    if (pthread_mutex_init(&(mtxprt->mtxp_lock), NULL) != 0) {
        abort();
    }

    /*
     * Do not use xport_lock(xprt) here.
     * The constructor has not progressed far enough, yet.
     */
    if (pthread_mutex_lock(&(mtxprt->mtxp_lock)) != 0) {
        abort();
    }
    mtxprt->mtxp_progress = 0;
    xprt->xp_p2 = NULL;
    xprt->xp_p1 = (caddr_t)cd;
    xprt->xp_verf.oa_base = cd->verf_body;
    xprt->xp_addrlen = 0;
    xprt->xp_ops = &svctcp_op;  /* truly deals with calls */
    xprt->xp_port = 0;          /* this is a connection, not a rendezvouser */
    xprt->xp_sock = fd;

    /*
     * Set "magic", right away.
     * Other functions validate it.  Keep them happy.
     */
    mtxprt->mtxp_magic = MTXPRT_MAGIC;
    mtxprt->mtxp_creator = pthread_self();
    mtxprt->mtxp_id = -1;
    mtxprt->mtxp_clone  = NULL;
    mtxprt->mtxp_parent = NO_PARENT;
    mtxprt->mtxp_refcnt = 0;
    if (pthread_mutex_init(&(mtxprt->mtxp_progress_lock), NULL) != 0) {
        abort();
    }
    xprt_set_busy(xprt, 0);
    xprt_unlock(xprt);
    xprt_register(xprt);
    return (xprt);
}

static SVCXPRT *
makefd_xprt(int fd, u_int sendsize, u_int recvsize)
{
    SVCXPRT *xprt;

    xprt = makefd_xprt_with_lock(fd, sendsize, recvsize);
    return (xprt);
}

static bool_t
rendezvous_request(SVCXPRT *xprt, struct rpc_msg *errmsg)
{
    int sock;
    struct tcp_rendezvous *r;
    struct sockaddr_in addr;
    socklen_t len;
    int err;

    UNUSED(errmsg);
    r = (struct tcp_rendezvous *)xprt->xp_p1;

  again:
    len = sizeof(struct sockaddr_in);
    sock = accept(xprt->xp_sock, (struct sockaddr *)&addr, &len);
    err = errno;
    tprintf("accept() => %d\n", sock);
    if (sock < 0) {
        if (err == EINTR)
            goto again;
        return (FALSE);
    }
    /*
     * Make a new transporter (re-uses xprt)
     */
    xprt = makefd_xprt(sock, r->sendsize, r->recvsize);
    memcpy(&xprt->xp_raddr, &addr, sizeof(addr));
    xprt->xp_addrlen = len;
    return (FALSE);             /* There is never an rpc msg to be processed */
}

static enum xprt_stat
rendezvous_stat(SVCXPRT *xprt  __attribute__((unused)))
{
    return (XPRT_IDLE);
}

static void
svctcp_destroy(SVCXPRT *xprt)
{
    mtxprt_t *mtxprt;
    int sock;

    xprt_lock(xprt);
    mtxprt = xprt_to_mtxprt(xprt);
    sock = xprt->xp_sock;

    /*
     * Close socket if this xprt is not a clone.
     */
    if (mtxprt->mtxp_parent == NO_PARENT) {
        struct stat statb;
        int rv;
        int err;

        rv = fstat(sock, &statb);
        err = errno;
        if (rv == 0) {
            tprintf("close(sock=%d)\n", sock);
            (void)close(sock);
        }
        else if (err == EBADF) {
            tprintf("sock=%d -- already closed.\n", sock);
        }
        else {
            char errmsgbuf[64];
            char esymbuf[32];
            char *ep;

            (void) decode_esym_r(esymbuf, sizeof (esymbuf), err);
            ep = strerror_r(err, errmsgbuf, sizeof (errmsgbuf));
            tprintf("sock=%d -- errno=%d=%s='%s'\n",
                sock, err, esymbuf, ep);
        }
    }

    if (xprt->xp_port != 0) {
        /* a rendezvouser socket */
        tprintf("Socket type(%d): rendezvous\n", sock);
        xprt->xp_port = 0;
    } else {
        /* an actual connection socket */
        struct tcp_conn *cd;

        tprintf("Socket type(%d): connection\n", sock);
        cd = (struct tcp_conn *)xprt->xp_p1;
        XDR_DESTROY(&(cd->xdrs));
    }
    free(xprt->xp_p1);
    xprt_unlock(xprt);

    xports_global_lock();
    xprt_unregister(xprt);
    xports_global_unlock();
    free(xprt);
}


/*
 * reads data from the tcp connection.
 * any error is fatal and the connection is closed.
 * (And a read of zero bytes is a half closed stream => error.)
 */
static int
readtcp_with_lock(char *xprtptr, char *buf, int ilen)
{
    SVCXPRT *xprt;
    int sock;
    int milliseconds;
    struct pollfd pollfd;
    size_t len;
    ssize_t rdlen;
    int rv;
    int err;

    tprintf("ilen=%d\n", ilen);
    xprt = (SVCXPRT *)xprtptr;
    sock = xprt->xp_sock;
    milliseconds = 35 * 1000;
    if (opt_svc_trace) {
        tprintf("xprt=%s, sock=%d, ilen=%d\n", decode_addr(xprt), sock, ilen);
        eprintf("        peer=%s\n", decode_inet_peer(sock));
    }

    do {
        pollfd.fd = sock;
        pollfd.events = POLLIN;
        rv = poll(&pollfd, 1, milliseconds);
        switch (rv) {
        case -1:
            if (errno == EINTR)
                continue;
            err = errno;
            teprintf("errno = %d\n", err);
            goto fatal_err;
        case 0:
            teprintf("poll() => 0\n");
            goto fatal_err;
        default:
            if ((pollfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                teprintf("pollfd.revents=%x\n", pollfd.revents);
                goto fatal_err;
            }
            break;
        }
    } while ((pollfd.revents & POLLIN) == 0);

    len = (size_t)ilen;
    rdlen = read(sock, buf, len);
    err = errno;
    tprintf("read(%d, %s, %zu) => %zd\n", sock, decode_addr(buf), len, rdlen);
    if (rdlen > 0) {
        return (ssize_to_int(rdlen));
    }

    if (rdlen < 0) {
        tprintf("read(): errno=%d\n", err);
    }

  fatal_err:
    ((struct tcp_conn *)(xprt->xp_p1))->strm_stat = XPRT_DIED;
    return (-1);
}

pthread_mutex_t tcp_lock = PTHREAD_MUTEX_INITIALIZER;

static int
readtcp(char *xprtptr, char *buf, int len)
{
    int rv;

    pthread_mutex_lock(&tcp_lock);
    rv = readtcp_with_lock(xprtptr, buf, len);
    pthread_mutex_unlock(&tcp_lock);
    return (rv);
}

/*
 * Writes data to the tcp connection.
 * Any error is fatal and the connection is closed.
 */
static int
writetcp(char *xprtptr, char *buf, int len)
{
    SVCXPRT *xprt;
    int sock;
    int cnt;
    int wlen;

    xprt = (SVCXPRT *)xprtptr;
    sock = xprt->xp_sock;
    tprintf("xprt=%s, sock=%d\n", decode_addr(xprt), sock);
    pthread_mutex_lock(&tcp_lock);
    for (cnt = len; cnt > 0; cnt -= wlen, buf += wlen) {
        wlen = write(sock, buf, cnt);
        if (wlen < 0) {
            ((struct tcp_conn *)(xprt->xp_p1))->strm_stat = XPRT_DIED;
            len = -1;
            break;
        }
    }
    pthread_mutex_unlock(&tcp_lock);
    return (len);
}

static enum xprt_stat
svctcp_stat(SVCXPRT *xprt)
{
    struct tcp_conn *cd;
    int rv;

    cd = (struct tcp_conn *)(xprt->xp_p1);

    if (cd->strm_stat == XPRT_DIED) {
        return (XPRT_DIED);
    }

    xdr_enter();
    rv = xdrrec_eof(&(cd->xdrs));
    xdr_exit();

    if (!rv) {
        return (XPRT_MOREREQS);
    }
    return (XPRT_IDLE);
}

static bool_t
svctcp_recv(SVCXPRT *xprt, struct rpc_msg *msg)
{
    struct tcp_conn *cd;
    XDR *xdrs;
    int rv;

    tprintf("xprt=%s, msg=%s\n", decode_addr(xprt), decode_addr(msg));
    xprt_progress_clrbits(xprt, XPRT_DONE_RECV);
    xdr_enter();
    xprt_lock(xprt);
    cd = (struct tcp_conn *)(xprt->xp_p1);
    xdrs = &(cd->xdrs);
    xdrs->x_op = XDR_DECODE;
    (void)xdrrec_skiprecord(xdrs);
    if (xdr_callmsg(xdrs, msg)) {
        cd->x_id = msg->rm_xid;
        rv = TRUE;
    }
    else {
        cd->strm_stat = XPRT_DIED;
        rv = FALSE;
    }
    xprt_unlock(xprt);
    xdr_exit();

#ifdef CONFIG_DIE_ON_RECV_FAILURE

    if (failfast && rv == 0) {
        // Die quickly in case of error.
        teprintf("rv = %d\n", rv);
        if (opt_svc_trace) {
            show_xports();
        }
        svc_die();
    }

#endif /* CONFIG_DIE_ON_RECV_FAILURE */

    xprt_progress_setbits(xprt, XPRT_DONE_RECV);
    return (rv);
}

static bool_t
svctcp_getargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
    struct tcp_conn *cd;
    XDR *xdrs;
    bool_t rv;

    tprintf("xprt=%s, args_ptr=%s\n", decode_addr(xprt), decode_addr(args_ptr));

#ifdef CONFIG_WAIT_FOR_RECV
    while ((xprt_get_progress(xprt) & XPRT_DONE_RECV) == 0) {
        const struct timespec ms = { 0, 1000000 };
        nanosleep(&ms, NULL);
    }
#endif /* CONFIG_WAIT_FOR_RECV */

    xdr_enter();
    xprt_lock(xprt);
    cd = (struct tcp_conn *)(xprt->xp_p1);
    xdrs = &(cd->xdrs);
    xdrs->x_op = XDR_DECODE;
    rv = (*xdr_args) (xdrs, args_ptr);
    tprintf("rv = %d\n", rv);
    xprt_unlock(xprt);
    xdr_exit();

    if (failfast && rv == 0) {
        // Die quickly in case of error.
        teprintf("rv = %d\n", rv);
        if (opt_svc_trace) {
            show_xports();
        }
        svc_die();
    }

    xprt_progress_setbits(xprt, XPRT_DONE_GETARGS);
    return (rv);
}

static bool_t
svctcp_freeargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
    struct tcp_conn *cd;
    XDR *xdrs;
    bool_t rv;

    tprintf("xprt=%s, args_ptr=%s\n", decode_addr(xprt), decode_addr(args_ptr));
    xdr_enter();
    xprt_lock(xprt);
    cd = (struct tcp_conn *)(xprt->xp_p1);
    xdrs = &(cd->xdrs);
    xdrs->x_op = XDR_FREE;
    rv = ((*xdr_args) (xdrs, args_ptr));
    xprt_unlock(xprt);
    xdr_exit();

    if (failfast && rv == 0) {
        // Die quickly in case of error.
        show_xports();
        svc_die();
    }

    return (rv);
}

static bool_t
svctcp_reply(SVCXPRT *xprt, struct rpc_msg *msg)
{
    struct tcp_conn *cd;
    XDR *xdrs;
    bool_t stat;

    tprintf("xprt=%s, msg=%s\n", decode_addr(xprt), decode_addr(msg));
    xdr_enter();
    xprt_lock(xprt);
    cd = (struct tcp_conn *)(xprt->xp_p1);
    xdrs = &(cd->xdrs);
    xdrs->x_op = XDR_ENCODE;
    msg->rm_xid = cd->x_id;
    stat = xdr_replymsg(xdrs, msg);
    (void)xdrrec_endofrecord(xdrs, TRUE);
    xprt_unlock(xprt);
    xdr_exit();
    return (stat);
}
