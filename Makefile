# mitr Makefile — portable incremental build

# ========================================================================
# Configuration
# ========================================================================

PROJECT = mitr
PREFIX ?= /usr/local
DESTDIR ?=
APPDIR ?=

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
BUILD_DIR = build
VENDOR_DIR = vendor
VENDOR_BUILD_DIR = $(BUILD_DIR)/vendor

CC ?= cc
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy

STD = -std=c2x
WARNINGS = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2 \
           -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-parameter

# Treat src/ warnings as errors in CI via: make WERROR=1
WERROR ?= 0
SRC_WERROR =
ifeq ($(WERROR),1)
SRC_WERROR = -Werror
endif

USE_VENDOR ?= 1
PKG_CONFIG ?= pkg-config
UNAME_S := $(shell uname -s)

ifeq ($(USE_VENDOR),1)
    INCLUDES = -I$(INC_DIR) -I$(VENDOR_BUILD_DIR)/include \
               -I$(VENDOR_BUILD_DIR)/include/SDL2 \
               -I$(VENDOR_BUILD_DIR)/include/freetype2
    LDFLAGS = -L$(VENDOR_BUILD_DIR)/lib
    LIBS = -lSDL2 -lSDL2_ttf -lfreetype -lyaml
    ifeq ($(UNAME_S),Darwin)
        LIBS += -framework Cocoa -framework IOKit -framework CoreAudio \
                -framework CoreVideo -framework Carbon -framework Metal \
                -framework ForceFeedback -framework CoreHaptics \
                -framework GameController -framework AudioToolbox \
                -framework CoreFoundation -liconv -lz
    else ifeq ($(UNAME_S),Linux)
        LIBS = -lSDL2 -lSDL2_ttf -lfontconfig -lfreetype -lexpat -lyaml \
               -lpthread -ldl -lm -lz
        LIBS += -lX11 -lXext -lXrandr -lXi -lXfixes -lXcursor
    endif
    YAML_LIBS = -lyaml
else
    INCLUDES = -I$(INC_DIR)
    LDFLAGS =
    LIBS =
    ifneq ($(shell $(PKG_CONFIG) --exists sdl2 && echo yes),)
        INCLUDES += $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf)
        LIBS += $(shell $(PKG_CONFIG) --libs sdl2 SDL2_ttf)
    else
        INCLUDES += -I/opt/homebrew/include -I/usr/local/include
        LDFLAGS += -L/opt/homebrew/lib -L/usr/local/lib
        LIBS += -lSDL2 -lSDL2_ttf
    endif
    ifneq ($(shell $(PKG_CONFIG) --exists yaml-0.1 && echo yes),)
        INCLUDES += $(shell $(PKG_CONFIG) --cflags yaml-0.1)
        YAML_LIBS = $(shell $(PKG_CONFIG) --libs yaml-0.1)
        LIBS += $(YAML_LIBS)
    else
        YAML_LIBS = -lyaml
        LIBS += -lyaml
    endif
endif

ifeq ($(UNAME_S),Linux)
ifneq ($(USE_VENDOR),1)
    FONTCONFIG_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags fontconfig 2>/dev/null)
    FONTCONFIG_LIBS ?= $(shell $(PKG_CONFIG) --libs fontconfig 2>/dev/null)
    INCLUDES += $(FONTCONFIG_CFLAGS)
    LIBS += $(FONTCONFIG_LIBS)
endif
else ifeq ($(UNAME_S),Darwin)
    LIBS += -framework CoreText -framework CoreFoundation
endif

CFLAGS_COMMON = $(STD) $(WARNINGS) $(INCLUDES) -MMD -MP
DEBUG_FLAGS = -g -O0 -DDEBUG -fsanitize=address -fsanitize=undefined
RELEASE_FLAGS = -O3 -DNDEBUG
TEST_FLAGS = -D_POSIX_C_SOURCE=200809L

BUILD_TYPE ?= debug
COVERAGE ?= 0
ifeq ($(COVERAGE),1)
    CFLAGS_COMMON += --coverage
    LDFLAGS += --coverage
