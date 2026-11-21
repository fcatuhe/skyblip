# Host test and simulator build, with nothing but a C++ compiler: README.md lists the targets.

# One job per core, as no ccache keeps objects between trees.
MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4)

CXX      ?= c++
# -Wconversion stays off for the fixed-point bit math, and -MMD -MP rebuild every object a header touches.
CXXFLAGS := -std=c++20 -O1 -g -Wall -Wextra -Wshadow -Werror \
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
# INFO: fc 26sep26 fuzz_nmea runs about 20000 inputs a second and fuzz_link 300, so link gets the minutes
FUZZ_SECONDS_nmea ?= 30
FUZZ_SECONDS_air ?= 60
FUZZ_SECONDS_adsb ?= 30
FUZZ_SECONDS_adsb_assembly ?= 30
FUZZ_SECONDS_link ?= 90
FUZZ_BUILD := build/fuzz$(ABI_SUFFIX)
ifdef FUZZ
BUILD := $(FUZZ_BUILD)
CXX := $(FUZZ_CXX)
CXXFLAGS += $(SANITIZE_FLAGS) -fsanitize=fuzzer-no-link
endif

# Every layer is globbed, so a file that exists is compiled and tested, the silicon platform and main.cpp aside.
SRC := $(filter-out hardware/platform/zephyr/%,$(wildcard protocols/*/*.cpp core/*/*.cpp ui/*.cpp ui/*/*.cpp \
       hardware/parts/*/*.cpp runtime/*.cpp products/*/*.cpp products/*/pages/*.cpp products/*/services/*.cpp \
       simulator/world/*.cpp))
SRC := $(filter-out products/%/main.cpp %/test_%.cpp,$(SRC))

# Monocypher is C that also compiles as C++: one compiler builds and instruments it with the rest.
VENDOR_SRC := $(wildcard vendor/monocypher/*.c)
VENDOR_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(VENDOR_SRC))

# Tests mirror the tree in test/, never beside the code they test, except each protocol stack's own.
TEST_SRC := $(filter-out test/fuzz/%,$(wildcard test/*.cpp test/*/*.cpp test/*/*/*.cpp)) \
            $(wildcard protocols/*/test/*.cpp protocols/*/test/*/*.cpp)

# One harness per file, each linked against every host source as the suite is.
FUZZ_SRC := $(wildcard test/fuzz/fuzz_*.cpp)
FUZZ_NAMES := $(patsubst test/fuzz/fuzz_%.cpp,%,$(FUZZ_SRC))
FUZZ_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(FUZZ_SRC))
FUZZERS := $(patsubst test/fuzz/%.cpp,$(BUILD)/%,$(FUZZ_SRC))

OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) $(TEST_SRC)) $(VENDOR_OBJ)

BIN := $(BUILD)/skyblip_tests

EMCC ?= emcc
# Linking C++ objects with emcc leaves operator new/delete undefined.
EMXX ?= em++
WEB_SRC := $(SRC) simulator/frontends/browser.cpp
WEB_DIST := $(BUILD)/browser
WEB_OUT := $(WEB_DIST)/skyblip_simulator.js
# Objects under build/web/, so a save rebuilds one file in seconds rather than every source in 30 s.
WEB_BUILD := $(BUILD)/web
WEB_STAMP := $(BUILD)/web/emsdk-version
WEB_OBJ := $(patsubst %.cpp,$(WEB_BUILD)/%.o,$(WEB_SRC)) $(patsubst %.c,$(WEB_BUILD)/%.o,$(VENDOR_SRC))
WEB_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror -I. -Ivendor -fno-exceptions -fno-rtti -MMD -MP

.PHONY: test test-sanitize test-protocols fuzz fuzzers all clean browser watch tidy
all: test

test: $(BIN)
	$(BIN)

test-sanitize:
	@UBSAN_OPTIONS=print_stacktrace=1 $(MAKE) --no-print-directory test SANITIZE=1

# Each stack is built alone with protocols/common, doctest and any stack it extends, so a stray include fails.
PROTOCOLS_BUILD := $(BUILD)/protocols-alone
PROTOCOL_STACKS := $(filter-out common,$(patsubst protocols/%/,%,$(wildcard protocols/*/)))
# The stacks promise C++17 to the project that takes one (protocols/README.md), so they are built as it.
PROTOCOLS_CXXFLAGS := $(filter-out -std=c++20 -I. -Ivendor -MMD -MP,$(CXXFLAGS)) -std=c++17
PROTOCOL_EXTENDS_adsl_edge := adsl

test-protocols: $(addprefix test-protocol-,$(PROTOCOL_STACKS))

