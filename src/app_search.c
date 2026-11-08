/**
 * app_search.c - Scrollback search UI (query, wrap, reveal)
 */

#include "app_search.h"
#include "renderer.h"
#include <ctype.h>

static void app_search_sync_prompt(AppSearch *s, Renderer *r) {
  if (!s) {
    return;
  }
  if (s->active) {
    renderer_set_search_prompt(r, s->query);
  } else {
    renderer_set_search_prompt(r, nullptr);
  }
}

static void app_search_clear_hit(AppSearch *s, Renderer *r) {
  if (!s) {
    return;
  }
  s->have_hit = false;
  renderer_set_search_hit(r, false, 0, 0, 0);
}

static size_t app_utf8_next(const char *str, uint32_t *cp) {
  const unsigned char *p = (const unsigned char *)str;
  if (p[0] == 0) {
    return 0;
  }
  if (p[0] < 0x80U) {
    *cp = p[0];
    return 1;
  }
  if ((p[0] & 0xE0U) == 0xC0U && (p[1] & 0xC0U) == 0x80U) {
    *cp = ((uint32_t)(p[0] & 0x1FU) << 6) | (uint32_t)(p[1] & 0x3FU);
    return 2;
  }
  if ((p[0] & 0xF0U) == 0xE0U && (p[1] & 0xC0U) == 0x80U &&
      (p[2] & 0xC0U) == 0x80U) {
    *cp = ((uint32_t)(p[0] & 0x0FU) << 12) | ((uint32_t)(p[1] & 0x3FU) << 6) |
          (uint32_t)(p[2] & 0x3FU);
    return 3;
  }
  if ((p[0] & 0xF8U) == 0xF0U && (p[1] & 0xC0U) == 0x80U &&
      (p[2] & 0xC0U) == 0x80U && (p[3] & 0xC0U) == 0x80U) {
    *cp = ((uint32_t)(p[0] & 0x07U) << 18) | ((uint32_t)(p[1] & 0x3FU) << 12) |
          ((uint32_t)(p[2] & 0x3FU) << 6) | (uint32_t)(p[3] & 0x3FU);
    return 4;
  }
  return 0;
}

/* Grid columns spanned by the match (continuations included in the span). */
static size_t app_search_ncells(const TerminalBuffer *tb, const char *query,
                                size_t hist_row, size_t hist_col) {
  const TermCell *cells = terminal_buffer_get_history_row(tb, hist_row);
  size_t cols = terminal_buffer_get_cols(tb);
  size_t col = hist_col;
  const char *p = query;

  if (!cells || !query || hist_col >= cols) {
    return 0;
  }
  while (*p != '\0') {
    uint32_t cp = 0;
    size_t n = app_utf8_next(p, &cp);
    if (n == 0) {
      break;
    }
    p += n;
    while (col < cols && (cells[col].flags & CELL_FLAG_CONTINUATION) != 0) {
      col++;
    }
    if (col >= cols) {
      break;
    }
    col++;
  }
  while (col < cols && (cells[col].flags & CELL_FLAG_CONTINUATION) != 0) {
    col++;
  }
  return col - hist_col;
}

/*
 * Reveal a history-coordinate match: scroll so it is visible, then highlight.
 * Scrollback rows sit at the top of the view (offset = used - hist_row).
 * Live-screen rows force offset 0. Wrap itself lives in wrap_start, not
 * in terminal_buffer_search.
 */
static void app_search_reveal(AppSearch *s, TerminalBuffer *tb, Renderer *r,
                              size_t hist_row, size_t hist_col) {
  size_t used = terminal_buffer_get_scrollback_used(tb);
  size_t current = terminal_buffer_get_scroll_offset(tb);
  size_t target = (hist_row < used) ? (used - hist_row) : 0;
  int delta = (int)target - (int)current;
  size_t offset;
  size_t hist_top;
  size_t view_row;
  size_t ncells;

  terminal_buffer_scroll_view(tb, delta);
  offset = terminal_buffer_get_scroll_offset(tb);
  hist_top = used - offset;
  view_row = (hist_row >= hist_top) ? (hist_row - hist_top) : 0;
  ncells = app_search_ncells(tb, s->query, hist_row, hist_col);
  renderer_set_search_hit(r, ncells > 0, view_row, hist_col, ncells);
}

void app_search_init(AppSearch *s) {
  if (!s) {
    return;
  }
  memset(s, 0, sizeof(*s));
}

void app_search_destroy(AppSearch *s) {
  if (!s) {
    return;
  }
  memset(s, 0, sizeof(*s));
}

void app_search_enter(AppSearch *s, Renderer *r) {
  if (!s || s->active) {
    return;
  }
  s->active = true;
  s->query[0] = '\0';
  s->query_len = 0;
  app_search_clear_hit(s, r);
  app_search_sync_prompt(s, r);
}

void app_search_exit(AppSearch *s, Renderer *r) {
  if (!s) {
    return;
  }
  s->active = false;
  s->query[0] = '\0';
  s->query_len = 0;
  app_search_clear_hit(s, r);
  app_search_sync_prompt(s, r);
}

