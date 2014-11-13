
#include "decode.h"

/*
 * Utiliy functions used by many libdecode functions.
 */

char *
append_buf(char *buf, size_t bufsz, char *ebuf, char const *new)
{
    size_t cnt;

    assert(in_buf(buf, bufsz, ebuf));
    cnt = (buf + bufsz) - ebuf;
    while (cnt != 0 && *new != 0) {
        *ebuf++ = *new++;
        --cnt;
    }
    if (cnt <= 0) {
        ebuf = buf + bufsz - 1;
    }
    *ebuf = '\0';
    return (ebuf);
}

int
popcount_int(int i)
{
    unsigned int w;
    int bits;

    w = i;
    bits = 0;
    while (w != 0) {
        w &= (w - 1);
        ++bits;
    }
    return (bits);
}
