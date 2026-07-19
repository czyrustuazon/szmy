# Host-side unit tests for szmy (Unity + gcov/lcov).
# Requires a native GCC or Clang toolchain (MSYS2, WSL, Linux, or macOS).

PROJECT_ROOT := $(abspath ..)
BUILD_DIR    := build
REPORT_DIR   := coverage_html
VGM_MINIMP3  := $(PROJECT_ROOT)/vgmstream/src/coding/libs

# Parallel by default when invoked directly (inherits jobserver if parent used -j).
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
ifeq ($(filter -j%,$(MAKEFLAGS))$(filter --jobserver-%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS)
endif

# Always use a native host compiler. Parent `make`/`make cia` exports
# arm-none-eabi-gcc as CC; that must not win here.
HOST_CC ?= gcc
override CC := $(HOST_CC)

CFLAGS   := -std=c11 -Wall -Wextra -Werror -g -O0 \
            -DUNIT_TEST \
            -Wno-format-truncation \
            -fprofile-arcs -ftest-coverage -fno-inline \
            -I$(PROJECT_ROOT)/include \
            -Isupport \
            -Ithird_party/unity
LDFLAGS  := --coverage -pthread -lm

UNITY_SRC    := third_party/unity/unity.c
HOST_MOCKS   := support/host_mocks.c
HOST_DRFLAC  := support/host_drflac.c
HOST_OPUSFILE := support/host_opusfile.c

SRC_PROD := \
	$(PROJECT_ROOT)/source/id3_util.c \
	$(PROJECT_ROOT)/source/path_util.c \
	$(PROJECT_ROOT)/source/audio_errors.c \
	$(PROJECT_ROOT)/source/bmp_util.c \
	$(PROJECT_ROOT)/source/jptext_errors.c \
	$(PROJECT_ROOT)/source/audio_ctrl.c \
	$(PROJECT_ROOT)/source/file_magic.c \
	$(PROJECT_ROOT)/source/musiclist.c \
	$(PROJECT_ROOT)/source/pcm_ring.c \
	$(PROJECT_ROOT)/source/audio_viz.c \
	$(PROJECT_ROOT)/source/ftp_server.c \
	$(PROJECT_ROOT)/source/tap_detector.c

TEST_RUNNERS := \
	test_id3_util \
	test_path_util \
	test_audio_errors \
	test_bmp_util \
	test_jptext_errors \
	test_audio_ctrl \
	test_file_magic \
	test_musiclist \
	test_pcm_ring \
	test_audio_viz \
	test_ftp_server \
	test_tap_detector \
	test_mp3_player \
	test_flac_player \
	test_opus_player

# MSYS2/Cygwin gcc writes *.exe; invoking ./build/runner without it can fail.
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
else
UNAME_S := $(shell uname -s 2>/dev/null)
ifneq (,$(findstring MINGW,$(UNAME_S)))
EXEEXT := .exe
else ifneq (,$(findstring MSYS,$(UNAME_S)))
EXEEXT := .exe
else
EXEEXT :=
endif
endif

.PHONY: all test coverage coverage-html clean

all: test

test: $(addprefix $(BUILD_DIR)/,$(TEST_RUNNERS))
	@set -e; \
	for t in $(TEST_RUNNERS); do \
		echo "==> $$t"; \
		"$(BUILD_DIR)/$$t$(EXEEXT)"; \
	done

# Require full line + function coverage of production sources (branch is not gated).
COVERAGE_MIN_LINES := 100
COVERAGE_MIN_FUNCS := 100

coverage:
	$(MAKE) clean
	$(MAKE) test
	@if command -v lcov >/dev/null 2>&1; then \
		lcov --capture --directory $(BUILD_DIR) --output-file $(BUILD_DIR)/coverage.info --rc lcov_branch_coverage=1; \
		lcov --remove $(BUILD_DIR)/coverage.info '*/tests/*' '*/unity/*' '*/minimp3*' '/usr/*' --output-file $(BUILD_DIR)/coverage.info --rc lcov_branch_coverage=1; \
		summary=$$(lcov --summary $(BUILD_DIR)/coverage.info 2>&1); \
		printf '%s\n' "$$summary"; \
		lines=$$(printf '%s\n' "$$summary" | sed -n 's/^[[:space:]]*lines\.*:[[:space:]]*\([0-9.]*\)%.*/\1/p'); \
		funcs=$$(printf '%s\n' "$$summary" | sed -n 's/^[[:space:]]*functions\.*:[[:space:]]*\([0-9.]*\)%.*/\1/p'); \
		ok=1; \
		awk -v v="$$lines" -v min="$(COVERAGE_MIN_LINES)" 'BEGIN { exit !(v+0 >= min+0) }' \
			|| { echo "FAIL: line coverage $${lines}% is below $(COVERAGE_MIN_LINES)%" >&2; ok=0; }; \
		awk -v v="$$funcs" -v min="$(COVERAGE_MIN_FUNCS)" 'BEGIN { exit !(v+0 >= min+0) }' \
			|| { echo "FAIL: function coverage $${funcs}% is below $(COVERAGE_MIN_FUNCS)%" >&2; ok=0; }; \
		test $$ok -eq 1 || exit 1; \
		echo "Coverage gate passed (lines $${lines}%, functions $${funcs}%)."; \
	else \
		echo "Install lcov to check coverage." >&2; \
		exit 1; \
	fi

# Optional HTML report (not part of the default coverage gate).
coverage-html: coverage
	@mkdir -p $(REPORT_DIR)
	genhtml $(BUILD_DIR)/coverage.info --output-directory $(REPORT_DIR) --branch-coverage
	@echo "HTML report: $(REPORT_DIR)/index.html"

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

define TEST_RULE
$(BUILD_DIR)/$(1): $(1).c $(SRC_PROD) $(UNITY_SRC) | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) $$^ -o $$@ $$(LDFLAGS)
endef

$(foreach t,$(filter-out test_mp3_player test_flac_player test_opus_player,$(TEST_RUNNERS)),$(eval $(call TEST_RULE,$(t))))

$(BUILD_DIR)/test_mp3_player: test_mp3_player.c $(SRC_PROD) $(PROJECT_ROOT)/source/mp3_player.c $(HOST_MOCKS) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(VGM_MINIMP3) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_flac_player: test_flac_player.c $(SRC_PROD) $(PROJECT_ROOT)/source/flac_player.c $(HOST_MOCKS) $(HOST_DRFLAC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_opus_player: test_opus_player.c $(SRC_PROD) $(PROJECT_ROOT)/source/opus_player.c $(HOST_MOCKS) $(HOST_OPUSFILE) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(REPORT_DIR)
