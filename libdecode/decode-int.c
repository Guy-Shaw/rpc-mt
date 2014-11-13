
#include <stdio.h>	// Import snprintf()

#include "decode.h"

char *
decode_int_r(char *buf, size_t bufsz, int i)
{
    (void)snprintf(buf, bufsz, "%d", i);
    return (buf);
}
