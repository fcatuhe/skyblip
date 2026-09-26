# Host unit-test + simulator build. Depends on nothing but a C++ compiler.
#
#   make test        build + run all host doctest suites
#   make test-sanitize  the same suites under ASan + UBSan, in build/sanitize/
#   make fuzz        every libFuzzer harness for FUZZ_SECONDS, in build/fuzz/ (clang)
#   make fuzz-nmea   one of them: nmea, air or link
#   make ... ABI=ilp32  any of the above for i386, where long is 32 bits as on the nRF52
#   make tidy        clang-tidy the sources the host build compiles
#   make simulator   terminal simulator
#   make browser     compile the WASM the browser page drives
#   make watch       incremental rebuild on save (WATCH=browser|simulator|test)
#
# The page itself and its dev server live in ../simulator: `make -C ../simulator
# dev`. This file compiles; it does not serve.
#   make clean
#
# core/ and ui/ must compile with NO framework headers. The device image is built
# by Zephyr/west, not here.

# 155 translation units and no ccache: serial, a full rebuild is five minutes.
MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4)

CXX      ?= c++
# -Wconversion is intentionally OFF: fixed-point bit-math (varint/adsl) truncates
# by design. Keep the high-signal warnings on and treat them as errors.
# -MMD -MP: emit header dependency files so editing a shared header (e.g. a
# device model's struct) recompiles every TU that includes it. Without this, stale
# objects with mismatched layouts link and crash non-deterministically.
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wshadow -Werror \
            -fno-exceptions -fno-rtti -I. -Ivendor -MMD -MP \
            -DDOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
LDFLAGS  :=

# GCC's undefined leaves out float-cast-overflow, clang's includes it.
SANITIZE_FLAGS := -fsanitize=address,undefined,float-cast-overflow \
                  -fno-sanitize-recover=all -fno-omit-frame-pointer

# One switch for the flags and the tree, so instrumented objects never mix with plain ones.
BUILD := build

# The nRF52 is ILP32 with an unsigned char: ABI=ilp32 builds for i386 with both, in its own trees.
ifeq ($(ABI),ilp32)
ABI_SUFFIX := -ilp32
BUILD := build/ilp32
CXXFLAGS += -m32 -funsigned-char
endif

ifdef SANITIZE
BUILD := build/sanitize$(ABI_SUFFIX)
CXXFLAGS += $(SANITIZE_FLAGS)
endif

# libFuzzer ships with clang only, GCC has no -fsanitize=fuzzer.
FUZZ_CXX ?= clang++
FUZZ_SECONDS ?= 60
FUZZ_BUILD := build/fuzz$(ABI_SUFFIX)
ifdef FUZZ
BUILD := $(FUZZ_BUILD)
CXX := $(FUZZ_CXX)
CXXFLAGS += $(SANITIZE_FLAGS) -fsanitize=fuzzer-no-link
endif

# Every layer is globbed: a file that exists is a file that is compiled and
# tested. Zephyr-only sources are inert on the host (guarded by __ZEPHYR__) and
# the silicon platform + product entry point are excluded outright.
SRC := $(filter-out hardware/platform/zephyr/%,$(wildcard core/*/*.cpp ui/*.cpp ui/*/*.cpp \
       hardware/parts/*/*.cpp runtime/*.cpp products/*/*.cpp products/*/pages/*.cpp products/*/services/*.cpp \
       simulator/world/*.cpp))
SRC := $(filter-out products/skyblip_go/main.cpp %/test_%.cpp,$(SRC))

# Test sources (mirrored tree): one folder per layer, tests never live beside
# the code they test.
TEST_SRC := $(filter-out test/fuzz/%,$(wildcard test/*.cpp test/*/*.cpp test/*/*/*.cpp))

# One harness per file, each linked against every host source as the suite is.
FUZZ_SRC := $(wildcard test/fuzz/fuzz_*.cpp)
FUZZ_NAMES := $(patsubst test/fuzz/fuzz_%.cpp,%,$(FUZZ_SRC))
FUZZ_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(FUZZ_SRC))
FUZZERS := $(patsubst test/fuzz/%.cpp,$(BUILD)/%,$(FUZZ_SRC))

OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) $(TEST_SRC))

BIN := $(BUILD)/skyblip_tests

# The simulator: the same product, on the host platform, inside simulator/world.
SIMULATOR_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) simulator/frontends/terminal.cpp)
SIMULATOR_BIN := $(BUILD)/skyblip_simulator

EMCC ?= emcc
# Linking C++ objects with emcc leaves operator new/delete undefined.
EMXX ?= em++
WEB_SRC := $(SRC) simulator/frontends/browser.cpp
WEB_DIST := $(BUILD)/browser
WEB_OUT := $(WEB_DIST)/skyblip_simulator.js
# Incremental browser build: compile to objects under build/web/ and link, so
# editing one file rebuilds one file (a single emcc pass over all sources costs
# ~30 s every save; this makes the edit->reload loop a couple of seconds).
WEB_BUILD := $(BUILD)/web
WEB_OBJ := $(patsubst %.cpp,$(WEB_BUILD)/%.o,$(WEB_SRC))
WEB_CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wshadow -Werror -I. -Ivendor -fno-exceptions -fno-rtti -MMD -MP

.PHONY: test test-sanitize fuzz fuzzers all clean simulator browser watch tidy
all: test