void app_search_append(AppSearch *s, Renderer *r, const char *text) {
  char filtered[APP_SEARCH_QUERY_MAX];
  size_t n = 0;
  size_t room;
  const unsigned char *p;

  if (!s || !text) {
    return;
  }
  p = (const unsigned char *)text;
  while (*p != '\0' && n + 1 < sizeof(filtered)) {
    if (iscntrl(*p)) {
      p++;
      continue;
    }
    filtered[n++] = (char)*p;
    p++;
  }
  if (n == 0) {
    return;
  }
  room = sizeof(s->query) - 1 - s->query_len;
  if (n > room) {
    return;
  }
  memcpy(s->query + s->query_len, filtered, n);
  s->query_len += n;
  s->query[s->query_len] = '\0';
  app_search_clear_hit(s, r);
  app_search_sync_prompt(s, r);
}

void app_search_backspace(AppSearch *s, Renderer *r) {
  size_t i;

  if (!s || s->query_len == 0) {
    return;
  }
  i = s->query_len - 1;
  while (i > 0 && ((unsigned char)s->query[i] & 0xC0U) == 0x80U) {
    i--;
  }
  s->query[i] = '\0';
  s->query_len = i;
  app_search_clear_hit(s, r);
  app_search_sync_prompt(s, r);
}

void app_search_next_start(bool backwards, bool have_hit, size_t hit_row,
                           size_t hit_col, size_t origin, size_t last_row,
                           size_t cols, size_t *start_row, size_t *start_col) {
  if (!start_row || !start_col || cols == 0) {
    return;
  }

  *start_row = origin;
  *start_col = 0;

  if (backwards) {
    if (have_hit && (hit_row > origin || hit_col > 0)) {
      if (hit_col > 0) {
        *start_row = hit_row;
        *start_col = hit_col - 1;
      } else {
        *start_row = hit_row - 1;
        *start_col = cols - 1;
      }
    } else {
      *start_row = last_row;
      *start_col = cols - 1;
    }
  } else if (have_hit) {
    if (hit_col + 1 < cols) {
      *start_row = hit_row;
      *start_col = hit_col + 1;
    } else if (hit_row < last_row) {
      *start_row = hit_row + 1;
      *start_col = 0;
    } else {
      *start_row = origin;
      *start_col = 0;
    }
  }
}

bool app_search_wrap_start(bool backwards, size_t origin, size_t last_row,
                           size_t last_col, size_t start_row, size_t start_col,
                           size_t *out_row, size_t *out_col) {
  if (!out_row || !out_col) {
    return false;
  }
  if (backwards) {
    if (start_row == last_row && start_col == last_col) {
      return false;
    }
    *out_row = last_row;
    *out_col = last_col;
    return true;
  }
  if (start_row == origin && start_col == 0) {
    return false;
  }
  *out_row = origin;
  *out_col = 0;
  return true;
}

void app_search_find(AppSearch *s, TerminalBuffer *tb, Renderer *r,
                     bool backwards) {
  size_t cols;
  size_t used;
  size_t rows;
  size_t total;
  size_t origin;
  size_t last_row;
  size_t start_row;
  size_t start_col;
  size_t out_row = 0;
  size_t out_col = 0;
  size_t wrap_row = 0;
  size_t wrap_col = 0;
  bool found;

  if (!s || !tb) {
    return;
  }

  cols = terminal_buffer_get_cols(tb);
  used = terminal_buffer_get_scrollback_used(tb);
  rows = terminal_buffer_get_rows(tb);
  total = used + rows;
  /* Alt screen: live grid only (history origin = used). Main: (0,0). */
  origin = terminal_buffer_is_alt_screen(tb) ? used : 0;
  last_row = (total > 0) ? (total - 1) : 0;
  start_row = origin;
  start_col = 0;

  if (s->query_len == 0 || cols == 0 || total == 0 || origin >= total) {
    app_search_clear_hit(s, r);
    return;
  }

  app_search_next_start(backwards, s->have_hit, s->hit_row, s->hit_col, origin,
                        last_row, cols, &start_row, &start_col);

  found = terminal_buffer_search(tb, s->query, backwards, true, start_row,
                                 start_col, &out_row, &out_col);
  if (found && out_row < origin) {
    found = false;
  }
  if (!found &&
      app_search_wrap_start(backwards, origin, last_row, cols - 1, start_row,
                            start_col, &wrap_row, &wrap_col)) {
    found = terminal_buffer_search(tb, s->query, backwards, true, wrap_row,
                                   wrap_col, &out_row, &out_col);
    if (found && out_row < origin) {
      found = false;
    }
  }

  if (!found) {
    app_search_clear_hit(s, r);
    return;
  }
  s->hit_row = out_row;
  s->hit_col = out_col;
  s->have_hit = true;
  app_search_reveal(s, tb, r, out_row, out_col);
}
