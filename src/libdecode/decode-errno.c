#include <stdio.h>
#include <string.h>
#include <errno.h>

extern char *
decode_esym_r(char *buf, size_t bufsz, int err);

/*
 * Use this instead of perror().
 *
 * We use our own function, rather than perror(),
 * for these reasons:
 *
 *   svc_perror() uses a given errno-like value, rather than using
 *   the "global", @var{errno}.
 *
 *   svc_perror() shows not only the error description,
 *   but symbolic value, as well.
 */

void
svc_perror(int err, const char *s)
{
    char estrbuf[100];
    char *ep;
    char esymbuf[32];

    ep = strerror_r(err, estrbuf, sizeof (estrbuf));
    (void)decode_esym_r(esymbuf, sizeof (esymbuf), err);
    fprintf(stderr, "%s: %d=%s='%s'\n", s, err, esymbuf, ep);
}