test: $(BIN)
	$(BIN)

test-sanitize:
	@UBSAN_OPTIONS=print_stacktrace=1 $(MAKE) --no-print-directory test SANITIZE=1

# Built once, then run one at a time: three fuzzers printing at once is a log nobody can read.
fuzz:
	@$(MAKE) --no-print-directory fuzzers FUZZ=1
	@$(MAKE) --no-print-directory -j1 $(addprefix fuzz-,$(FUZZ_NAMES))

fuzzers: $(FUZZERS)

# New inputs land in build/, the checked-in seeds and dictionary are only read.
fuzz-%:
	@$(MAKE) --no-print-directory $(FUZZ_BUILD)/fuzz_$* FUZZ=1
	@mkdir -p $(FUZZ_BUILD)/corpus/$* $(FUZZ_BUILD)/crashes
	UBSAN_OPTIONS=print_stacktrace=1 $(FUZZ_BUILD)/fuzz_$* -max_total_time=$(FUZZ_SECONDS) \
	  -artifact_prefix=$(FUZZ_BUILD)/crashes/$*- \
	  $(addprefix -dict=,$(wildcard test/fuzz/$*.dict)) \
	  $(FUZZ_BUILD)/corpus/$* $(wildcard test/fuzz/corpus/$*)

$(BUILD)/fuzz_%: $(BUILD)/test/fuzz/fuzz_%.o $(patsubst %.cpp,$(BUILD)/%.o,$(SRC))
	$(CXX) $(CXXFLAGS) -fsanitize=fuzzer $^ -o $@ $(LDFLAGS)

# Same $(SRC) the host build compiles, so no workflow ever restates the list, and
# the same flags less the ones that write dependency files. clang-tidy cannot
# guess the macOS SDK; without a stdlib it exits 1 rather than passing silently.
TIDY      ?= clang-tidy
TIDY_SDK  := $(shell xcrun --show-sdk-path 2>/dev/null)
TIDY_ARGS := $(filter-out -MMD -MP -Werror,$(CXXFLAGS)) $(if $(TIDY_SDK),-isysroot $(TIDY_SDK))

tidy:
	$(TIDY) --quiet $(SRC) -- $(TIDY_ARGS)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LDFLAGS)

simulator: $(SIMULATOR_BIN)
	@echo "run: ./$(SIMULATOR_BIN)"

$(SIMULATOR_BIN): $(SIMULATOR_OBJ)
	$(CXX) $(CXXFLAGS) $(SIMULATOR_OBJ) -o $@ $(LDFLAGS)

# WASM build. Requires Emscripten (https://emscripten.org). The page that loads
# it lives in ../simulator, which is also what serves it.
browser:
	@command -v $(EMCC) >/dev/null 2>&1 || { \
	  echo "emcc not found. Install Emscripten, then: make browser"; exit 1; }
	@$(MAKE) --no-print-directory $(WEB_OUT) $(WEB_DIST)/embed.js

$(WEB_BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(EMCC) $(WEB_CXXFLAGS) -c $< -o $@

$(WEB_OUT): $(WEB_OBJ)
	@mkdir -p $(WEB_DIST)
	$(EMXX) -O2 $(WEB_OBJ) \
	  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8 \
	  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0 \
	  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web \
	  -o $(WEB_OUT)
	@echo "built $(WEB_DIST)/, serve it with: make -C ../simulator dev"

# Its own target: copied by the link recipe, an edit to it alone shipped nothing.
$(WEB_DIST)/embed.js: simulator/web/embed.js
	@mkdir -p $(WEB_DIST)
	cp $< $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# dev loop
# C++ has no hot-module-reload: a change always means a recompile. These make
# the loop automatic instead of manual.
#
#   make watch            rebuild + rerun host tests on every save (default)
#   make watch WATCH=browser    recompile the WASM on every save
#   make watch WATCH=simulator  rebuild the terminal simulator on every save
#
WATCH ?= test
WATCH_SRC := core ui ports hardware boards runtime products simulator test

watch:
	@echo "watching (target: $(WATCH)), Ctrl-C to stop"
	@if command -v watchexec >/dev/null 2>&1; then \
	  watchexec -e cpp,h,html --debounce 400ms -- $(MAKE) --no-print-directory $(WATCH); \
	else \
	  last=""; \
	  while true; do \
	    cur=$$(find $(WATCH_SRC) \( -name '*.cpp' -o -name '*.h' -o -name '*.html' \) \
	           -exec ls -l {} + 2>/dev/null | cksum); \
	    if [ "$$cur" != "$$last" ]; then \
	      last="$$cur"; \
	      printf '\n=== %s: rebuilding ===\n' "$$(date +%H:%M:%S)"; \
	      $(MAKE) --no-print-directory $(WATCH) || true; \
	    fi; \
	    sleep 1; \
	  done; \
	fi

clean:
	rm -rf $(BUILD)

# Header dependencies for ALL THREE object trees. Without the web half, editing
# a .h left the stale .o in place and the browser kept running old code; without
# the simulator half, the terminal frontend was the one translation unit no
# other target compiles, so renaming a driver method left it linking against a
# symbol that no longer existed.
-include $(OBJ:.o=.d)
-include $(FUZZ_OBJ:.o=.d)
-include $(SIMULATOR_OBJ:.o=.d)
-include $(WEB_OBJ:.o=.d)
