# Bundled dependencies

This directory holds the library sources and build tools used by mitr.
Versions and upstream sources are listed in [VERSIONS.md](VERSIONS.md);
licenses and attribution are in [NOTICE](../NOTICE).

The sources are included so builds use fixed versions and can run offline
once system prerequisites are installed. The compiler, CMake, autotools,
X11 libraries on Linux, and macOS frameworks remain system dependencies.
See [build instructions](../README.md#build-and-run) for setup and the
`USE_VENDOR=0` alternative.

## What is included

| Directory | Purpose |
|-----------|---------|
| `SDL2/` | Windowing, input, and rendering |
| `SDL2_ttf/` | Font rasterization through FreeType |
| `freetype/` | Font engine |
| `fontconfig/` | Font lookup on Linux; uses host `/etc/fonts`, caches, and fonts |
| `expat/` | XML parser for Fontconfig |
| `gperf/` | Fontconfig table generator; used only during builds |
| `zlib/` | Compression for the font stack |
| `libyaml/` | Configuration parser |
| `log.c/` | Logging, compiled directly into mitr in both dependency modes |

## Build outputs

Run `make vendor` from the repository root. Static libraries go into
`build/vendor/lib/`; gperf goes into `build/vendor/bin/` and is not linked
into mitr. libc and platform libraries remain system libraries.
`make vendor-clean` removes the dependency build.

libyaml, and on Linux gperf and Fontconfig, are bootstrapped with the installed
Autotools in disposable source copies under `build/vendor/src/`. Full
regeneration keeps Automake and Libtool files consistent with the installed
tools and leaves vendored sources unchanged. Fontconfig bootstrap also needs
gettext and autopoint, installed by `make deps`. Only the gperf library and
executable are built; its generated
documentation is not needed by mitr.

## Update a dependency

Dependency upgrades require approval under [AGENTS.md](../AGENTS.md).
After approval, run from the repository root:

```sh
bash scripts/update_vendor.sh <library> <version>
```

The script downloads a release and replaces the existing source directory.
Review the diff, update [VERSIONS.md](VERSIONS.md) and [NOTICE](../NOTICE),
then rebuild with `make clean-all` and `make vendor` and run the contributor
checks. Do not patch third-party sources to work around application bugs.

Some upstream tests, docs, and IDE files are omitted to keep the repository
smaller. Preserve required license files, including FreeType's `FTL.TXT`
and `GPLv2.TXT`.
