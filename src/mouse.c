/**
 * @file mouse.c
 * @brief Mouse Manager implementation
 */

#include "mouse.h"
#include "terminal_buffer.h"
#include "unicode.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct MouseManager {
  bool in_progress;
  bool active;

  size_t start_row;
  size_t start_col;
  size_t end_row;
  size_t end_col;

  size_t norm_start_row;
  size_t norm_start_col;
  size_t norm_end_row;
  size_t norm_end_col;

  bool reporting_enabled;
};

bool mouse_pixel_to_cell(int pixel_x, int pixel_y, int padding_x,
                         int padding_y, int cell_width, int cell_height,
                         size_t rows, size_t cols, MouseCellPolicy policy,
                         size_t *row, size_t *col) {
  int64_t px;
  int64_t py;
  size_t c;
  size_t r;

  if (!row || !col || padding_x < 0 || padding_y < 0 || cell_width <= 0 ||
      cell_height <= 0 || rows == 0 || cols == 0) {
    return false;
  }

  px = (int64_t)pixel_x - (int64_t)padding_x;
  py = (int64_t)pixel_y - (int64_t)padding_y;
  if (px < 0 || py < 0) {
    if (policy != MOUSE_CELL_CLAMP) {
      return false;
    }
    if (px < 0) {
      px = 0;
    }
    if (py < 0) {
      py = 0;
    }
  }

  c = (size_t)(px / (int64_t)cell_width);
  r = (size_t)(py / (int64_t)cell_height);
  if (c >= cols || r >= rows) {
    if (policy != MOUSE_CELL_CLAMP) {
      return false;
    }
    if (c >= cols) {
      c = cols - 1;
    }
    if (r >= rows) {
      r = rows - 1;
    }
  }

  *col = c;
  *row = r;
  return true;
}

/* Append bytes, leaving room for a trailing NUL. */
static bool selection_append(char *buffer, size_t *pos, size_t cap,
                             const char *bytes, size_t n) {
  if (n == 0) {
    return true;
  }
  if (*pos >= cap || n >= cap - *pos) {
    return false;
  }
  memcpy(buffer + *pos, bytes, n);
  *pos += n;
  return true;
}

/* Helper: normalize selection coordinates (ensure start < end) */
static void normalize_selection(MouseManager *mm) {
  size_t start_row = mm->start_row;
  size_t start_col = mm->start_col;
  size_t end_row = mm->end_row;
  size_t end_col = mm->end_col;

  /* Compare positions: (row1, col1) < (row2, col2) if row1 < row2 or (row1 ==
   * row2 && col1 < col2) */
  bool start_before_end =
      (start_row < end_row) || (start_row == end_row && start_col <= end_col);

  if (start_before_end) {
    mm->norm_start_row = start_row;
    mm->norm_start_col = start_col;
    mm->norm_end_row = end_row;
    mm->norm_end_col = end_col;
  } else {
    mm->norm_start_row = end_row;
    mm->norm_start_col = end_col;
    mm->norm_end_row = start_row;
    mm->norm_end_col = start_col;
  }
}

MouseManager *mouse_manager_init(void) {
  MouseManager *mm = checked_calloc(1, sizeof(MouseManager));
  if (!mm) {
    return nullptr;
  }

  mm->in_progress = false;
  mm->active = false;
  mm->reporting_enabled = false;

  return mm;
}

void mouse_manager_destroy(MouseManager *mm) {
  if (mm) {
    free(mm);
  }
}

void mouse_manager_start_selection(MouseManager *mm, size_t row, size_t col) {
  if (!mm)
    return;

  mm->in_progress = true;
  mm->active = false;
  mm->start_row = row;
  mm->start_col = col;
  mm->end_row = row;
  mm->end_col = col;

  normalize_selection(mm);
}

void mouse_manager_select_range(MouseManager *mm, size_t start_row,
                                size_t start_col, size_t end_row,
                                size_t end_col) {
  if (!mm) {
    return;
  }

  mm->in_progress = true;
  mm->active = true;
  mm->start_row = start_row;
  mm->start_col = start_col;
  mm->end_row = end_row;
  mm->end_col = end_col;
  normalize_selection(mm);
}

void mouse_manager_select_word(MouseManager *mm, const TerminalBuffer *tb,
                               size_t row, size_t col) {
  size_t begin = 0;
  size_t end = 0;

  if (!mm) {
    return;
  }
  if (!terminal_buffer_word_range(tb, row, col, &begin, &end) || begin >= end) {
    mouse_manager_clear_selection(mm);
    return;
  }
  mouse_manager_select_range(mm, row, begin, row, end - 1);
}

void mouse_manager_select_line(MouseManager *mm, const TerminalBuffer *tb,
                               size_t row) {
  size_t begin = 0;
  size_t end = 0;

  if (!mm) {
    return;
  }
  if (!terminal_buffer_line_range(tb, row, &begin, &end) || begin >= end) {
    mouse_manager_clear_selection(mm);
    return;
  }
  mouse_manager_select_range(mm, row, begin, row, end - 1);
}

void mouse_manager_update_selection(MouseManager *mm, size_t row, size_t col) {
  if (!mm || !mm->in_progress)
    return;

  mm->end_row = row;
  mm->end_col = col;

  normalize_selection(mm);
}

