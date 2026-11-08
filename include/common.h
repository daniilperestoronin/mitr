/**
 * common.h - Common definitions, types, and macros for mitr
 *
 * This file contains shared definitions used across the entire project.
 */

#ifndef MITR_COMMON_H
#define MITR_COMMON_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __STDC_VERSION__ < 202311L
#define nullptr ((void *)0)
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

#define MITR_VERSION_MAJOR 0
#define MITR_VERSION_MINOR 1
#define MITR_VERSION_PATCH 0
#define MITR_VERSION "0.1.0"

#define WINDOW_TITLE "mitr v" MITR_VERSION

/* ========================================================================
 * Error Codes
 * ======================================================================== */

typedef enum {
  MITR_OK = 0,
  MITR_ERROR_MEMORY,
  MITR_ERROR_SDL,
  MITR_ERROR_PTY,
  MITR_ERROR_INVALID_PARAM,
  MITR_ERROR_IO,
  MITR_ERROR_CONFIG,
  MITR_ERROR_NOT_IMPLEMENTED
} MitrError;

/*
 * Error return policy:
 *   constructors / pointer lookups → nullptr
 *   fallible mutations / parsers     → MitrError (MITR_OK on success)
 *   I/O transfers                    → ssize_t (bytes, 0, or -1)
 *   predicates and mode queries      → bool
 */

/* ========================================================================
 * Debug Logging (using log.c library)
 * ======================================================================== */

#include "mitr_log.h"

/* ========================================================================
 * Performance Profiling
 * ======================================================================== */

#ifdef DEBUG
#include <sys/time.h>

static inline uint64_t perf_get_time_us(void) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

#define PERF_START(name) uint64_t perf_start_##name = perf_get_time_us()

