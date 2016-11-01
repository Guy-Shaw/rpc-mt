
#include "decode.h"

static char *inet_af_table[] = {
    /*    0 */ "AF_UNSPEC",
    /*    1 */ "AF_UNIX",
    /*    2 */ "AF_INET",
    /*    3 */ "AF_AX25",
    /*    4 */ "AF_IPX",
    /*    5 */ "AF_APPLETALK",
    /*    6 */ "AF_NETROM",
    /*    7 */ "AF_BRIDGE",
    /*    8 */ "AF_ATMPVC",
    /*    9 */ "AF_X25",
    /*   10 */ "AF_INET6",
    /*   11 */ "AF_ROSE",
    /*   12 */ NULL,
    /*   13 */ "AF_NETBEUI",
    /*   14 */ "AF_SECURITY",
    /*   15 */ "AF_KEY",
    /*   16 */ "AF_NETLINK",
    /*   17 */ "AF_PACKET",
    /*   18 */ "AF_ASH",
    /*   19 */ "AF_ECONET",
    /*   20 */ "AF_ATMSVC",
    /*   21 */ "AF_RDS",
    /*   22 */ "AF_SNA",
    /*   23 */ "AF_IRDA",
    /*   24 */ "AF_PPPOX",
    /*   25 */ "AF_WANPIPE",
    /*   26 */ "AF_LLC",
    /*   27 */ NULL,
    /*   28 */ NULL,
    /*   29 */ "AF_CAN",
    /*   30 */ "AF_TIPC",
    /*   31 */ "AF_BLUETOOTH",
    /*   32 */ "AF_IUCV",
    /*   33 */ "AF_RXRPC",
    /*   34 */ "AF_ISDN",
    /*   35 */ "AF_PHONET",
    /*   36 */ "AF_IEEE802154",
    /*   37 */ "AF_MAX"
};

static unsigned int inet_af_table_size = sizeof (inet_af_table) / sizeof (char *);

char *
decode_inet_family_r(char *buf, size_t bufsz, int family)
{
    char *ebuf;
    char *str_family;

    sprintf(buf, "%u=", family);
    ebuf = strend(buf);
    if (family >= 0 && family < inet_af_table_size) {
        str_family = inet_af_table[family];
    }
    else {
        str_family = "?";
    }
    append_buf(buf, bufsz, ebuf, str_family);
    return (buf);
}

char *
decode_inet_family(int family)
{
    return (decode_inet_family_r(dbuf_slot_alloc(2), DBUF_SIZE, family));
}
