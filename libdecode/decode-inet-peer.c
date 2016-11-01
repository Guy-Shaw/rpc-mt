#include <sys/socket.h>
#include <errno.h>

#include "decode.h"

char *
decode_inet_peer_r(char *buf, size_t bufsz, int socket)
{
    struct sockaddr peer_addr;
    socklen_t addrlen;
    int ret;

    addrlen = sizeof (peer_addr);
    ret = getpeername(socket, &peer_addr, &addrlen);
    if (ret == 0) {
        (void)decode_inet_endpoint_r(buf, bufsz, &peer_addr);
    }
    else {
        int err;

        err = errno;
        *buf = 'E';
        (void)decode_int_r(buf + 1, bufsz - 1, err);
    }
    return (buf);
}

char *
decode_inet_peer(int socket)
{
    return (decode_inet_peer_r(dbuf_slot_alloc(4), DBUF_SIZE, socket));
}
