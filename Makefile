CC      = cc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L \
          $(shell cups-config --cflags)
CUPSLIBS  = $(shell cups-config --libs) -lcupsimage
CURLCFLAGS = $(shell pkg-config --cflags libcurl)
CURLLIBS   = $(shell pkg-config --libs libcurl)

SRC = src

FILTER      = rastertotmt20iv
BACKEND     = epos

# Install locations. DESTDIR is honoured so debhelper can stage into debian/<pkg>/.
DESTDIR        ?=
prefix         ?= /usr
CUPSFILTERDIR  ?= $(prefix)/lib/cups/filter
CUPSBACKENDDIR ?= $(prefix)/lib/cups/backend
PPDDIR         ?= $(prefix)/share/ppd/epsont20iv

.PHONY: all clean test install uninstall

all: $(FILTER) $(BACKEND)

$(FILTER): $(SRC)/rastertotmt20iv.c $(SRC)/raster.c $(SRC)/buffer.c
	$(CC) $(CFLAGS) -o $@ $(SRC)/rastertotmt20iv.c $(SRC)/raster.c $(SRC)/buffer.c $(CUPSLIBS)

$(BACKEND): $(SRC)/epos_backend.c $(SRC)/epos.c $(SRC)/http.c $(SRC)/config.c $(SRC)/log.c $(SRC)/buffer.c $(SRC)/status.c
	$(CC) $(CFLAGS) $(CURLCFLAGS) -o $@ \
	    $(SRC)/epos_backend.c $(SRC)/epos.c $(SRC)/http.c $(SRC)/config.c $(SRC)/log.c $(SRC)/buffer.c $(SRC)/status.c \
	    $(shell cups-config --libs) $(CURLLIBS)

# Unit tests for the pure-logic modules (no CUPS/curl deps). Runs anywhere.
test: tests/test_status tests/test_raster
	./tests/test_status
	./tests/test_raster

tests/test_status: tests/test_status.c $(SRC)/status.c
	$(CC) $(CFLAGS) -I$(SRC) -o $@ tests/test_status.c $(SRC)/status.c

tests/test_raster: tests/test_raster.c $(SRC)/raster.c $(SRC)/buffer.c
	$(CC) $(CFLAGS) -I$(SRC) -o $@ tests/test_raster.c $(SRC)/raster.c $(SRC)/buffer.c

# Stage the driver. The backend is 0700 because CUPS then runs it as root; the
# .deb re-applies this after dh_fixperms (see debian/rules).
install: all
	install -d $(DESTDIR)$(CUPSFILTERDIR) $(DESTDIR)$(CUPSBACKENDDIR) $(DESTDIR)$(PPDDIR)
	install -m 0755 $(FILTER) $(DESTDIR)$(CUPSFILTERDIR)/$(FILTER)
	install -m 0700 $(BACKEND) $(DESTDIR)$(CUPSBACKENDDIR)/$(BACKEND)
	install -m 0644 ppd/tmt20iv.ppd $(DESTDIR)$(PPDDIR)/tmt20iv.ppd

uninstall:
	rm -f $(DESTDIR)$(CUPSFILTERDIR)/$(FILTER)
	rm -f $(DESTDIR)$(CUPSBACKENDDIR)/$(BACKEND)
	rm -f $(DESTDIR)$(PPDDIR)/tmt20iv.ppd

clean:
	rm -f $(FILTER) $(BACKEND) tests/test_status tests/test_raster