endif

ifeq ($(BUILD_TYPE),release)
    CFLAGS = $(CFLAGS_COMMON) $(RELEASE_FLAGS)
    LDFLAGS_SAN =
    OBJ_DIR = $(BUILD_DIR)/obj/release
else
    CFLAGS = $(CFLAGS_COMMON) $(DEBUG_FLAGS)
    LDFLAGS_SAN = -fsanitize=address -fsanitize=undefined
    OBJ_DIR = $(BUILD_DIR)/obj/debug
endif

LDFLAGS += $(LDFLAGS_SAN)

# Rebuild objects when CC/CFLAGS change within the same OBJ_DIR
# (WERROR, COVERAGE, etc.). Debug and release already use different dirs.
COMPILE_FLAGS_FILE = $(OBJ_DIR)/compile_flags
$(shell mkdir -p $(OBJ_DIR) && \
  printf '%s\n' "$(CC)" "$(CFLAGS)" "$(SRC_WERROR)" "$(TEST_FLAGS)" > $(COMPILE_FLAGS_FILE).tmp && \
  if [ ! -f $(COMPILE_FLAGS_FILE) ] || ! cmp -s $(COMPILE_FLAGS_FILE).tmp $(COMPILE_FLAGS_FILE); then \
    mv $(COMPILE_FLAGS_FILE).tmp $(COMPILE_FLAGS_FILE); \
  else \
    rm -f $(COMPILE_FLAGS_FILE).tmp; \
  fi)

FMT_FILES = $(shell find $(SRC_DIR) $(INC_DIR) $(TEST_DIR) \
                -name '*.c' -o -name '*.h' 2>/dev/null)
LINT_SRCS = $(shell find $(SRC_DIR) $(TEST_DIR) -name '*.c' 2>/dev/null)
LINT_FLAGS = $(STD) $(WARNINGS) $(INCLUDES)
# Analyzer/bugprone gate for src/. POSIX feature-test macros, C11 fprintf,
# struct padding, and env-taint on a validated execve are excluded.
LINT_SRC_CHECKS = -*,clang-analyzer-*,bugprone-*,-bugprone-easily-swappable-parameters,-bugprone-reserved-identifier,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-clang-analyzer-optin.performance.Padding,-clang-analyzer-optin.taint.GenericTaint

# ========================================================================
# Sources
# ========================================================================

MAIN_APP_SRCS = $(SRC_DIR)/main.c \
                $(SRC_DIR)/font_manager.c \
                $(SRC_DIR)/font_resolver.c \
                $(SRC_DIR)/terminal_buffer.c \
                $(SRC_DIR)/unicode.c \
                $(SRC_DIR)/renderer.c \
                $(SRC_DIR)/ansi_parser.c \
                $(SRC_DIR)/pty_manager.c \
                $(SRC_DIR)/mouse.c \
                $(SRC_DIR)/config.c \
                $(SRC_DIR)/input_encoder.c \
                $(SRC_DIR)/app_search.c \
                $(SRC_DIR)/app_input.c

LINT_SRC_SRCS = $(MAIN_APP_SRCS)

MAIN_SRCS = $(MAIN_APP_SRCS) $(VENDOR_DIR)/log.c/src/log.c

TEST_SRCS = $(TEST_DIR)/test_runner.c \
            $(TEST_DIR)/test_framework.c \
            $(TEST_DIR)/test_helpers.c \
            $(TEST_DIR)/test_terminal_buffer.c \
            $(TEST_DIR)/test_ansi_parser.c \
            $(TEST_DIR)/test_cursor_control.c \
            $(TEST_DIR)/test_pty_manager.c \
            $(TEST_DIR)/test_mouse.c \
            $(TEST_DIR)/test_config.c \
            $(TEST_DIR)/test_input_encoder.c \
            $(TEST_DIR)/test_app_search.c \
            $(TEST_DIR)/test_vt100.c \
            $(TEST_DIR)/test_vt102.c \
            $(TEST_DIR)/test_vt220.c \
            $(TEST_DIR)/test_vt320.c \
            $(TEST_DIR)/test_xterm.c \
            $(TEST_DIR)/test_renderer.c \
            $(TEST_DIR)/test_font_manager.c \
            $(TEST_DIR)/test_font_resolver.c \
            $(TEST_DIR)/test_unicode.c \
            $(TEST_DIR)/test_performance.c

