# log.c

A small C logging library by [rxi](https://github.com/rxi/log.c), used by mitr.
It provides TRACE through FATAL levels, timestamps, file/line information,
and optional colors, output callbacks, and locking callbacks.

## Use in mitr

Include the project wrapper, or `common.h`, rather than vendor headers:

```c
#include "mitr_log.h"

log_debug("Loaded %zu font faces", count);
log_warn("Fallback font unavailable: %s", path);
```

The Makefile compiles `src/log.c` directly in both vendor and system-library
builds. The app uses DEBUG logging in debug builds and INFO in release;
tests use WARN. Follow the [project logging rules](../../AGENTS.md#logging).

The bundled version is recorded in [VERSIONS.md](../VERSIONS.md).
The MIT license is included in [src/log.c](src/log.c).
