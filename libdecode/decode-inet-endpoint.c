
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include "decode.h"

char *
decode_inet_endpoint_r(char *buf, size_t bufsz, void *inet_addr)
{
    struct sockaddr_in *sockaddr;
    sa_family_t family;
    struct in_addr *ipaddr;
    int port;
    char *ebuf;
    const char *ntop_rv;
    int err;

    ebuf = buf;
    ebuf = append_buf(buf, bufsz, ebuf, "[");
    family = ((struct sockaddr *)inet_addr)->sa_family;
    decode_inet_family_r(ebuf, bufsz - (ebuf - buf), family);
    ebuf = strend(buf);
    ebuf = append_buf(buf, bufsz, ebuf, ":");
    sockaddr = (struct sockaddr_in *)inet_addr;
    ipaddr = &(sockaddr->sin_addr);
    ntop_rv = inet_ntop(family, ipaddr, ebuf, bufsz - (ebuf - buf));
    err = errno;
    if (ntop_rv != NULL) {
        ebuf = strend(ebuf);
    }
    else {
        ebuf = buf;
        *ebuf = 'E';
        ++ebuf;
        snprintf(ebuf, bufsz - (ebuf - buf), "%d", err);
        ebuf = strend(ebuf);
    }
    ebuf = append_buf(buf, bufsz, ebuf, ":");
    port = sockaddr->sin_port;
    snprintf(ebuf, bufsz - (ebuf - buf), "%d", port);
    ebuf = strend(ebuf);
    ebuf = append_buf(buf, bufsz, ebuf, "]");
    return (buf);
}

char *
decode_inet_endpoint(void *inet_addr)
{
    return (decode_inet_endpoint_r(dbuf_slot_alloc(3), DBUF_SIZE, inet_addr));
}
