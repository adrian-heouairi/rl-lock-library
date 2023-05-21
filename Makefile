CC=gcc
CFLAGS=-Wall -g -pedantic -std=c11
LDLIBS=-pthread -lrt
ALL=lib tests

all: $(ALL)

doc:
	doxygen

lib: rl_lock_library.o

rl_lock_library.o: rl_lock_library.c rl_lock_library.h

test_count_to_200000.o: test_count_to_200000.c rl_lock_library.h rl_lock_library.o

test_count_to_200000: rl_lock_library.o test_count_to_200000.o

tests: test_count_to_200000

clean:
	rm -rf *~ *.o

cleandoc:
	rm -rf doc