TEST_LIB_SRCS = $(SRC_DIR)/terminal_buffer.c \
                $(SRC_DIR)/unicode.c \
                $(SRC_DIR)/ansi_parser.c \
                $(SRC_DIR)/pty_manager.c \
                $(SRC_DIR)/mouse.c \
                $(SRC_DIR)/config.c \
                $(SRC_DIR)/input_encoder.c \
                $(SRC_DIR)/app_search.c \
                $(SRC_DIR)/renderer.c \
                $(SRC_DIR)/font_manager.c \
                $(SRC_DIR)/font_resolver.c \
                $(VENDOR_DIR)/log.c/src/log.c

MAIN_APP_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(MAIN_APP_SRCS))
VENDOR_LOG_OBJ = $(OBJ_DIR)/vendor/log.c/src/log.o
MAIN_OBJS = $(MAIN_APP_OBJS) $(VENDOR_LOG_OBJ)

TEST_APP_SRCS = $(TEST_SRCS) $(filter-out $(VENDOR_DIR)/log.c/src/log.c,$(TEST_LIB_SRCS))
TEST_APP_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(TEST_APP_SRCS))
TEST_OBJS = $(TEST_APP_OBJS) $(VENDOR_LOG_OBJ)

MAIN_TARGET = $(BUILD_DIR)/$(PROJECT)
TEST_TARGET = $(BUILD_DIR)/test_runner

VENDOR_LIBS = $(VENDOR_BUILD_DIR)/lib/libSDL2.a \
              $(VENDOR_BUILD_DIR)/lib/libSDL2_ttf.a \
              $(VENDOR_BUILD_DIR)/lib/libfreetype.a \
              $(VENDOR_BUILD_DIR)/lib/libz.a \
              $(VENDOR_BUILD_DIR)/lib/libyaml.a

ifeq ($(UNAME_S),Linux)
VENDOR_LIBS += $(VENDOR_BUILD_DIR)/lib/libfontconfig.a \
               $(VENDOR_BUILD_DIR)/lib/libexpat.a
endif

# ========================================================================
# Targets
# ========================================================================

.PHONY: all debug release test test-perf coverage clean clean-all run help deps check-deps \
        vendor vendor-clean fmt fmt-check lint lint-src lint-fix fuzz compile-commands install \
        uninstall

FUZZ_CC ?= clang
FUZZ_SRCS = $(TEST_DIR)/fuzz_ansi_parser.c \
            $(SRC_DIR)/ansi_parser.c \
            $(SRC_DIR)/terminal_buffer.c \
            $(SRC_DIR)/unicode.c \
            $(VENDOR_DIR)/log.c/src/log.c
FUZZ_TARGET = $(BUILD_DIR)/fuzz_ansi_parser
FUZZ_CORPUS = $(TEST_DIR)/fuzz_corpus
FUZZ_REQUIRED ?= 0

all: debug

debug: BUILD_TYPE = debug
debug: $(MAIN_TARGET)
	@echo "Debug build complete: $(MAIN_TARGET)"

release: BUILD_TYPE = release
release:
	@$(MAKE) BUILD_TYPE=release $(MAIN_TARGET)
	@echo "Release build complete: $(MAIN_TARGET)"

ifeq ($(USE_VENDOR),1)
$(MAIN_TARGET): $(MAIN_OBJS) $(VENDOR_LIBS) | $(BUILD_DIR)
	@echo "Linking $(PROJECT) (vendored)..."
	$(CC) $(MAIN_OBJS) -o $@ $(LDFLAGS) $(LIBS)
