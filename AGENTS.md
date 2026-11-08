# mitr contributor guide

Rules for people and coding agents working on mitr. Start with
[README.md](README.md) for user-facing behavior and
[ARCHITECTURE.md](ARCHITECTURE.md) for modules and ownership.
Search `include/` and `src/` before adding an API that may already exist.

## Constraints and layout

Keep the design simple: flat modules, explicit ownership, no extra frameworks.

- C23 (`-std=c2x`), GCC 13+ or Clang 15+; Linux and macOS.
- SDL2 rendering without custom shaders; SDL2_ttf / FreeType for fonts.
- YAML through libyaml; sample config in `assets/mitr.yml`.
- Version: `MITR_VERSION` in `include/common.h`. License: MIT.

```text
include/    Public headers, one per module, plus common.h
src/        Implementations and main.c
tests/      Test framework, suites, fuzz harness and corpus
assets/     Sample config, icons, Linux launcher, macOS Info.plist
scripts/    Dependency builds, installation, developer tools
vendor/     Third-party sources; do not patch to fix application bugs
.github/    Ubuntu and macOS CI
build/      mitr, test_runner, optional fuzz_ansi_parser; never commit
```

Objects go to `build/obj/`; bundled dependencies build into `build/vendor/`.
`USE_VENDOR=1` is the default and the CI configuration.

Keep opaque types opaque and avoid circular includes. SDL types belong in
renderer/font initialization APIs, `main.c`, `app_input`, and SDL tests.
Keep SDL out of `input_encoder`; route special keys, paste, and mouse reports
through `app_input` and `input_encoder`.

## Build and check

Use the Makefile; do not add a second build system.

```sh
make deps DEPS_DEV=1      # prerequisites plus formatting/lint/fuzz tools
make check-deps          # check prerequisites without installing
make vendor              # build bundled libraries and tools once
make                     # debug: ASan + UBSan, -g -O0 -DDEBUG
make release             # -O3 -DNDEBUG; no -march=native
make test                # unit and integration tests
make test-perf           # separate performance tests
make fmt-check           # formatting gate
make lint                # full clang-tidy sweep; local, best effort
make lint-src            # analyzer/bugprone findings in src/ are errors
make fuzz                # parser fuzzing for 5s; skip if toolchain unavailable
make fuzz FUZZ_REQUIRED=1 # fail if clang/libFuzzer is unavailable
make coverage            # report at build/coverage/summary.txt
make compile-commands    # gitignored compile_commands.json for clangd
make clean               # remove objects and binaries, keep vendor build
make clean-all           # also remove vendor build
make run                 # run build/mitr
```

