/*
 * svc_udp.c,
 * Server side for UDP/IP based RPC.  (Does some caching in the hopes of
 * achieving execute-at-most-once semantics.)
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
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
 *     * Neither the name of Sun Microsystems, Inc. nor the names of its
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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/stat.h>
#include <libintl.h>

#ifdef IP_PKTINFO
#include <sys/uio.h>
#endif

#ifdef USE_IN_LIBIO
# include <wchar.h>
# include <libio/iolibio.h>
#endif

#include "svc_mtxprt.h"
#include "svc_debug.h"

#define rpc_buffer(xprt) ((xprt)->xp_p1)
#ifndef MAX
#define MAX(a, b)     ((a > b) ? a : b)
#endif

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

static bool_t svcudp_recv(SVCXPRT *, struct rpc_msg *);
static bool_t svcudp_reply(SVCXPRT *, struct rpc_msg *);
static enum xprt_stat svcudp_stat(SVCXPRT *);
static bool_t svcudp_getargs(SVCXPRT *, xdrproc_t, caddr_t);
static bool_t svcudp_freeargs(SVCXPRT *, xdrproc_t, caddr_t);
static void svcudp_destroy(SVCXPRT *);
static SVCXPRT *svcudp_xprt_clone(SVCXPRT *);

static const struct xp_ops svcudp_op = {
    svcudp_recv,
    svcudp_stat,
    svcudp_getargs,
    svcudp_reply,
    svcudp_freeargs,
    svcudp_destroy
};

static int cache_get(SVCXPRT *, struct rpc_msg *, char **replyp, u_long *replylenp);
static void cache_set(SVCXPRT *xprt, u_long replylen);

/*
 * kept in xprt->xp_p2
 */
struct svcudp_data
{
    u_int su_iosz;                      /* byte size of send.recv buffer */
    u_long su_xid;                      /* transaction id */
    XDR su_xdrs;                        /* XDR handle */
    char su_verfbody[MAX_AUTH_BYTES];   /* verifier body */
    char *su_cache;                     /* cached data, NULL if no cache */
};

#define su_data(xprt) ((struct svcudp_data *)(xprt->xp_p2))

/*
 * Navigate from a pointer to @type{SVCXPRT} to its XDR.
 */
static inline XDR *
select_xprt_xdrs(SVCXPRT *xprt)
{
    struct svcudp_data *su;
    XDR *xdrs;

    su = su_data(xprt);
    xdrs = &(su->su_xdrs);
    return (xdrs);
}

/*
 * If sock<0 then a socket is created, else sock is used.
 * If the socket, sock is not bound to a port then svcudp_create
 * binds it to an arbitrary port.  In any (successful) case,
 * xprt->xp_sock is the registered socket number and xprt->xp_port is the
 * associated port number.
 * Once *xprt is initialized, it is registered as a transporter;
 * see (svc.h, xprt_register).
 * The routines returns NULL if a problem occurred.
 */
