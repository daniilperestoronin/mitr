/**
 * app_search.h - Scrollback search UI state (query, wrap, hit highlight)
 *
 * No SDL. Renderer is an optional overlay pointer (nullptr in unit tests).
 * Wrap around history is applied here because terminal_buffer_search does not
 * wrap. Find is always ASCII case-insensitive (A–Z → a–z); there is no
 * prompt option and no regex.
 */

#ifndef MITR_APP_SEARCH_H
#define MITR_APP_SEARCH_H

#include "common.h"
#include "terminal_buffer.h"
#include <stddef.h>

typedef struct Renderer Renderer;

#define APP_SEARCH_QUERY_MAX 256

typedef struct {
  char query[APP_SEARCH_QUERY_MAX];
  size_t query_len;
  bool active;
  size_t hit_row;
  size_t hit_col;
  bool have_hit;
} AppSearch;

void app_search_init(AppSearch *s);
void app_search_destroy(AppSearch *s);

void app_search_enter(AppSearch *s, Renderer *r);
void app_search_exit(AppSearch *s, Renderer *r);
void app_search_append(AppSearch *s, Renderer *r, const char *text);
void app_search_backspace(AppSearch *s, Renderer *r);
void app_search_find(AppSearch *s, TerminalBuffer *tb, Renderer *r,
                     bool backwards);

/*
 * Next inclusive start cell after the current hit (or origin / last cell
 * when there is no hit). cols == 0 leaves start_row/start_col unchanged.
 */
void app_search_next_start(bool backwards, bool have_hit, size_t hit_row,
                           size_t hit_col, size_t origin, size_t last_row,
                           size_t cols, size_t *start_row, size_t *start_col);

/*
 * Wrap restart after a miss. False if the first scan already started at the
 * wrap origin (no second scan).
 */
bool app_search_wrap_start(bool backwards, size_t origin, size_t last_row,
                           size_t last_col, size_t start_row, size_t start_col,
                           size_t *out_row, size_t *out_col);

#endif /* MITR_APP_SEARCH_H */