void mouse_manager_finish_selection(MouseManager *mm) {
  if (!mm || !mm->in_progress)
    return;

  mm->in_progress = false;

  /* Drag selections need more than one cell. Word/line ranges may be a
   * single cell (select_range already set active). */
  if (mm->norm_start_row != mm->norm_end_row ||
      mm->norm_start_col != mm->norm_end_col) {
    mm->active = true;
  }
}

void mouse_manager_clear_selection(MouseManager *mm) {
  if (!mm)
    return;

  mm->in_progress = false;
  mm->active = false;
}

void mouse_manager_focus_lost(MouseManager *mm) {
  if (mm && mm->in_progress) {
    mouse_manager_clear_selection(mm);
  }
}

bool mouse_manager_is_selected(const MouseManager *mm, size_t row, size_t col) {
  if (!mm || (!mm->in_progress && !mm->active)) {
    return false;
  }

  /* Check if (row, col) is within [norm_start, norm_end] */

  /* Before selection start */
  if (row < mm->norm_start_row)
    return false;
  if (row == mm->norm_start_row && col < mm->norm_start_col)
    return false;

  /* After selection end */
  if (row > mm->norm_end_row)
    return false;
  if (row == mm->norm_end_row && col > mm->norm_end_col)
    return false;

  return true;
}

char *mouse_manager_get_selected_text(const MouseManager *mm,
                                      const TerminalBuffer *tb) {
  if (!mm || !tb || (!mm->in_progress && !mm->active)) {
    return strdup(""); /* Return empty string */
  }

  /* Estimate buffer size: assume max line length + newlines, UTF-8 worst case
   */
  size_t row_count = mm->norm_end_row - mm->norm_start_row + 1;
  size_t cols_plus_nl;
  size_t estimated_chars;
  size_t estimated_bytes;

  size_t tb_cols = terminal_buffer_get_cols(tb);
  if (size_add_overflow(tb_cols, 1, &cols_plus_nl) ||
      size_mul_overflow(row_count, cols_plus_nl, &estimated_chars) ||
      size_add_overflow(estimated_chars, 1, &estimated_chars) ||
      size_mul_overflow(estimated_chars, 4, &estimated_bytes) ||
      estimated_bytes < 1) {
    return nullptr;
  }

  char *buffer = checked_malloc(estimated_bytes);
  if (!buffer) {
    return nullptr;
  }

  size_t buf_pos = 0;

  for (size_t row = mm->norm_start_row;
       row <= mm->norm_end_row && row < terminal_buffer_get_rows(tb); row++) {
    size_t start_col = (row == mm->norm_start_row) ? mm->norm_start_col : 0;
    size_t end_col = (row == mm->norm_end_row) ? mm->norm_end_col : tb_cols - 1;

    /* Clamp to valid range */
    if (end_col >= tb_cols)
      end_col = tb_cols - 1;

    /* Copy characters from this row */
    size_t last_non_space = start_col;
    for (size_t col = start_col; col <= end_col; col++) {
      const TermCell *cell = terminal_buffer_get_cell_const(tb, row, col);
      if (cell && (cell->flags & CELL_FLAG_CONTINUATION) != 0) {
        continue;
      }
      if (cell && cell->codepoint != 0 && cell->codepoint != ' ') {
        last_non_space = col;
        if (cell->width == 2 && col + 1 <= end_col) {
          last_non_space = col + 1;
        }
        /* Include trailing combining marks */
        for (size_t next = col + (cell->width == 2 ? 2u : 1u); next <= end_col;
             next++) {
          const TermCell *next_cell =
              terminal_buffer_get_cell_const(tb, row, next);
          if (!next_cell || next_cell->width != 0 ||
              (next_cell->flags & CELL_FLAG_CONTINUATION) != 0) {
            break;
          }
          last_non_space = next;
        }
      }
    }

    /* Copy up to last non-space character (trim trailing spaces) */
    for (size_t col = start_col; col <= last_non_space; col++) {
      const TermCell *cell = terminal_buffer_get_cell_const(tb, row, col);
      if (cell && (cell->flags & CELL_FLAG_CONTINUATION) != 0) {
        continue;
      }
      if (cell && cell->codepoint != 0) {
        char utf8[5];
        int len = codepoint_to_utf8(cell->codepoint, utf8);
        if (!selection_append(buffer, &buf_pos, estimated_bytes, utf8,
                              (size_t)len)) {
          buffer[buf_pos] = '\0';
          return buffer;
        }
      } else if (!selection_append(buffer, &buf_pos, estimated_bytes, " ", 1)) {
        buffer[buf_pos] = '\0';
        return buffer;
      }
    }

    /* Add newline if not last row */
    if (row < mm->norm_end_row) {
      if (!selection_append(buffer, &buf_pos, estimated_bytes, "\n", 1)) {
        buffer[buf_pos] = '\0';
        return buffer;
      }
    }
  }

  if (buf_pos >= estimated_bytes) {
    buf_pos = estimated_bytes - 1;
  }
  buffer[buf_pos] = '\0';
  return buffer;
}

void mouse_manager_set_reporting(MouseManager *mm, bool enabled) {
  if (mm) {
    mm->reporting_enabled = enabled;
  }
}

bool mouse_manager_is_reporting_enabled(const MouseManager *mm) {
  return mm && mm->reporting_enabled;
}

bool mouse_manager_is_selecting(const MouseManager *mm) {
  return mm && (mm->in_progress || mm->active);
}

bool mouse_manager_is_dragging(const MouseManager *mm) {
  return mm && mm->in_progress;
}
