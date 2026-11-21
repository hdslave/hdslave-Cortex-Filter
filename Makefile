# Cortex Filter: a CLAP 3D morphing filter for the Akai MPC Sample (64-bit ARM Linux, Cortex-A55).
# Build inside a Linux arm64 image with ./build.sh, which runs `make all test`.
#   make            build/CortexFilter.clap
#   make test       the tests against the built plug-in (tests/host.cpp), the engine's own (tests/engine.cpp) and the
#                   sound's fingerprint (tests/fingerprint.cpp: the output to the bit, against tests/fingerprint.txt)
#   make bench      build/bench: a busy setting; tools/bench.sh counts its instructions
CXX ?= g++
OUT := build
WARN := -Wall -Wextra -Werror
# The MPC's cores. -ffp-contract=fast lets the compiler fuse multiply-adds; nothing here relies on IEEE errno,
# traps or exceptions.
OPT := -O3 -mcpu=cortex-a55 -ffp-contract=fast -fno-math-errno -fno-trapping-math
CXXFLAGS := -std=c++17 $(OPT) $(WARN) -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -Ithird_party/clap/include -Isrc
# Only clap_entry is exported; libstdc++ and libgcc are linked in so the MPC needs nothing beyond libc and libm. -s
# leaves out the symbol table (the code is the same; the exported clap_entry stays).
LDFLAGS := -shared -s -static-libgcc -static-libstdc++ -Wl,--gc-sections -Wl,-z,now -Wl,-O1 -Wl,--version-script=src/exports.map

SOURCES := src/cortex.cpp
HEADERS := $(wildcard src/*.h src/ui/*.h)
TESTFLAGS := -std=c++17 -O2 $(WARN) -Ithird_party/clap/include

.PHONY: all test bench clean fingerprint-update
all: $(OUT)/CortexFilter.clap

$(OUT)/CortexFilter.clap: $(SOURCES) $(HEADERS) src/exports.map Makefile
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(OUT)/host: tests/host.cpp tests/wav.h tests/png.h tests/signal.h $(HEADERS) Makefile
	@mkdir -p $(OUT)
	$(CXX) $(TESTFLAGS) tests/host.cpp -ldl -o $@

$(OUT)/engine-test: tests/engine.cpp tests/signal.h $(HEADERS) Makefile
	@mkdir -p $(OUT)
	$(CXX) $(TESTFLAGS) $(OPT) -Isrc tests/engine.cpp -o $@ # the shipped maths: its to-the-bit checks hold for it

$(OUT)/fingerprint: tests/fingerprint.cpp tests/signal.h Makefile
	@mkdir -p $(OUT)
	$(CXX) $(TESTFLAGS) tests/fingerprint.cpp -ldl -o $@

test: $(OUT)/CortexFilter.clap $(OUT)/host $(OUT)/engine-test $(OUT)/fingerprint
	$(OUT)/engine-test
	$(OUT)/host $(OUT)/CortexFilter.clap $(OUT)
	$(OUT)/fingerprint $(OUT)/CortexFilter.clap tests/fingerprint.txt > /dev/null

# The sound's hashes after a change meant to change the sound (see tests/fingerprint.cpp).
fingerprint-update: $(OUT)/CortexFilter.clap $(OUT)/fingerprint
	$(OUT)/fingerprint $(OUT)/CortexFilter.clap > tests/fingerprint.txt

$(OUT)/bench: tests/bench.cpp tests/signal.h $(SOURCES) $(HEADERS) Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 $(OPT) $(WARN) -g -Ithird_party/clap/include -Isrc tests/bench.cpp -o $@

bench: $(OUT)/bench

clean:
	rm -rf $(OUT)
