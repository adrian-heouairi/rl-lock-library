CC=gcc
CFLAGS=-Wall -g -pedantic -std=c11
LDLIBS=-pthread -lrt

all: rl_lock_library.o compile_tests

doc:
	doxygen

rl_lock_library.o: rl_lock_library.c rl_lock_library.h

test_files := $(shell find . ! -name rl_lock_library.c -name "*.c")

compile_tests: rl_lock_library.o
	for i in $(test_files); do \
		$(CC) $(CFLAGS) -o $${i%.c}.test $$i rl_lock_library.o $(LDLIBS); done

test: compile_tests
	./tests.sh

clean:
	rm -rf *~ *.o *.test

cleandoc:
	rm -rf doc
