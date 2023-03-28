CC=gcc
CFLAGS=-Wall -g -pedantic
LDLIBS=
ALL=test

all: $(ALL)

test: rl_lock_library.o

rl_lock_library.o: rl_lock_library.c rl_lock_library.h

clean:
	rm -rf $(ALL) *~ *.o
