# Cortex Filter: a CLAP 3D morphing filter for the Akai MPC Sample (64-bit ARM Linux, Cortex-A55).
# Build inside a Linux arm64 image with ./build.sh, which runs `make all test`.
#   make            build/CortexFilter.clap
#   make test       the tests against the built plug-in (tests/host.cpp), the engine's own (tests/engine.cpp) and the
#                   sound's fingerprint (tests/fingerprint.cpp: the output to the bit, against tests/fingerprint.txt)
#   make bench      build/bench: a busy setting; tools/bench.sh counts its instructions
#   make sanitize   the tests and fingerprint scenarios under AddressSanitizer and UBSan (build/asan)
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

.PHONY: all test bench clean fingerprint-update sanitize
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

# The tests and every fingerprint scenario under AddressSanitizer and UBSan (build/asan): any read past a buffer,
# leak or undefined behaviour stops it — the state and bank parser is what it chiefly covers. The fingerprints'
# hashes hold only for the release flags, so a mismatch there (exit 1) is expected; a sanitizer's report aborts
# instead (abort_on_error for both, so UBSan's is not an exit 1).
SAN := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g1
sanitize:
	@mkdir -p $(OUT)/asan
	$(CXX) $(subst -O3,-O1,$(CXXFLAGS)) $(SAN) $(SOURCES) -shared -Wl,--version-script=src/exports.map -o $(OUT)/asan/CortexFilter.clap
	$(CXX) -std=c++17 -O1 $(SAN) -Ithird_party/clap/include tests/host.cpp -ldl -o $(OUT)/asan/host
	$(CXX) -std=c++17 $(subst -O3,-O1,$(OPT)) $(WARN) $(SAN) -Ithird_party/clap/include -Isrc tests/engine.cpp -o $(OUT)/asan/engine-test
	$(CXX) -std=c++17 -O1 $(SAN) -Ithird_party/clap/include tests/fingerprint.cpp -ldl -o $(OUT)/asan/fingerprint
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 $(OUT)/asan/engine-test
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 $(OUT)/asan/host $(OUT)/asan/CortexFilter.clap $(OUT)/asan
	@ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 \
	    $(OUT)/asan/fingerprint $(OUT)/asan/CortexFilter.clap tests/fingerprint.txt > $(OUT)/asan/fingerprint.log 2>&1; \
	    rc=$$?; if [ $$rc -gt 1 ]; then cat $(OUT)/asan/fingerprint.log; exit $$rc; fi; echo "sanitize: every fingerprint scenario ran clean"

clean:
	rm -rf $(OUT)

# SP-16 device build with the freesp16 toolchain. The desktop host loads the regular Linux build.
SP16_CXX ?= arm-linux-gnueabi-g++
.PHONY: sp16
sp16: $(OUT)/sp16/CortexFilter.clap
$(OUT)/sp16/CortexFilter.clap: $(SOURCES) $(HEADERS) src/exports.map Makefile
	@mkdir -p $(@D)
	$(SP16_CXX) $(filter-out -mcpu=cortex-a55,$(CXXFLAGS)) -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=softfp $(SOURCES) $(LDFLAGS) -o $@