SVCXPRT *
svcudp_bufcreate(int sock, u_int sendsz, u_int recvsz)
{
    bool_t madesock;
    SVCXPRT *xprt;
    mtxprt_t *mtxprt;
    struct svcudp_data *su;
    struct sockaddr_in addr;
    socklen_t len;
    int pad;
    void *buf;
    size_t bufsize;

    if (sock == RPC_ANYSOCK) {
        tprintf("sock=RPC_ANYSOCK\n");
    }
    else {
        tprintf("sock=%d\n", sock);
    }

    madesock = FALSE;
    len = sizeof(struct sockaddr_in);
    if (sock == RPC_ANYSOCK) {
        if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
            svc_perror(errno, "svcudp_create: socket creation problem");
            return ((SVCXPRT *)NULL);
        }
        tprintf("socket() => %d\n", sock);
        madesock = TRUE;
    }

    bzero((char *)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    if (bindresvport(sock, &addr)) {
        addr.sin_port = 0;
        (void)bind(sock, (struct sockaddr *)&addr, len);
    }
    if (getsockname(sock, (struct sockaddr *)&addr, &len) != 0) {
        svc_perror(errno, "svcudp_create - cannot getsockname");
        if (madesock)
            (void)close(sock);
        return ((SVCXPRT *)NULL);
    }
    bufsize = ((MAX(sendsz, recvsz) + 3) / 4) * 4;

    xprt = alloc_xprt();

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

    /*
     * Set "magic", right away.
     * Other functions validate it.  Keep them happy.
     */
    mtxprt->mtxp_magic = MTXPRT_MAGIC;

    /*
     * Populate the "standard" SVCXPRT
     */
    su = (struct svcudp_data *)guard_malloc(sizeof(*su));
    buf = guard_malloc(bufsize);

    su->su_iosz = bufsize;
    rpc_buffer(xprt) = buf;
    xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt), su->su_iosz, XDR_DECODE);
    su->su_cache = NULL;
    xprt->xp_p2 = (caddr_t)su;
    xprt->xp_verf.oa_base = su->su_verfbody;
    xprt->xp_ops = &svcudp_op;
    xprt->xp_port = ntohs(addr.sin_port);
    xprt->xp_sock = sock;

    /*
     * Populate the rest of mtxprt
     */
    mtxprt->mtxp_creator = pthread_self();
    mtxprt->mtxp_id = -1;
    mtxprt->mtxp_bufsz  = bufsize;
    mtxprt->mtxp_clone  = svcudp_xprt_clone;
    mtxprt->mtxp_parent = NO_PARENT;
    mtxprt->mtxp_refcnt = 0;
    if (pthread_mutex_init(&(mtxprt->mtxp_progress_lock), NULL) != 0) {
        abort();
    }
    xprt_set_busy(xprt, 0);
    xprt_unlock(xprt);

#ifdef IP_PKTINFO
    if ((sizeof(struct iovec) + sizeof(struct msghdr)
         + sizeof(struct cmsghdr) + sizeof(struct in_pktinfo))
        > sizeof(xprt->xp_pad)) {
        (void) eprintf("svcudp_create: xp_pad is too small for IP_PKTINFO\n");
        return (NULL);
    }
    pad = 1;
    if (setsockopt(sock, SOL_IP, IP_PKTINFO, (void *)&pad, sizeof(pad)) == 0) {
        /* Set the padding to all 1s. */
        pad = 0xff;
    } else {
        /* Clear the padding. */
        pad = 0;
    }
#else
    /* Clear the padding. */
    pad = 0;
#endif

    memset(&xprt->xp_pad[0], pad, sizeof(xprt->xp_pad));

    xprt_register(xprt);
    return (xprt);
}

/*
 * Clone a @type{SVCXPRT}.
 * Do a shallow copy, then change the clone, as needed, to ensure
 * that the parts that need to be private (per thread) are
 * deep-copied.
 */
