CC      = cc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L \
          $(shell cups-config --cflags)
CUPSLIBS  = $(shell cups-config --libs) -lcupsimage
CURLCFLAGS = $(shell pkg-config --cflags libcurl)
CURLLIBS   = $(shell pkg-config --libs libcurl)

SRC = src

FILTER      = rastertotmt20iv
BACKEND     = epos

.PHONY: all clean test

all: $(FILTER) $(BACKEND)

$(FILTER): $(SRC)/rastertotmt20iv.c $(SRC)/buffer.c
	$(CC) $(CFLAGS) -o $@ $(SRC)/rastertotmt20iv.c $(SRC)/buffer.c $(CUPSLIBS)

$(BACKEND): $(SRC)/epos_backend.c $(SRC)/epos.c $(SRC)/http.c $(SRC)/config.c $(SRC)/log.c $(SRC)/buffer.c $(SRC)/status.c
	$(CC) $(CFLAGS) $(CURLCFLAGS) -o $@ \
	    $(SRC)/epos_backend.c $(SRC)/epos.c $(SRC)/http.c $(SRC)/config.c $(SRC)/log.c $(SRC)/buffer.c $(SRC)/status.c \
	    $(shell cups-config --libs) $(CURLLIBS)

# Unit tests for the pure-logic modules (no CUPS/curl deps). Runs anywhere.
test: tests/test_status
	./tests/test_status

tests/test_status: tests/test_status.c $(SRC)/status.c
	$(CC) $(CFLAGS) -I$(SRC) -o $@ tests/test_status.c $(SRC)/status.c

clean:
	rm -f $(FILTER) $(BACKEND) tests/test_status
