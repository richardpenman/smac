# Convenience wrapper around CMake for building and playing smacx (macOS, Linux, Windows/MSYS2).
#
#   make            configure (first time) and build smacx, smacx_headless and the tests
#   make run        build, then start the game (it finds the game files or asks for them)
#   make test       build, then run the unit and smoke tests
#   make clean      remove the build directory
#
# Variables: BUILD=dir (default build/native), JOBS=n, CMAKE_ARGS="..." (extra configure options).
# CMake 3.20+ and a C++17 compiler are needed; smacx also needs SDL2 and SDL2_ttf
# (macOS: brew install cmake sdl2 sdl2_ttf; Debian/Ubuntu: apt install cmake libsdl2-dev libsdl2-ttf-dev).

BUILD ?= build/native
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
CMAKE ?= cmake
CXX_TEST = printf '\#include <cstdint>\nint main(){}\n' | $${CXX:-c++} -x c++ - -o /dev/null 2>/dev/null

# Some macOS Command Line Tools betas cannot find their own C++ headers (<cstdint> not found).
# If a trivial C++ program does not compile, use the SDK's headers explicitly.
SDK_CXX = $(shell xcrun --show-sdk-path 2>/dev/null)/usr/include/c++/v1
FIX_FLAGS = $(shell if [ "$$(uname)" = Darwin ] && ! $(CXX_TEST) && [ -d "$(SDK_CXX)" ]; then \
                echo "-DCMAKE_CXX_FLAGS=-nostdinc++ -isystem $(SDK_CXX)"; fi)

.PHONY: all build run test clean

all: build

$(BUILD)/CMakeCache.txt:
	@if [ -n "$(FIX_FLAGS)" ]; then echo "make: using the SDK's C++ headers (this compiler can't find its own)"; fi
	$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=RelWithDebInfo $(if $(FIX_FLAGS),"$(FIX_FLAGS)") $(CMAKE_ARGS)

build: $(BUILD)/CMakeCache.txt
	$(CMAKE) --build $(BUILD) -j $(JOBS)
	@if [ ! -e $(BUILD)/smacx ] && [ ! -e $(BUILD)/smacx.exe ]; then \
	    echo "make: smacx was not built: install SDL2 and SDL2_ttf, then 'make clean && make'"; exit 1; fi

run: build
	$(BUILD)/smacx

test: build
	cd $(BUILD) && ctest --output-on-failure

clean:
	rm -rf $(BUILD)