static SVCXPRT *
svcudp_xprt_clone(SVCXPRT *xprt1)
{
    SVCXPRT *xprt2;
    mtxprt_t *mtxprt1;
    mtxprt_t *mtxprt2;
    struct svc_req *rqstp2;
    struct rpc_msg *msgp2;
    struct svcudp_data *su1;
    struct svcudp_data *su2;
    XDR *xdrs1;
    XDR *xdrs2;
    void *buf;
    size_t bufsize;

#ifdef IP_PKTINFO
    struct msghdr *mesgp1;
    struct msghdr *mesgp2;
    struct iovec *iovp;
#endif

    xprt2 = alloc_xprt();

    /*
     * Shallow copy
     */
    memcpy(xprt2, xprt1, sizeof (SVCXPRT) + sizeof (mtxprt_t));
    mtxprt1 = xprt_to_mtxprt(xprt1);
    mtxprt2 = xprt_to_mtxprt_nocheck(xprt2);
    bufsize = mtxprt1->mtxp_bufsz;

    if (pthread_mutex_init(&(mtxprt2->mtxp_lock), NULL) != 0) {
        abort();
    }

    /*
     * Do not use xport_lock(xprt2) here.
     * The constructor has not progressed far enough, yet.
     */
    if (pthread_mutex_lock(&(mtxprt2->mtxp_lock)) != 0) {
        abort();
    }

    /*
     * Set "magic", right away.
     * Other functions validate it.  Keep them happy.
     */
    mtxprt2->mtxp_magic = MTXPRT_MAGIC;

    /*
     * Populate the "standard" SVCXPRT
     */
    su1 = su_data(xprt1);
    su2 = (struct svcudp_data *)guard_malloc(sizeof (*su2));
    memcpy(su2, su1, sizeof (*su2));
    su2->su_cache = su1->su_cache;
    xprt2->xp_p2 = (caddr_t)su2;
    xprt2->xp_verf.oa_base = su2->su_verfbody;
    xprt2->xp_ops = &svcudp_op;

    /*
     * Populate the rest of mtxprt
     */
    mtxprt2->mtxp_id = -1;
    mtxprt2->mtxp_creator = pthread_self();
    mtxprt2->mtxp_parent = mtxprt1->mtxp_id;
    mtxprt2->mtxp_refcnt = 0;
    if (pthread_mutex_init(&(mtxprt2->mtxp_progress_lock), NULL) != 0) {
        abort();
    }
    xprt_set_busy(xprt2, 0);
    rqstp2 = &(mtxprt2->mtxp_rqst);
    msgp2 = &(mtxprt2->mtxp_msg);
    rqstp2->rq_clntcred = &(mtxprt2->mtxp_cred[2 * MAX_AUTH_BYTES]);
    rqstp2->rq_xprt = xprt2;
    msgp2->rm_call.cb_cred.oa_base = &(mtxprt2->mtxp_cred[0]);
    msgp2->rm_call.cb_verf.oa_base = &(mtxprt2->mtxp_cred[MAX_AUTH_BYTES]);
    buf = guard_malloc(bufsize);
    rpc_buffer(xprt2) = buf;
    xdrmem_create(&(su2->su_xdrs), rpc_buffer(xprt2), su2->su_iosz, XDR_DECODE);
    memcpy(rpc_buffer(xprt2), rpc_buffer(xprt1), bufsize);
    xdrs1 = &(su1->su_xdrs);
    xdrs2 = &(su2->su_xdrs);
    xdrs2->x_op = xdrs1->x_op;
    xdrs2->x_handy = xdrs1->x_handy;
    xdrs2->x_private += (xdrs1->x_private - xdrs1->x_base);

#ifdef IP_PKTINFO
    mesgp1 = (struct msghdr *)&xprt1->xp_pad[sizeof(struct iovec)];
    mesgp2 = (struct msghdr *)&xprt2->xp_pad[sizeof(struct iovec)];
    if (mesgp2->msg_iovlen) {
        iovp = (struct iovec *)&xprt2->xp_pad[0];
        iovp->iov_base = rpc_buffer(xprt2);
        iovp->iov_len = su2->su_iosz;
        mesgp2->msg_iov = iovp;
        mesgp2->msg_iovlen = 1;
        mesgp2->msg_name = &(xprt2->xp_raddr);
        mesgp2->msg_namelen = (socklen_t) sizeof(struct sockaddr_in);
        if (mesgp1->msg_control != NULL) {
            mesgp2->msg_control = &xprt2->xp_pad[sizeof(struct iovec) + sizeof(struct msghdr)];
            mesgp2->msg_controllen = sizeof(xprt2->xp_pad)
                - sizeof(struct iovec) - sizeof(struct msghdr);
        }
        else {
            if (!(mesgp2->msg_control == NULL)) {
                teprintf("assertion failed: mesgp2->msg_control == NULL.\n");
                abort();
            }
            if (!(mesgp2->msg_controllen == 0)) {
                teprintf("assertion failed: mesgp2->msg_controllen == 0.\n");
                abort();
            }
        }
    }
#endif

    xprt_unlock(xprt2);
    xprt_register(xprt2);
    return (xprt2);
}

SVCXPRT *
svcudp_create(int sock)
{
    SVCXPRT *xprt;

    xprt = svcudp_bufcreate(sock, UDPMSGSIZE, UDPMSGSIZE);
    return (xprt);
}

static enum xprt_stat
svcudp_stat(SVCXPRT *xprt)
{
    return (XPRT_IDLE);
}

#ifdef IP_PKTINFO

#define SIMPLE_IP_PKTINFO_SIZE \
    (sizeof(struct cmsghdr) + sizeof(struct in_pktinfo))

