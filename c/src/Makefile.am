GENGETOPT = @gengetopt@


AM_CFLAGS = -Wall -ggdb

bin_PROGRAMS = usbnarf

usbnarf_SOURCES =  options.c main.c

include_HEADERS = options.h debugutil.h usbnarf.h
usbnarf_LDFLAGS = 

usbnarf_LDADD = $(USBNARF_LIBS)

dist_noinst_DATA=options.ggo 

options.c: options.ggo
options.h: options.ggo

#ifneq ($(GENGETOPT),"false")
.ggo.c:
	$(GENGETOPT) -F ${@:%.c=%} < $<
	LANG=C sed -i -e 's/..&@//g' $@ ${@:%.c=%.h}
#endif