`make deps` honors `USE_VENDOR`. `DEPS_DEV=1` adds clang-format, and on Linux
clang and clang-tidy. System-library builds use `make USE_VENDOR=0` and
pkg-config; CI does not exercise that path. Run `make clean` when changing
build mode or dependency source; see [installation](README.md#install) for
`make install`, `make uninstall`, `PREFIX`, `APPDIR`, and `DESTDIR`.

For a typical code change, both the app and tests must compile:

```sh
make WERROR=1 all test
make fmt-check
```

Fix warnings in `src/` and sanitizer failures. Vendor warnings are acceptable;
disabling warnings, sanitizers, or tests is not.

CI runs these checks on Ubuntu and macOS, plus `make compile-commands`.
Ubuntu also runs `make lint-src` and `make fuzz FUZZ_REQUIRED=1`.
Dependency installation must succeed, including Homebrew; do not hide failures
with `|| true`. PRs must pass the applicable CI gates.

## Adding or changing code

Use TDD for behavior changes: test first, then the header and implementation.
Update the relevant documentation in the same change.

1. Add a test in `tests/test_<module>.c`.
2. Declare the API in `include/<module>.h` and implement it in `src/<module>.c`.
3. Register new tests with `RUN_TEST` or `RUN_TEST_TIMEOUT`.
4. For a new module, add it to `MAIN_APP_SRCS` and `TEST_LIB_SRCS` in the
   Makefile, add its suite to `TEST_SRCS`, and declare/call the suite runner
   in `tests/test_runner.c`.
5. Run the checks above. Run `make clean` first when adding a module.

Use the custom framework in `tests/test_framework.h`:

```c
TEST(module_returns_expected_value) {
  Module *m = module_init();
  ASSERT_NOT_NULL(m);
  ASSERT_EQ(module_value(m), 42);
  module_destroy(m);
}
```

Assertions include `ASSERT_TRUE/FALSE`, `ASSERT_EQ/NE`, `ASSERT_NULL/NOT_NULL`,
`ASSERT_STR_EQ`, `ASSERT_MEM_EQ`, and `TEST_FAIL`. Module suites use `test_<module>.c`;
parser protocol tests also live in `test_vt100` through `test_xterm` and
`test_cursor_control`. Register tests inside `run_*_tests(void)`.

PTY tests use a real shell: keep them deterministic and bounded. Parser
tests assert cells and cursor state, without SDL. Renderer and font tests
need SDL. There are no per-file test binaries; use `make test` for focused
regressions too. Performance tests do not replace the unit suite.

## Code style

- Keep each function focused; aim for about 50 lines or split it. Extract
  repeated blocks into helpers; avoid clever macros and unnecessary layers.
- Functions use `module_action`, types `PascalCase`, variables `snake_case`,
  macros/enums `UPPER_CASE`, and header guards `MITR_*`.
- Use `nullptr`, `static_assert`, and `mitr_min` / `mitr_max`. `common.h`
  supplies the compatibility shim for pre-final C23 compilers.
- Include project headers as `"module.h"` via `-Iinclude`, SDL as
  `<SDL2/SDL.h>`. Public APIs must not include vendor headers; logging goes
  through `mitr_log.h`, included by `common.h`.
- Put static helpers above public functions. Use `/* */` comments to explain
  intent; omit iteration labels, development history, and stale TODOs.
- Match `.clang-format` and surrounding code. Format only touched files with
  clang-format; `make fmt` rewrites all of `src/`, `include/`, and `tests/`,
  so ask before formatting the whole tree.

The clang-tidy configuration includes `bugprone-*`, `cert-*`,
`clang-analyzer-*`, and readability checks with cognitive complexity 25.
Fix findings rather than silencing real bugs.

### Memory and errors

Use `*_init()` / `*_destroy()` and clean up partially initialized objects.
`*_destroy(NULL)` must be a no-op.

- Allocate with `checked_malloc` / `checked_calloc` from `common.h`.
  Check size multiplication with `size_mul_overflow`; `checked_calloc`
  already checks its two arguments.
- Validate grid sizes with `mitr_grid_dims_valid` (1–1024 rows and columns).
  Respect `MITR_MAX_*`, check pointers, and bounds-check indexes.
- Use `SAFE_FREE` to free and clear pointers.
- No heap allocation in the parser feed loop or per-cell render path.
- Return `NULL`/`nullptr` for pointer failures, `MitrError` for fallible
  mutations, `ssize_t` for I/O, and `bool` for predicates.

### Logging

Use the bundled `log.c`; tests set `LOG_WARN`.

| Level | Use |
|-------|-----|
| TRACE | Initialization and destruction only |
| DEBUG | Initialization, destruction, mode changes |
| INFO | General application information |
| WARN | Recoverable errors or invalid input |
| ERROR | Failures that abort an operation |
| FATAL | Fatal failures |

Keep TRACE and DEBUG out of the parser feed loop and per-cell drawing.

## Boundaries and security

Preserve existing naming, ownership, and error codes. Prefer fixing the
parser or buffer over adding a renderer workaround. Treat PTY output as
untrusted: protect CSI parameters, OSC buffers, and cell indexes. Do not
weaken the shell-path checks or environment whitelist described in
[ARCHITECTURE.md](ARCHITECTURE.md#pty-and-shell). Never execute a shell
before `pty_manager_shell_path_valid` succeeds.

Ask before:

- Adding a dependency or upgrading vendor code.
- Exposing opaque structs in public headers.
- Adding a GPU path or dirty-rectangle rendering, or replacing the test framework.
- Implementing Windows / ConPTY support.

Do not:

- Patch `vendor/` to fix mitr; change application code or scripts instead.
- Commit build outputs, sanitizer logs, or secrets.
- Hand-roll character width when `unicode.c` already handles the codepoint.
- Add a second logging library or JSON/YAML parser.
- Delete fuzz corpus samples because they look like noise.
- Add a network protocol.
- Expand into a terminal multiplexer, plugin host, or GPU engine unless assigned.

Do not implement deliberate gaps without a request: full grapheme clusters
or ligatures, cursor animation, OSC 1 icon names, or Windows support.
User-facing limitations belong in [README.md](README.md#limitations).

## Git and reviews

Commit only when the human asks. Use Conventional Commits:
`type(scope): description`, omitting an unhelpful scope. Write an imperative
subject that explains the purpose, for example
`fix(parser): preserve CUP origin when DECOM is set`.

Commit as the current repository user. Human coauthor trailers are allowed;
never attribute authorship or coauthorship to agents. Keep the diff within
the task and avoid unrelated reformatting.

## Keeping docs current

Update the document that owns a topic in the same change. Link to it from
other files instead of repeating it.

| File | Owns |
|------|------|
| [README.md](README.md) | Installation, use, settings, compatibility, limitations, license |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Modules, ownership, data flow, parser, PTY, rendering |
| This file | Contribution workflow, commands, checks, coding and security rules |
| [CHANGELOG.md](CHANGELOG.md) | Notable changes shipped; use Keep a Changelog sections |
| [vendor/README.md](vendor/README.md) | Dependency build layout and update procedure |
| [vendor/VERSIONS.md](vendor/VERSIONS.md) | Bundled versions and upstream sources |
| [NOTICE](NOTICE) | Third-party licensing and attribution |

If docs disagree with the code, check headers and tests, then correct the
document before finishing. Keep architecture and current limitation lists
out of the changelog.

## References

- [VT100 manual](https://vt100.net/docs/vt100-ug/)
- [ECMA-48](https://www.ecma-international.org/publications/standards/Ecma-048.htm)
- [XTerm control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
- [SDL2 documentation](https://wiki.libsdl.org/)
- [C23 reference](https://en.cppreference.com/w/c/23)
