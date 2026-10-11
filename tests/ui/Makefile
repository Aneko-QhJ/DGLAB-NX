CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -g
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O2 -g
ROOT := ../..
.PHONY: all clean preview
all: build/test_input build/test_texture_lifecycle build/test_pages build/test_presentation
	./build/test_input
	./build/test_texture_lifecycle
	./build/test_pages
	./build/test_presentation
build/test_input: test_input.c $(ROOT)/nro/source/ui/input_state.c $(ROOT)/nro/include/dglab/ui/input_state.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(ROOT)/nro/include test_input.c $(ROOT)/nro/source/ui/input_state.c -o $@
build/test_texture_lifecycle: test_texture_lifecycle.cpp $(ROOT)/third_party/borealis/library/include/borealis/core/cache_helper.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(ROOT)/third_party/borealis/library/include $< -o $@
clean:
	rm -rf build

PAGE_SOURCES := $(ROOT)/nro/source/motion/motion_feed.c $(ROOT)/nro/source/motion/motion_settings.c $(ROOT)/nro/source/touch/touch_feed.c
build/test_pages: test_pages.c hostshim/switch.h $(ROOT)/nro/source/pages/controller.inc $(wildcard $(ROOT)/nro/include/dglab/ui/*.h) $(PAGE_SOURCES) $(ROOT)/nro/source/ui/input_state.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Ihostshim -I../net/hostshim -I$(ROOT)/nro/include -I$(ROOT)/common/include test_pages.c $(PAGE_SOURCES) $(ROOT)/nro/source/ui/input_state.c -lm -o $@
build/test_presentation: test_presentation.c $(ROOT)/nro/include/dglab/ui/presentation.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(ROOT)/nro/include $< -lm -o $@

# Optional macOS renderer of the actual framework tree; supply a local CJK font.
preview:
	@test -n "$(FONT)" || { echo 'Supply FONT=<path-to-CJK-font>'; exit 1; }
	cmake -S preview -B build/preview -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5
	cmake --build build/preview --parallel 8
	./build/preview/preview "$(abspath $(ROOT))" "$(FONT)" "$(abspath build/captures)"
