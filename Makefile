CC=gcc
CFLAGS=-Wall -g -pedantic -std=c11
LDLIBS=-pthread -lrt
ALL=lib tests

all: $(ALL)

doc:
	doxygen

lib: rl_lock_library.o

rl_lock_library.o: rl_lock_library.c rl_lock_library.h

tests: lib
	$(CC) $(CFLAGS) -o tests unittests.c -lcunit -pthread

clean:
	rm -rf *~ *.o doc
