CC=gcc
CFLAGS=-Wall -g -pedantic -std=c11
LDLIBS=-pthread -lrt
ALL=test

all: $(ALL)

doc:
	doxygen

test: rl_lock_library.o

rl_lock_library.o: rl_lock_library.c rl_lock_library.h

clean:
	rm -rf *~ *.o doc
