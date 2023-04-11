CC=gcc
CFLAGS=-Wall -g -pedantic -std=c11
LDLIBS=-pthread
ALL=test

all: $(ALL)

test: rl_lock_library.o

rl_lock_library.o: rl_lock_library.c rl_lock_library.h

clean:
	rm -rf *~ *.o
