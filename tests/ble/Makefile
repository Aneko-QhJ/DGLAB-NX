CC ?= cc
DEVKITPRO ?= /opt/devkitpro
CFLAGS ?= -std=c11 -O2 -g
CFLAGS += -Wall -Wextra -Werror -I../../sysmodule/include -I$(DEVKITPRO)/libnx/include
.PHONY: all clean
all: build/test_preparation
	./build/test_preparation
build/test_preparation: test_preparation.c ../../sysmodule/include/dglab/transport/ble_prepare.h
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@
clean:
	rm -rf build
