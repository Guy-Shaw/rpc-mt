
char *
decode_esym_r(char *buf, size_t bufsz, int ierr)
{
    const char *sym;

    sym = NULL;
    if (ierr >= 0 && (size_t)ierr < errno_table_size) {
        sym = errno_table[(size_t)ierr];
    }

    if (sym) {
        append_buf(buf, bufsz, buf, sym);
    }
    else {
        snprintf(buf, bufsz, "#%d", ierr);
    }
    return (buf);
}