#define PERF_END(name)                                                         \
  do {                                                                         \
    uint64_t perf_end_##name = perf_get_time_us();                             \
    uint64_t perf_duration_##name = perf_end_##name - perf_start_##name;       \
    if (perf_duration_##name > 10000) {                                        \
      log_debug("PERF: %s took %llu us (%.2f ms)", #name,                      \
                (unsigned long long)perf_duration_##name,                      \
                (double)perf_duration_##name / 1000.0);                        \
    }                                                                          \
  } while (0)

#else
#define PERF_START(name) ((void)0)
#define PERF_END(name) ((void)0)
#endif

/* ========================================================================
 * UTF-8 Encoding
 * ======================================================================== */

/**
 * Encode a Unicode codepoint to UTF-8.
 * @param codepoint Unicode codepoint
 * @param out Output buffer (must be at least 5 bytes)
 * @return Number of bytes written (excluding NUL terminator)
 */
static inline int codepoint_to_utf8(uint32_t codepoint, char *out) {
  if (codepoint < 0x80) {
    out[0] = (char)codepoint;
    out[1] = '\0';
    return 1;
  } else if (codepoint < 0x800) {
    out[0] = (char)(0xC0 | (codepoint >> 6));
    out[1] = (char)(0x80 | (codepoint & 0x3F));
    out[2] = '\0';
    return 2;
  } else if (codepoint < 0x10000) {
    out[0] = (char)(0xE0 | (codepoint >> 12));
    out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[2] = (char)(0x80 | (codepoint & 0x3F));
    out[3] = '\0';
    return 3;
  } else if (codepoint < 0x110000) {
    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    out[4] = '\0';
    return 4;
  }
  out[0] = '?';
  out[1] = '\0';
  return 1;
}

/* ========================================================================
 * Utility helpers
 * ======================================================================== */

static inline int mitr_min(int a, int b) { return a < b ? a : b; }

static inline int mitr_max(int a, int b) { return a > b ? a : b; }

static inline int mitr_clamp(int value, int lo, int hi) {
  return mitr_min(mitr_max(value, lo), hi);
}

/**
 * Drawable pixels per window pixel (usually 1 or 2).
 * Returns 1 when either size is not positive so callers can run without a
 * HiDPI display.
 */
static inline float mitr_display_scale(int window_w, int drawable_w) {
  if (window_w <= 0 || drawable_w <= 0) {
    return 1.0f;
  }
  float scale = (float)drawable_w / (float)window_w;
  if (scale < 1.0f) {
    return 1.0f;
  }
  return scale;
}

/** Convert atlas/drawable pixels to window/logical pixels. */
static inline int mitr_drawable_to_logical(int drawable_px, float scale) {
  if (scale < 1.0f) {
    scale = 1.0f;
  }
  int px = (int)((float)drawable_px / scale + 0.5f);
  if (px < 1 && drawable_px > 0) {
    return 1;
  }
  return px;
}

/** Convert window/logical pixels to drawable pixels. */
static inline int mitr_logical_to_drawable(int logical_px, float scale) {
  if (scale < 1.0f) {
    scale = 1.0f;
  }
  return (int)((float)logical_px * scale + 0.5f);
}

/* Array size */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Unused parameter (to avoid warnings) */
#define UNUSED(x) (void)(x)

/* ========================================================================
 * Color Definitions (RGB888 format)
 * ======================================================================== */

/* Pack RGB values into uint32_t (0x00RRGGBB) */
#define RGB(r, g, b)                                                           \
  ((uint32_t)((((uint8_t)(r)) << 16) | (((uint8_t)(g)) << 8) | ((uint8_t)(b))))

/* Extract color components */
#define RGB_RED(color) (uint8_t)(((color) >> 16) & 0xFF)
#define RGB_GREEN(color) (uint8_t)(((color) >> 8) & 0xFF)
#define RGB_BLUE(color) (uint8_t)((color) & 0xFF)

/* Default terminal colors */
#define COLOR_BLACK RGB(0, 0, 0)
#define COLOR_RED RGB(205, 49, 49)
#define COLOR_GREEN RGB(13, 188, 121)
#define COLOR_YELLOW RGB(229, 229, 16)
#define COLOR_BLUE RGB(36, 114, 200)
#define COLOR_MAGENTA RGB(188, 63, 188)
#define COLOR_CYAN RGB(17, 168, 205)
#define COLOR_WHITE RGB(229, 229, 229)

/* Bright colors */
#define COLOR_BRIGHT_BLACK RGB(102, 102, 102)
#define COLOR_BRIGHT_RED RGB(241, 76, 76)
#define COLOR_BRIGHT_GREEN RGB(35, 209, 139)
#define COLOR_BRIGHT_YELLOW RGB(245, 245, 67)
#define COLOR_BRIGHT_BLUE RGB(59, 142, 234)
#define COLOR_BRIGHT_MAGENTA RGB(214, 112, 214)
#define COLOR_BRIGHT_CYAN RGB(41, 184, 219)
#define COLOR_BRIGHT_WHITE RGB(255, 255, 255)

/* Default foreground/background */
#define COLOR_DEFAULT_FG COLOR_WHITE
#define COLOR_DEFAULT_BG COLOR_BLACK

/* ========================================================================
 * Terminal Attributes
 * ======================================================================== */

typedef enum {
  ATTR_NONE = 0,
  ATTR_BOLD = 1 << 0,      /* VT100: Bold */
  ATTR_DIM = 1 << 1,       /* VT100: Dim */
  ATTR_ITALIC = 1 << 2,    /* VT320: Italic (not widely supported) */
  ATTR_UNDERLINE = 1 << 3, /* VT100: Underline */
  ATTR_BLINK = 1 << 4,     /* VT320: Blink */
  ATTR_REVERSE = 1 << 5,   /* VT100: Reverse video */
  ATTR_CONCEALED = 1 << 6, /* VT320: Concealed (hidden) */
  ATTR_STRIKE = 1 << 7     /* VT320: Strikethrough (not in standard) */
} TermAttributes;

/* ========================================================================
 * Platform Detection
 * ======================================================================== */

#if defined(_WIN32) || defined(_WIN64)
#define MITR_PLATFORM_WINDOWS
#elif defined(__APPLE__) && defined(__MACH__)
#define MITR_PLATFORM_MACOS
#elif defined(__linux__)
#define MITR_PLATFORM_LINUX
#elif defined(__unix__)
#define MITR_PLATFORM_UNIX
#else
#error "Unsupported platform"
#endif

/* ========================================================================
 * Dimension Limits
 * ======================================================================== */

/* Hard caps to prevent pathological allocations and integer overflow. */
#define MITR_MIN_ROWS 1
#define MITR_MIN_COLS 1
#define MITR_MAX_ROWS 1024
#define MITR_MAX_COLS 1024
#define MITR_MAX_SCROLLBACK 1000000
#define MITR_MAX_WINDOW_WIDTH 16384
#define MITR_MAX_WINDOW_HEIGHT 16384

static_assert(MITR_MIN_ROWS >= 1, "row minimum");
static_assert(MITR_MIN_COLS >= 1, "column minimum");
static_assert(MITR_MAX_ROWS >= MITR_MIN_ROWS, "row limits");
static_assert(MITR_MAX_COLS >= MITR_MIN_COLS, "column limits");
static_assert(MITR_MAX_SCROLLBACK > 0, "scrollback cap");

/* ========================================================================
 * Memory Management Helpers
 * ======================================================================== */

/* Safe free macro */
#define SAFE_FREE(ptr)                                                         \
  do {                                                                         \
    if (ptr) {                                                                 \
      free(ptr);                                                               \
      (ptr) = nullptr;                                                         \
    }                                                                          \
  } while (0)

/**
 * Multiply two size_t values with overflow detection.
 * @return true if overflow would occur (out unchanged), false on success
 */
static inline bool size_mul_overflow(size_t a, size_t b, size_t *out) {
  if (a != 0 && b > SIZE_MAX / a) {
    return true;
  }
  if (out) {
    *out = a * b;
  }
  return false;
}

/**
 * Add two size_t values with overflow detection.
 * @return true if overflow would occur (out unchanged), false on success
 */
static inline bool size_add_overflow(size_t a, size_t b, size_t *out) {
  if (b > SIZE_MAX - a) {
    return true;
  }
  if (out) {
    *out = a + b;
  }
  return false;
}

/** True if rows/cols are within supported terminal grid limits. */
static inline bool mitr_grid_dims_valid(size_t rows, size_t cols) {
  return rows >= MITR_MIN_ROWS && cols >= MITR_MIN_COLS &&
         rows <= MITR_MAX_ROWS && cols <= MITR_MAX_COLS;
}

/** True if scrollback capacity is within supported limits. */
static inline bool mitr_scrollback_capacity_valid(size_t capacity) {
  return capacity <= MITR_MAX_SCROLLBACK;
}

/* Checked malloc - returns nullptr on failure */
static inline void *checked_malloc(size_t size) {
  void *ptr = malloc(size);
  if (!ptr && size > 0) {
    fprintf(stderr, "ERROR: malloc(%zu) failed\n", size);
  }
  return ptr;
}

/* Checked calloc - rejects count*size overflow, returns nullptr on failure */
static inline void *checked_calloc(size_t count, size_t size) {
  if (count == 0 || size == 0) {
    return nullptr;
  }

  size_t total;
  if (size_mul_overflow(count, size, &total)) {
    fprintf(stderr, "ERROR: calloc size overflow (%zu * %zu)\n", count, size);
    return nullptr;
  }

  void *ptr = calloc(count, size);
  if (!ptr) {
    fprintf(stderr, "ERROR: calloc(%zu, %zu) failed\n", count, size);
  }
  return ptr;
}

#endif /* MITR_COMMON_H */
