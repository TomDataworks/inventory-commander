CFLAGS=-g -std=c99 -lcurses -lsqlite3
all: invc size
clean:
	rm invc || true
	rm size || true
