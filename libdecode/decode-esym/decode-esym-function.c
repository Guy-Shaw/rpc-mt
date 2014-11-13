
char *
decode_esym_r(char *buf, size_t bufsz, int err)
{
    char number_buf[10];
    char *sym;

    sym = NULL;
    if (err >= 0 && err < errno_table_size) {
        sym = errno_table[err];
    }

    if (sym == NULL) {
        sprintf(number_buf, "#%d", err);
        sym = number_buf;
    }
    append_buf(buf, bufsz, buf, sym);
    return (buf);
}
