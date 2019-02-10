
#include <stdio.h>
    // Import fprintf()
    // Import var stderr
#include <stdlib.h>
    // Import abort()

typedef unsigned int uint_t;

uint_t
int_to_uint(int i)
{
    if (i > 0) {
        return ((uint_t)i);
    }

    fprintf(stderr, "Attempt to cast negative number (%d) to unsigned.\n", i);
    abort();
}

int
ssize_to_int(ssize_t ssz)
{
    int isize;
    ssize_t vsize;

    isize = ssz;
    vsize = isize;
    if (vsize != ssz) {
        fprintf(stderr, "Data loss converting from ssize (%zd) to int.\n", ssz);
        abort();
    }
    return (isize);
}
