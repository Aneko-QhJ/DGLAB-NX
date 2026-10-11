CC ?= cc
CFLAGS ?= -std=c11 -O2 -g
CFLAGS += -Wall -Wextra -Werror -I../../nro/include
.PHONY: all clean
all: build/test_log_queue
	./build/test_log_queue
build/test_log_queue: test_log_queue.c ../../nro/include/dglab/util/log_queue.h
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@
clean:
	rm -rf build