test-protocol-%:
	@rm -rf $(PROTOCOLS_BUILD)/$*
	@mkdir -p $(PROTOCOLS_BUILD)/$*/tree/protocols $(PROTOCOLS_BUILD)/$*/tree/vendor
	cp -R protocols/common protocols/$* $(addprefix protocols/,$(PROTOCOL_EXTENDS_$*)) \
	  $(PROTOCOLS_BUILD)/$*/tree/protocols/
	$(foreach extended,$(PROTOCOL_EXTENDS_$*),rm -rf $(PROTOCOLS_BUILD)/$*/tree/protocols/$(extended)/test;)
	cp -R vendor/doctest $(PROTOCOLS_BUILD)/$*/tree/vendor/
	printf '#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include "doctest/doctest.h"\n' > $(PROTOCOLS_BUILD)/$*/main.cpp
	cd $(PROTOCOLS_BUILD)/$*/tree && $(CXX) $(PROTOCOLS_CXXFLAGS) -I. -Ivendor \
	  $$(find protocols -name '*.cpp' | sort) ../main.cpp -o ../$*_tests
	$(PROTOCOLS_BUILD)/$*/$*_tests

# Built once, then run one at a time: three fuzzers printing at once is a log nobody can read.
fuzz:
	@$(MAKE) --no-print-directory fuzzers FUZZ=1
	@$(MAKE) --no-print-directory -j1 $(addprefix fuzz-,$(FUZZ_NAMES))

fuzzers: $(FUZZERS)

# New inputs land in build/, the checked-in seeds and dictionary are only read.
fuzz-%:
	@$(MAKE) --no-print-directory $(FUZZ_BUILD)/fuzz_$* FUZZ=1
	@mkdir -p $(FUZZ_BUILD)/corpus/$* $(FUZZ_BUILD)/crashes
	UBSAN_OPTIONS=print_stacktrace=1 $(FUZZ_BUILD)/fuzz_$* -max_total_time=$(or $(FUZZ_SECONDS),$(FUZZ_SECONDS_$*)) \
	  -artifact_prefix=$(FUZZ_BUILD)/crashes/$*- \
	  $(addprefix -dict=,$(wildcard test/fuzz/$*.dict)) \
	  $(FUZZ_BUILD)/corpus/$* $(wildcard test/fuzz/corpus/$*)

$(BUILD)/fuzz_%: $(BUILD)/test/fuzz/fuzz_%.o $(patsubst %.cpp,$(BUILD)/%.o,$(SRC)) $(VENDOR_OBJ)
	$(CXX) $(CXXFLAGS) -fsanitize=fuzzer $^ -o $@ $(LDFLAGS)

# The host build's sources and flags, with the macOS SDK named, because clang-tidy cannot guess it.
TIDY      ?= clang-tidy
TIDY_SDK  := $(shell xcrun --show-sdk-path 2>/dev/null)
TIDY_ARGS := $(filter-out -MMD -MP -Werror,$(CXXFLAGS)) $(if $(TIDY_SDK),-isysroot $(TIDY_SDK))

tidy:
	$(TIDY) --quiet $(SRC) -- $(TIDY_ARGS)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LDFLAGS)

# The WASM, built with the Emscripten version .github/pins/action.yml pins, which ../scripts/emsdk.rb installs.
browser:
	@pinned=$$(ruby ../scripts/emsdk.rb "$(EMCC)") && set -- $$pinned && mkdir -p $(dir $(WEB_STAMP)) && \
	  { echo "$$1" | cmp -s - $(WEB_STAMP) || echo "$$1" > $(WEB_STAMP); } && \
	  $(MAKE) --no-print-directory $(WEB_OUT) $(WEB_DIST)/embed.js $(WEB_DIST)/scenarios.json \
	    EMCC="$$2" EMXX="$${2%emcc}em++" WEB_BUILD="$(BUILD)/web/$$1"

$(WEB_BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(EMCC) $(WEB_CXXFLAGS) -c $< -o $@

$(WEB_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(EMCC) $(WEB_CXXFLAGS) -x c++ -c $< -o $@

$(WEB_OUT): $(WEB_OBJ) $(WEB_STAMP)
	@mkdir -p $(WEB_DIST)
	$(EMXX) -O2 $(WEB_OBJ) \
	  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8 \
	  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0 \
	  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web \
	  -o $(WEB_OUT)
	@echo "built $(WEB_DIST)/"

# Its own target: copied by the link recipe, an edit to it alone shipped nothing.
$(WEB_DIST)/embed.js: simulator/web/embed.js
	@mkdir -p $(WEB_DIST)
	cp $< $@

# Each release carries the scenarios beside the WASM, as one file.
$(WEB_DIST)/scenarios.json: $(wildcard scenarios/*.json) ../lib/simulator_scenarios.rb
	@mkdir -p $(WEB_DIST)
	ruby ../scripts/simulator_scenarios.rb scenarios > $@.tmp && mv $@.tmp $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

# Rebuilds on every save, the host tests by default and the WASM with WATCH=browser. Needs watchexec.
WATCH ?= test
WATCH_SRC := protocols core ui ports hardware boards runtime products simulator scenarios test vendor

watch:
	watchexec $(addprefix -w ,$(WATCH_SRC)) -e cpp,h,c,js,json --debounce 400ms -- $(MAKE) --no-print-directory $(WATCH)

clean:
	rm -rf $(BUILD)

# Header dependencies for every object tree, the browser's included.
-include $(OBJ:.o=.d)
-include $(FUZZ_OBJ:.o=.d)
-include $(WEB_OBJ:.o=.d)
