
#include <poll.h>

#include "decode.h"

#define DECODE_POLLSYM(sym) \
    ({ \
        if (events & sym) { \
            if (count) { \
                ebuf = append_buf(buf, bufsz, ebuf, "|"); \
            } \
            ebuf = append_buf(buf, bufsz, ebuf, #sym); \
            ++count; \
        } \
        valid_msk |= sym; \
        err += (popcount_int(sym) != 1); \
    })

char *
decode_poll_events_r(char *buf, size_t bufsz, int events)
{
    char *ebuf;
    int valid_msk;
    int err;
    int count;

    sprintf(buf, "%#x=", events);
    ebuf = strend(buf);

    count = 0;
    err = 0;
    valid_msk = 0;
#if defined(POLLIN)
    DECODE_POLLSYM(POLLIN);
#endif

#if defined(POLLPRI)
    DECODE_POLLSYM(POLLPRI);
#endif

#if defined(POLLOUT)
    DECODE_POLLSYM(POLLOUT);
#endif

#if defined(POLLERR)
    DECODE_POLLSYM(POLLERR);
#endif

#if defined(POLLHUP)
    DECODE_POLLSYM(POLLHUP);
#endif

#if defined(POLLNVAL)
    DECODE_POLLSYM(POLLNVAL);
#endif

#if defined(POLLRDNORM)
    DECODE_POLLSYM(POLLRDNORM);
#endif

#if defined(POLLRDBAND)
    DECODE_POLLSYM(POLLRDBAND);
#endif

#if defined(POLLWRNORM)
    DECODE_POLLSYM(POLLWRNORM);
#endif

#if defined(POLLWRBAND)
    DECODE_POLLSYM(POLLWRBAND);
#endif

#if defined(POLLMSG)
    DECODE_POLLSYM(POLLMSG);
#endif

#if defined(POLLREMOVE)
    DECODE_POLLSYM(POLLREMOVE);
#endif

#if defined(POLLRDHUP)
    DECODE_POLLSYM(POLLRDHUP);
#endif

    if (err) {
        ebuf = append_buf(buf, bufsz, ebuf, "*ERROR*");
        ++count;
    }

    if (events & ~valid_msk) {
        if (count) {
            ebuf = append_buf(buf, bufsz, ebuf, ",");
        }
        ebuf = append_buf(buf, bufsz, ebuf, "*INVALID*");
        ++count;
    }
    return (buf);
}

char *
decode_poll_events(int events)
{
    return (decode_poll_events_r(dbuf_slot_alloc(1), DBUF_SIZE, events));
}