static inline int
is_simple_ip_pktinfo(struct msghdr *mesgp, struct cmsghdr *cmsg)
{
    int is_simple;

    is_simple =
        cmsg != NULL
        && CMSG_NXTHDR(mesgp, cmsg) != NULL
        && cmsg->cmsg_level == SOL_IP
        && cmsg->cmsg_type == IP_PKTINFO
        && cmsg->cmsg_len >= SIMPLE_IP_PKTINFO_SIZE;
    return (is_simple);
}

#endif /* IP_PKTINFO */

static bool_t
svcudp_recv_with_id_lock(SVCXPRT *xprt, struct rpc_msg *msg)
{
    struct svcudp_data *su;
    XDR *xdrs;
    int rlen;
    char *reply;
    u_long replylen;
    socklen_t len;

#ifdef IP_PKTINFO
    struct iovec *iovp;
    struct msghdr *mesgp;
#endif

    {
        pthread_t self;
        mtxprt_t *mtxprt;

        mtxprt = xprt_to_mtxprt(xprt);
        self = pthread_self();
        if (self != mtxprt->mtxp_creator) {
            teprintf("Expect only svc_run() thread to receive.\n");
            return (FALSE);
        }
    }
    su = su_data(xprt);
    xdrs = &(su->su_xdrs);

    /*
     * It is very tricky when you have IP aliases.
     * We want to make sure that we are sending the packet
     * from the IP address where the incoming packet is addressed to.
     * -- H.J.
     */

  again:
    tprintf("@again:\n");
    len = (socklen_t) sizeof(struct sockaddr_in);

#ifdef IP_PKTINFO
    iovp = (struct iovec *)&xprt->xp_pad[0];
    mesgp = (struct msghdr *)&xprt->xp_pad[sizeof(struct iovec)];
    if (mesgp->msg_iovlen) {
        iovp->iov_base = rpc_buffer(xprt);
        iovp->iov_len = su->su_iosz;
        mesgp->msg_iov = iovp;
        mesgp->msg_iovlen = 1;
        mesgp->msg_name = &(xprt->xp_raddr);
        mesgp->msg_namelen = len;
        mesgp->msg_control = &xprt->xp_pad[sizeof(struct iovec) + sizeof(struct msghdr)];
        mesgp->msg_controllen = sizeof(xprt->xp_pad)
            - sizeof(struct iovec) - sizeof(struct msghdr);
        rlen = recvmsg(xprt->xp_sock, mesgp, 0);
        if (rlen >= 0) {
            struct cmsghdr *cmsg;

            len = mesgp->msg_namelen;
            cmsg = CMSG_FIRSTHDR(mesgp);
            if (is_simple_ip_pktinfo(mesgp, cmsg)) {
                /*
                 * It was a simple IP_PKTINFO, as we expected.
                 * Discard the interface field.
                 */
                struct in_pktinfo *pkti = (struct in_pktinfo *)CMSG_DATA(cmsg);
                pkti->ipi_ifindex = 0;
            } else {
                /*
                 * Not a simple IP_PKTINFO, ignore it.
                 */
                mesgp->msg_control = NULL;
                mesgp->msg_controllen = 0;
            }

        }
    } else {
        rlen = recvfrom(xprt->xp_sock, rpc_buffer(xprt), (int)su->su_iosz, 0, (struct sockaddr *)&(xprt->xp_raddr), &len);
    }
#else
    tprintf("recvfrom(%d, _, %d, 0, _, %d)\n",
        xprt->xp_sock, (int)su->su_iosz, *len);
    rlen = recvfrom(xprt->xp_sock, rpc_buffer(xprt), (int)su->su_iosz, 0, (struct sockaddr *)&(xprt->xp_raddr), &len);
#endif

    xprt->xp_addrlen = len;
    if (rlen == -1 && errno == EINTR)
        goto again;
    if (rlen < (4 * sizeof(uint32_t)))  /* < 4 32-bit ints? */
        return (FALSE);
    xdrs->x_op = XDR_DECODE;
    XDR_SETPOS(xdrs, 0);
    if (!xdr_callmsg(xdrs, msg))
        return (FALSE);
    su->su_xid = msg->rm_xid;
    if (su->su_cache != NULL) {
        if (cache_get(xprt, msg, &reply, &replylen)) {

#ifdef IP_PKTINFO
            if (mesgp->msg_iovlen) {
                iovp->iov_base = reply;
                iovp->iov_len = replylen;
                (void)sendmsg(xprt->xp_sock, mesgp, 0);
            } else {
                (void)sendto(xprt->xp_sock, reply, (int)replylen, 0, (struct sockaddr *)&xprt->xp_raddr, len);
            }
#else
            (void)sendto(xprt->xp_sock, reply, (int)replylen, 0, (struct sockaddr *)&xprt->xp_raddr, len);
#endif
            return (TRUE);
        }
    }
    return (TRUE);
}

