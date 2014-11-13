
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "decode.h"

char *
decode_inet_endpoint_r(char *buf, size_t bufsz, void *inet_addr)
{
    struct sockaddr_in *sockaddr;
    sa_family_t family;
    struct in_addr *ipaddr;
    int port;
    char *ebuf;

    family = ((struct sockaddr *)inet_addr)->sa_family;
    decode_inet_family_r(buf, bufsz, family);
    ebuf = strend(buf);
    ebuf = append_buf(buf, bufsz, ebuf, ":");
    sockaddr = (struct sockaddr_in *)inet_addr;
    ipaddr = &(sockaddr->sin_addr);
    (void)inet_ntop(family, ipaddr, ebuf, bufsz - (ebuf - buf));
    ebuf = strend(ebuf);
    ebuf = append_buf(buf, bufsz, ebuf, ":");
    port = sockaddr->sin_port;
    snprintf(ebuf, bufsz - (ebuf - buf), "%d", port);
    return (buf);
}
