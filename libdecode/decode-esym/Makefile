
SRCS = decode-esym-prolog.c decode-esym-table.c decode-esym-function.c

.PHONY: all

all: decode-esym.c

decode-esym.c: $(SRCS)
	cat $(SRCS) > decode-esym.c

decode-esym-table.c:
	./make-errno-table --table='errno_table' \
		--sym-regex '\AE[A-Z0-9]+\z' \
		/usr/include/errno.h \
		> decode-esym-table.c

clean:
	rm -f cscope.out cscope.in.out cscope.po.out tags
	rm -f decode-esym.c decode-esym-table.c

distclean: clean