bool_t
svcudp_recv(SVCXPRT *xprt, struct rpc_msg *msg)
{
    bool_t rv;

    xprt_progress_clrbits(xprt, XPRT_DONE_RECV);
    tprintf("xprt=%s, msg=%s\n", decode_addr(xprt), decode_addr(msg));
    rv = svcudp_recv_with_id_lock(xprt, msg);
    xprt_progress_setbits(xprt, XPRT_DONE_RECV);
    return (rv);
}

static ssize_t
xprt_sendto(SVCXPRT *xprt, size_t slen)
{
    const struct sockaddr *addr;
    socklen_t alen;
    ssize_t sent;

    addr = (struct sockaddr *)&(xprt->xp_raddr);
    alen = xprt->xp_addrlen;
    sent = sendto(xprt->xp_sock, rpc_buffer(xprt), slen, 0, addr, alen);
    return (sent);
}

static bool_t
svcudp_reply(SVCXPRT *xprt, struct rpc_msg *msg)
{
    struct svcudp_data *su;
    XDR *xdrs;
    int err;
    bool_t stat;

#ifdef IP_PKTINFO
    struct iovec *iovp;
    struct msghdr *mesgp;
#endif

    xprt_lock(xprt);
    su = su_data(xprt);
    xdrs = select_xprt_xdrs(xprt);

    xdrs->x_op = XDR_ENCODE;
    XDR_SETPOS(xdrs, 0);
    msg->rm_xid = su->su_xid;
    stat = xdr_replymsg(xdrs, msg);
    tprintf("xdr_replymsg() => %d\n", stat);
    if (stat) {
        size_t slen;
        ssize_t sent;

        stat = FALSE;
        slen = (size_t)XDR_GETPOS(xdrs);
#ifdef IP_PKTINFO
        mesgp = (struct msghdr *)&xprt->xp_pad[sizeof(struct iovec)];
        tprintf("mesgp->msg_iovlen = %d\n", mesgp->msg_iovlen);
        if (mesgp->msg_iovlen) {
            iovp = (struct iovec *)&xprt->xp_pad[0];
            iovp->iov_base = rpc_buffer(xprt);
            iovp->iov_len = slen;
            mesgp->msg_iov = iovp;
            mesgp->msg_iovlen = 1;
            mesgp->msg_name = &(xprt->xp_raddr);
            mesgp->msg_namelen = (socklen_t) sizeof(struct sockaddr_in);
            tprintf("sendmsg(%d, _, 0)\n", xprt->xp_sock);
            sent = sendmsg(xprt->xp_sock, mesgp, 0);
        }
        else {
            sent = xprt_sendto(xprt, slen);
        }
#else
        sent = xprt_sendto(xprt, slen);
#endif
        if (sent < 0) {
            err = errno;
            tprintf("err=%d = '%s'\n", err, strerror(err));
        }
        tprintf("slen=%d, sent=%d\n", slen, sent);
        if (sent == slen) {
            stat = TRUE;
            if (su->su_cache && slen >= 0) {
                cache_set(xprt, (u_long)slen);
            }
        }
    }

    xprt_unlock(xprt);
    return (stat);
}

static bool_t
svcudp_getargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
    XDR *xdrs;
    bool_t rv;

    tprintf("xprt=%s, args_ptr=%s\n", decode_addr(xprt), decode_addr(args_ptr));
    xprt_lock(xprt);
    xdrs = select_xprt_xdrs(xprt);
    rv = (*xdr_args) (xdrs, args_ptr);
    xprt_progress_setbits(xprt, XPRT_DONE_GETARGS);
    xprt_unlock(xprt);
    return (rv);
}

