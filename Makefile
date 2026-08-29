CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g

SRC   := allocator.c
DEMO  := main.c
TESTS := tests.c
DEPS  := allocator.h Makefile

.PHONY: all run test asan clean

all: allocator tests

allocator: $(DEMO) $(SRC) $(DEPS)
	$(CC) $(CFLAGS) $(DEMO) $(SRC) -o $@

tests: $(TESTS) $(SRC) $(DEPS)
	$(CC) $(CFLAGS) $(TESTS) $(SRC) -o $@

run: allocator
	./allocator

test: tests
	./tests

# Same suite under AddressSanitizer and UndefinedBehaviorSanitizer.
asan: $(TESTS) $(SRC) $(DEPS)
	$(CC) -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined \
	      $(TESTS) $(SRC) -o tests-asan
	./tests-asan

clean:
	rm -f allocator tests tests-asan
	rm -rf *.dSYM