else
$(MAIN_TARGET): $(MAIN_OBJS) | $(BUILD_DIR)
	@echo "Linking $(PROJECT) (system libs)..."
	$(CC) $(MAIN_OBJS) -o $@ $(LDFLAGS) $(LIBS)
endif

$(TEST_TARGET): $(TEST_OBJS) | $(BUILD_DIR)
	@echo "Linking tests..."
ifeq ($(USE_VENDOR),1)
	$(CC) $(TEST_OBJS) -o $@ $(LDFLAGS) $(LIBS) $(YAML_LIBS)
else
	$(CC) $(TEST_OBJS) -o $@ $(LDFLAGS) $(LIBS) $(YAML_LIBS)
endif

$(OBJ_DIR)/src/%.o: $(SRC_DIR)/%.c $(COMPILE_FLAGS_FILE) | check-deps
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SRC_WERROR) -c $< -o $@

$(OBJ_DIR)/tests/%.o: $(TEST_DIR)/%.c $(COMPILE_FLAGS_FILE) | check-deps
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TEST_FLAGS) -c $< -o $@

# Vendor log.c: keep warnings out of -Werror project gate
$(VENDOR_LOG_OBJ): $(VENDOR_DIR)/log.c/src/log.c $(COMPILE_FLAGS_FILE)
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -Werror,$(CFLAGS)) -Wno-format-nonliteral -c $< -o $@

-include $(MAIN_APP_OBJS:.o=.d)
-include $(TEST_APP_OBJS:.o=.d)
-include $(VENDOR_LOG_OBJ:.o=.d)

test: $(TEST_TARGET)
	@echo "Running unit and integration tests..."
	@./$(TEST_TARGET)

test-perf: $(TEST_TARGET)
	@echo "Running performance tests..."
	@./$(TEST_TARGET) --perf

coverage:
	@$(MAKE) COVERAGE=1 clean
	@$(MAKE) COVERAGE=1 test
	@mkdir -p $(BUILD_DIR)/coverage
	@ROOT="$(CURDIR)" python3 scripts/coverage_report.py $(OBJ_DIR) $(BUILD_DIR)/coverage

compile-commands:
	@CC="$(CC)" CFLAGS="$(CFLAGS)" TEST_FLAGS="$(TEST_FLAGS)" \
	  ROOT="$(CURDIR)" OBJDIR="$(OBJ_DIR)" TEST_FILES="$(TEST_SRCS)" \
	  FILES="$(MAIN_APP_SRCS) $(TEST_SRCS) $(VENDOR_DIR)/log.c/src/log.c" \
	  python3 scripts/gen_compile_commands.py

fuzz: | $(BUILD_DIR)
	@set -e; \
	if ! command -v $(FUZZ_CC) >/dev/null 2>&1; then \
	  echo "Skipping fuzz: $(FUZZ_CC) not found (corpus kept in $(FUZZ_CORPUS))"; \
	  if [ "$(FUZZ_REQUIRED)" = 1 ]; then exit 1; fi; \
	  exit 0; \
	fi; \
	echo "Building fuzz harness with $(FUZZ_CC)..."; \
	if ! $(FUZZ_CC) -std=c2x -g -O1 -fsanitize=fuzzer,address,undefined \
	      -I$(INC_DIR) $(FUZZ_SRCS) -o $(FUZZ_TARGET); then \
	  echo "Fuzz harness failed to link with $(FUZZ_CC) -fsanitize=fuzzer"; \
	  echo "Corpus retained at $(FUZZ_CORPUS)"; \
	  if [ "$(FUZZ_REQUIRED)" = 1 ]; then exit 1; fi; \
	  exit 0; \
	fi; \
	echo "Running short fuzz pass over $(FUZZ_CORPUS)..."; \
	$(FUZZ_TARGET) -max_total_time=5 $(FUZZ_CORPUS)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

run: $(MAIN_TARGET)
	@./$(MAIN_TARGET)

run-debug: debug
	@DEBUG=1 ./$(MAIN_TARGET)

