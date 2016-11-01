
#include <stdio.h>	// Import snprintf()

#include "decode.h"

char *
decode_int_r(char *buf, size_t bufsz, int i)
{
    (void)snprintf(buf, bufsz, "%d", i);
    return (buf);
}

char *
decode_int(int i)
{
    return decode_int_r(dbuf_slot_alloc(5), DBUF_SIZE, i);
}