static bool_t
svcudp_freeargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
    XDR *xdrs;
    bool_t rv;

    xprt_lock(xprt);
    xdrs = select_xprt_xdrs(xprt);
    xdrs->x_op = XDR_FREE;
    rv = (*xdr_args) (xdrs, args_ptr);
    xprt_unlock(xprt);
    return (rv);
}

static void
svcudp_destroy(SVCXPRT *xprt)
{
    mtxprt_t           *mtxprt;
    struct svcudp_data *su;
    XDR                *xdrs;
    int                id;

    mtxprt = xprt_to_mtxprt(xprt);
    id = mtxprt->mtxp_id;
    tprintf("xprt=%s, id=%d\n", decode_addr(xprt), id);
    xprt_lock(xprt);
    /*
     * Close socket if this xprt is not a clone.
     */
    if (mtxprt->mtxp_parent == NO_PARENT) {
        struct stat statb;
        int sock;
        int rv;
        int err;

        sock = xprt->xp_sock;
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
    su = su_data(xprt);
    xdrs = &(su->su_xdrs);
    XDR_DESTROY(xdrs);
    free(rpc_buffer(xprt));
    free(su);
    xprt_unlock(xprt);

    xports_global_lock();
    xprt_unregister(xprt);
    free(xprt);
    xports_global_unlock();
}


/*********** this could be a separate file *********************/

/*
 * Fifo cache for udp server
 * Copies pointers to reply buffers into fifo cache
 * Buffers are sent again if retransmissions are detected.
 */

#define SPARSENESS 4            /* 75% sparse */

#define CACHE_PERROR(msg)       \
    (void) eprintf("%s\n", msg)

#define ALLOC(type, size)       \
    (type *) malloc((size_t) (sizeof(type) * (size)))

#define CALLOC(type, size)      \
    (type *) calloc (sizeof (type), size)

/*
 * An entry in the cache
 */
typedef struct cache_node *cache_ptr;

struct cache_node {
    /*
     * Index into cache is xid, proc, vers, prog and address
     */
    u_long cache_xid;
    u_long cache_proc;
    u_long cache_vers;
    u_long cache_prog;
    struct sockaddr_in cache_addr;
    /*
     * The cached reply and length
     */
    char *cache_reply;
    u_long cache_replylen;
    /*
     * Next node on the list, if there is a collision
     */
    cache_ptr cache_next;
};



/*
 * The entire cache
 */
struct udp_cache {
    u_long uc_size;             /* size of cache */
    cache_ptr *uc_entries;      /* hash table of entries in cache */
    cache_ptr *uc_fifo;         /* fifo list of entries in cache */
    u_long uc_nextvictim;       /* points to next victim in fifo list */
    u_long uc_prog;             /* saved program number */
    u_long uc_vers;             /* saved version number */
    u_long uc_proc;             /* saved procedure number */
    struct sockaddr_in uc_addr; /* saved caller's address */
};


/*
 * the hashing function
 */
#define CACHE_LOC(transp, xid) \
 (xid % (SPARSENESS*((struct udp_cache *) su_data(transp)->su_cache)->uc_size))


/*
 * Enable use of the cache.
 * Note: there is no disable.
 */
int
svcudp_enablecache(SVCXPRT *transp, u_long size)
{
    struct svcudp_data *su = su_data(transp);
    struct udp_cache *uc;

    if (su->su_cache != NULL) {
        CACHE_PERROR("enablecache: cache already enabled");
        return (0);
    }
    uc = ALLOC(struct udp_cache, 1);
    if (uc == NULL) {
        CACHE_PERROR("enablecache: could not allocate cache");
        return (0);
    }
    uc->uc_size = size;
    uc->uc_nextvictim = 0;
    uc->uc_entries = CALLOC(cache_ptr, size * SPARSENESS);
    if (uc->uc_entries == NULL) {
        free(uc);
        CACHE_PERROR("enablecache: could not allocate cache data");
        return (0);
    }
    uc->uc_fifo = CALLOC(cache_ptr, size);
    if (uc->uc_fifo == NULL) {
        free(uc->uc_entries);
        free(uc);
        CACHE_PERROR("enablecache: could not allocate cache fifo");
        return (0);
    }
    su->su_cache = (char *)uc;
    return (1);
}


/*
 * Set an entry in the cache
 */
static void
cache_set(SVCXPRT *xprt, u_long replylen)
{
    cache_ptr victim;
    cache_ptr *vicp;
    struct svcudp_data *su;
    struct udp_cache *uc;
    u_int loc;
    char *newbuf;

    su = su_data(xprt);
    uc = (struct udp_cache *)su->su_cache;

    /*
     * Find space for the new entry, either by
     * reusing an old entry, or by mallocing a new one
     */
    victim = uc->uc_fifo[uc->uc_nextvictim];
    if (victim != NULL) {
        loc = CACHE_LOC(xprt, victim->cache_xid);
        for (vicp = &uc->uc_entries[loc]; *vicp != NULL && *vicp != victim; vicp = &(*vicp)->cache_next);
        if (*vicp == NULL) {
            CACHE_PERROR("cache_set: victim not found");
            return;
        }
        *vicp = victim->cache_next;     /* remote from cache */
        newbuf = victim->cache_reply;
    } else {
        victim = ALLOC(struct cache_node, 1);
        if (victim == NULL) {
            CACHE_PERROR("cache_set: victim alloc failed");
            return;
        }
        newbuf = malloc(su->su_iosz);
        if (newbuf == NULL) {
            free(victim);
            CACHE_PERROR("cache_set: could not allocate new rpc_buffer");
            return;
        }
    }

    /*
     * Store it away
     */
    victim->cache_replylen = replylen;
    victim->cache_reply = rpc_buffer(xprt);
    rpc_buffer(xprt) = newbuf;
    xdrmem_create(&(su->su_xdrs), rpc_buffer(xprt), su->su_iosz, XDR_ENCODE);
    victim->cache_xid = su->su_xid;
    victim->cache_proc = uc->uc_proc;
    victim->cache_vers = uc->uc_vers;
    victim->cache_prog = uc->uc_prog;
    victim->cache_addr = uc->uc_addr;
    loc = CACHE_LOC(xprt, victim->cache_xid);
    victim->cache_next = uc->uc_entries[loc];
    uc->uc_entries[loc] = victim;
    uc->uc_fifo[uc->uc_nextvictim++] = victim;
    uc->uc_nextvictim %= uc->uc_size;
}

#define EQADDR(a1, a2) (memcmp((char*)&a1, (char*)&a2, sizeof(a1)) == 0)

static inline int
cache_match(cache_ptr ent, struct svcudp_data *su, struct udp_cache *uc)
{
    int match;

    match = ent->cache_xid == su->su_xid
        && ent->cache_proc == uc->uc_proc
        && ent->cache_vers == uc->uc_vers
        && ent->cache_prog == uc->uc_prog
        && EQADDR(ent->cache_addr, uc->uc_addr);
    return (match);
}

/*
 * Try to get an entry from the cache
 * return 1 if found, 0 if not found
 */
static int
cache_get(SVCXPRT *xprt, struct rpc_msg *msg, char **replyp, u_long *replylenp)
{
    u_int loc;
    cache_ptr ent;
    struct svcudp_data *su;
    struct udp_cache *uc;

    su = su_data(xprt);
    uc = (struct udp_cache *)su->su_cache;

    loc = CACHE_LOC(xprt, su->su_xid);
    for (ent = uc->uc_entries[loc]; ent != NULL; ent = ent->cache_next) {
        if (cache_match(ent, su, uc)) {
            *replyp = ent->cache_reply;
            *replylenp = ent->cache_replylen;
            return (1);
        }
    }
    /*
     * Failed to find entry
     * Remember a few things so we can do a set later
     */
    uc->uc_proc = msg->rm_call.cb_proc;
    uc->uc_vers = msg->rm_call.cb_vers;
    uc->uc_prog = msg->rm_call.cb_prog;
    memcpy(&uc->uc_addr, &xprt->xp_raddr, sizeof(uc->uc_addr));
    return (0);
}
