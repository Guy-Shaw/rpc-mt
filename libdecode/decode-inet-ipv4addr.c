
#include <socket.h>

#include "decode.h"

/*
 * Decode an IPv4 address.
 *
 * Never actually return NULL, no matter what might go wrong,
 * because this is a decode function, which should always return
 * something printable.
 */

char *
decode_inet_ipv4_addr_r(char *buf, size_t bufsz, struct sockaddr *addr)
{
    struct sockaddr_in *ipv4_addr;
    char *addr_str;

    addr_str = NULL;
    if (addr) {
        ipv4_addr = (struct sockaddr_in *)addr;
    }
    addr_str = inet_ntoa(ipv4_addr->sin_addr);
    if (addr_str == NULL) {
        addr_str = "<NULL>";
    }

    (void)append_buf(buf, bufsz, buf, addr_str);
    return (buf);
}