install:
	@$(MAKE) clean
	@$(MAKE) release
	@PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" APPDIR="$(APPDIR)" \
	  bash scripts/install.sh

uninstall:
	@PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" APPDIR="$(APPDIR)" \
	  bash scripts/install.sh --uninstall

DEPS_DEV ?= 0

deps:
	@USE_VENDOR="$(USE_VENDOR)" DEPS_DEV="$(DEPS_DEV)" bash scripts/install_deps.sh

check-deps:
	@set -e; \
	if [ "$(UNAME_S)" = "Linux" ]; then \
	  if ! command -v "$(PKG_CONFIG)" >/dev/null 2>&1; then \
	    echo "Missing pkg-config. Run: make deps" >&2; \
	    exit 1; \
	  fi; \
	  if [ "$(USE_VENDOR)" = "1" ]; then \
	    modules="x11 xext xrandr xi xfixes xcursor"; \
	  else \
	    modules="fontconfig sdl2 SDL2_ttf yaml-0.1"; \
	  fi; \
	  if ! "$(PKG_CONFIG)" --exists $$modules; then \
	    echo "Missing system build dependencies for USE_VENDOR=$(USE_VENDOR)." >&2; \
	    "$(PKG_CONFIG)" --print-errors --exists $$modules 2>&1 || true; \
	    echo "Install them with: make deps" >&2; \
	    exit 1; \
	  fi; \
	fi

vendor:
	@bash scripts/build_vendor.sh

vendor-clean:
	@rm -rf $(VENDOR_BUILD_DIR)
	@rm -rf $(VENDOR_DIR)/*/build_static

fmt:
	@$(CLANG_FORMAT) -i $(FMT_FILES)

fmt-check:
	@$(CLANG_FORMAT) --dry-run --Werror $(FMT_FILES)
	@echo "All files are properly formatted."

lint:
	@$(CLANG_TIDY) $(LINT_SRCS) -- $(LINT_FLAGS)

lint-src:
	@$(CLANG_TIDY) $(LINT_SRC_SRCS) --header-filter='^(src|include)/' \
	  --checks='$(LINT_SRC_CHECKS)' \
	  --warnings-as-errors='clang-analyzer-*,bugprone-*' \
	  -- $(LINT_FLAGS)

lint-fix:
	@$(CLANG_TIDY) --fix --fix-errors $(LINT_SRCS) -- $(LINT_FLAGS)

clean:
	@rm -rf $(BUILD_DIR)/obj $(MAIN_TARGET) $(TEST_TARGET) $(FUZZ_TARGET) \
	        $(BUILD_DIR)/.fuzz_probe
	@echo "Clean complete (vendor kept)."

clean-all: clean vendor-clean
	@echo "Full clean complete!"

help:
	@echo "mitr Makefile"
	@echo "  make / make debug   - debug build with sanitizers"
	@echo "  make release        - portable optimized build (no -march=native)"
	@echo "  make deps           - install system build dependencies"
	@echo "  DEPS_DEV=1 make deps - also clang-format; Linux adds clang and clang-tidy"
	@echo "  make check-deps     - verify system build dependencies"
	@echo "  make vendor         - build vendored application libraries"
	@echo "  make install        - release build, copy to PREFIX/bin, register launcher"
	@echo "  make uninstall      - remove PREFIX/bin/mitr and the launcher icon"
	@echo "  PREFIX DESTDIR APPDIR - install paths (defaults in README)"
	@echo "  make test           - build and run unit/integration tests"
	@echo "  make test-perf      - build and run performance tests"
	@echo "  make coverage       - tests with --coverage and build/coverage/summary.txt"
	@echo "  make compile-commands - write compile_commands.json for clangd"
	@echo "  make lint-src       - clang-tidy analyzer/bugprone on src/ (errors)"
	@echo "  make WERROR=1       - treat warnings as errors (CI)"
	@echo "  USE_VENDOR=0 make   - system libs via pkg-config when available"
	@echo "  make fmt-check lint fuzz clean"
