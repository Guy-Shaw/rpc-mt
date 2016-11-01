
#include "decode.h"

#define NSLOTS 8

static char buf01[128];
static char buf02[128];
static char buf03[128];
static char buf04[128];
static char buf05[128];
static char buf06[128];
static char buf07[128];
static char buf08[128];

static char *dbuf[NSLOTS] = {
    &buf01[0],
    &buf02[0],
    &buf03[0],
    &buf04[0],
    &buf05[0],
    &buf06[0],
    &buf07[0],
    &buf08[0]
};

char *
dbuf_slot_alloc(size_t slot)
{
    assert(slot < NSLOTS);
    return (dbuf[slot]);
}

/*
 * Utiliy functions used by many libdecode functions.
 */

char *
append_buf(char *buf, size_t bufsz, char *ebuf, char const *newstr)
{
    size_t cnt;

    assert(in_buf(buf, bufsz, ebuf));
    cnt = (buf + bufsz) - ebuf;
    while (cnt != 0 && *newstr != 0) {
        *ebuf++ = *newstr++;
        --cnt;
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
