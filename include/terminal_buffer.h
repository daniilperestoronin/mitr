/**
 * terminal_buffer.h - Terminal screen buffer
 *
 * Manages the terminal screen state: cells, cursor, colors.
 */

#ifndef MITR_TERMINAL_BUFFER_H
#define MITR_TERMINAL_BUFFER_H

#include "common.h"
#include "unicode.h"

/* TermCell flags (stored in TermCell.flags) */
#define CELL_FLAG_CONTINUATION (1u << 0)

/* ========================================================================
 * Terminal Cell Structure
 * ======================================================================== */

typedef struct {
  uint32_t codepoint; /* Character (Unicode codepoint) */
  uint32_t fg_color;  /* Foreground color (RGB888) */
  uint32_t bg_color;  /* Background color (RGB888) */
  uint8_t attributes; /* Text attributes (bold, underline, etc.) */
  uint8_t width;      /* 0=combining/empty, 1=narrow, 2=wide */
  uint8_t flags;      /* CELL_FLAG_CONTINUATION, etc. */
} TermCell;

/* ========================================================================
 * Terminal Buffer (opaque)
 * ======================================================================== */

/**
 * Opaque terminal screen state. Created by terminal_buffer_init(); valid
 * until terminal_buffer_destroy(). TermCell pointers from get_cell /
 * get_view_row are invalidated by resize, clear, scroll, and destroy.
 */
typedef struct TerminalBuffer TerminalBuffer;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * Initialize terminal buffer
 *
 * @param rows Number of rows (1..MITR_MAX_ROWS)
 * @param cols Number of columns (1..MITR_MAX_COLS)
 * @return Pointer to TerminalBuffer on success, nullptr on failure
 */
TerminalBuffer *terminal_buffer_init(size_t rows, size_t cols);

/**
 * Destroy terminal buffer and free resources
 *
 * @param tb TerminalBuffer to destroy
 */
void terminal_buffer_destroy(TerminalBuffer *tb);

/**
 * Set default foreground and background colors
 *
 * @param tb TerminalBuffer to update
 * @param fg_color Foreground color (RGB888)
 * @param bg_color Background color (RGB888)
 */
void terminal_buffer_set_colors(TerminalBuffer *tb, uint32_t fg_color,
                                uint32_t bg_color);

/**
 * Clear entire buffer (fill with spaces and default colors)
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_clear(TerminalBuffer *tb);

/**
 * Put a character at current cursor position and advance cursor
 *
 * @param tb TerminalBuffer
 * @param ch Character to write (ASCII)
 */
void terminal_buffer_put_char(TerminalBuffer *tb, char ch);

/**
 * Put a Unicode codepoint at current cursor position and advance cursor
 *
 * @param tb TerminalBuffer
 * @param codepoint Unicode codepoint (UTF-32)
 */
void terminal_buffer_put_codepoint(TerminalBuffer *tb, uint32_t codepoint);

/**
 * Get cell at specific position
 *
 * @param tb TerminalBuffer
 * @param row Row index
 * @param col Column index
 * @return Pointer to TermCell, or nullptr if out of bounds
 */
TermCell *terminal_buffer_get_cell(TerminalBuffer *tb, size_t row, size_t col);

/**
 * Get cell at specific position (const version)
 */
const TermCell *terminal_buffer_get_cell_const(const TerminalBuffer *tb,
                                               size_t row, size_t col);

/**
 * Set cursor position
 *
 * @param tb TerminalBuffer
 * @param row Row index (0-based)
 * @param col Column index (0-based)
 */
void terminal_buffer_set_cursor(TerminalBuffer *tb, size_t row, size_t col);

/**
 * Snap column leftward off wide-character continuation cells.
 */
size_t terminal_buffer_snap_column(const TerminalBuffer *tb, size_t row,
                                   size_t col);

/**
 * Get cursor position
 *
 * @param tb TerminalBuffer
 * @param row Output: cursor row
 * @param col Output: cursor column
 */
void terminal_buffer_get_cursor(TerminalBuffer *tb, size_t *row, size_t *col);

/**
 * Scroll buffer up by one line (bottom line becomes empty)
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_scroll_up(TerminalBuffer *tb);

/**
 * Set scrolling region (DECSTBM)
 *
 * @param tb TerminalBuffer
 * @param top Top row of scrolling region (0-based, inclusive)
 * @param bottom Bottom row of scrolling region (0-based, inclusive)
 */
void terminal_buffer_set_scroll_region(TerminalBuffer *tb, size_t top,
                                       size_t bottom);

/**
 * Reset scrolling region to full screen
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_reset_scroll_region(TerminalBuffer *tb);

/**
 * Insert lines at cursor position within scrolling region
 * Lines below cursor are pushed down, lines pushed past bottom are lost
 *
 * @param tb TerminalBuffer
 * @param count Number of lines to insert
 */
void terminal_buffer_insert_lines(TerminalBuffer *tb, size_t count);

/**
 * Delete lines at cursor position within scrolling region
 * Lines below cursor are pulled up, blank lines are added at bottom
 *
 * @param tb TerminalBuffer
 * @param count Number of lines to delete
 */
void terminal_buffer_delete_lines(TerminalBuffer *tb, size_t count);

/**
 * VT100: IND (Index) - Move cursor down, scroll if at bottom of scroll region
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_index(TerminalBuffer *tb);

/**
 * VT100: RI (Reverse Index) - Move cursor up, scroll down if at top of scroll
 * region
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_reverse_index(TerminalBuffer *tb);

/**
 * VT100: Scroll down within scrolling region (opposite of scroll_up)
 * Used by RI when cursor is at top of scroll region
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_scroll_down(TerminalBuffer *tb);

/**
 * VT102: Insert characters at cursor position
 * Characters to the right are shifted right, characters pushed past end are
 * lost
 *
 * @param tb TerminalBuffer
 * @param count Number of characters to insert
 */
void terminal_buffer_insert_chars(TerminalBuffer *tb, size_t count);

/**
 * VT102: Delete characters at cursor position
 * Characters to the right are shifted left, spaces are added at end
 *
 * @param tb TerminalBuffer
 * @param count Number of characters to delete
 */
void terminal_buffer_delete_chars(TerminalBuffer *tb, size_t count);

/**
 * VT102: Erase characters at cursor position (replace with spaces)
 * Cursor does not move
 *
 * @param tb TerminalBuffer
 * @param count Number of characters to erase
 */
void terminal_buffer_erase_chars(TerminalBuffer *tb, size_t count);

/**
 * VT220: Scroll up n lines within scrolling region
 *
 * @param tb TerminalBuffer
 * @param count Number of lines to scroll up
 */
void terminal_buffer_scroll_up_n(TerminalBuffer *tb, size_t count);

/**
 * VT220: Scroll down n lines within scrolling region
 *
 * @param tb TerminalBuffer
 * @param count Number of lines to scroll down
 */
void terminal_buffer_scroll_down_n(TerminalBuffer *tb, size_t count);

/**
 * VT220: Set tab stop at current cursor column
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_set_tab_stop(TerminalBuffer *tb);

/**
 * VT220: Clear tab stop at current cursor column
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_clear_tab_stop(TerminalBuffer *tb);

/**
 * VT220: Clear all tab stops
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_clear_all_tab_stops(TerminalBuffer *tb);

/**
 * VT220: Get next tab stop position from current column
 *
 * @param tb TerminalBuffer
 * @return Next tab stop column, or cols if no more tab stops
 */
size_t terminal_buffer_get_next_tab_stop(TerminalBuffer *tb);

/**
 * VT220: Get previous tab stop from current column (CBT).
 *
 * @param tb TerminalBuffer
 * @return Previous tab stop column, or 0 if none
 */
size_t terminal_buffer_get_prev_tab_stop(TerminalBuffer *tb);

/**
 * VT220: Soft reset - reset terminal to initial state
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_soft_reset(TerminalBuffer *tb);

/**
 * VT320: Resize terminal buffer columns (for DECCOLM)
 *
 * @param tb TerminalBuffer
 * @param new_cols New number of columns (80 or 132)
 * @return MITR_OK on success, MITR_ERROR_INVALID_PARAM or MITR_ERROR_MEMORY
 */
MitrError terminal_buffer_resize_columns(TerminalBuffer *tb, size_t new_cols);

/**
 * Resize terminal buffer to new dimensions
 * Preserves content where possible (top-left aligned).
 * Transactional: on failure, existing buffers and dimensions are unchanged.
 *
 * @param tb TerminalBuffer
 * @param new_rows New number of rows (1..MITR_MAX_ROWS)
 * @param new_cols New number of columns (1..MITR_MAX_COLS)
 * @return MITR_OK on success, MITR_ERROR_INVALID_PARAM or MITR_ERROR_MEMORY
 */
MitrError terminal_buffer_resize(TerminalBuffer *tb, size_t new_rows,
                                 size_t new_cols);

/**
 * XTERM: Enter alternate screen buffer
 * Saves current screen content and cursor, switches to alternate buffer
 *
 * @param tb TerminalBuffer
 * @return MITR_OK on success, MITR_ERROR_INVALID_PARAM or MITR_ERROR_MEMORY
 */
MitrError terminal_buffer_enter_alt_screen(TerminalBuffer *tb);

/**
 * XTERM: Exit alternate screen buffer
 * Restores saved screen content and cursor from main buffer
 *
 * @param tb TerminalBuffer
 */
void terminal_buffer_exit_alt_screen(TerminalBuffer *tb);

/**
 * XTERM: Check if using alternate screen
 *
 * @param tb TerminalBuffer
 * @return true if using alternate screen, false otherwise
 */
bool terminal_buffer_is_alt_screen(TerminalBuffer *tb);

/**
 * Set the scrollback buffer capacity (number of rows).
 * Reallocates the buffer; existing scrollback content is discarded.
 *
 * @param tb TerminalBuffer
 * @param capacity Maximum number of scrollback rows (0 disables scrollback)
 * @return MITR_OK on success, MITR_ERROR_INVALID_PARAM or MITR_ERROR_MEMORY
 */
MitrError terminal_buffer_set_scrollback_capacity(TerminalBuffer *tb,
                                                  size_t capacity);

/**
 * Get a row of cells for display, accounting for scrollback offset.
 * When scroll_offset == 0, returns the live screen row.
 * When scroll_offset > 0, may return a row from the scrollback buffer.
 *
 * @param tb TerminalBuffer
 * @param display_row Row index on screen (0-based)
 * @return Pointer to the row's cells, or nullptr on error
 */
const TermCell *terminal_buffer_get_view_row(const TerminalBuffer *tb,
                                             size_t display_row);

/**
 * Scroll the view into the scrollback buffer.
 * Positive delta scrolls toward history, negative toward live.
 *
 * @param tb TerminalBuffer
 * @param delta Number of rows to scroll (positive = into history)
 */
void terminal_buffer_scroll_view(TerminalBuffer *tb, int delta);

/**
 * Get the current scrollback view offset.
 *
 * @param tb TerminalBuffer
 * @return Current scroll_offset (0 = live view)
 */
size_t terminal_buffer_get_scroll_offset(const TerminalBuffer *tb);

/**
 * Number of scrollback rows currently stored (0 if none).
 */
size_t terminal_buffer_get_scrollback_used(const TerminalBuffer *tb);

/**
 * History rows: 0 = oldest scrollback, last `rows` = live screen.
 * Total history rows = scrollback_used + rows (live always included).
 * Invalid hist_row returns nullptr.
 */
const TermCell *terminal_buffer_get_history_row(const TerminalBuffer *tb,
                                                size_t hist_row);

/**
 * Search scrollback + live grid for a UTF-8 query. When case_insensitive
 * is true, ASCII A–Z fold to a–z; other codepoints compare literally.
 * No regex. Coordinates are history rows/cols. Start is inclusive. Does
 * not wrap: forward stops at the last history cell, backwards at the first.
 * Empty or nullptr query, invalid UTF-8, a query longer than 256
 * codepoints, or a start past the end returns false. No heap allocation.
 * A match cannot start on a continuation or zero-width cell;
 * continuation cells are skipped when walking the haystack.
 */
bool terminal_buffer_search(const TerminalBuffer *tb, const char *query,
                            bool backwards, bool case_insensitive,
                            size_t start_row, size_t start_col, size_t *out_row,
                            size_t *out_col);

/**
 * Set the current OSC 8 hyperlink for subsequent put_codepoint writes.
 * Null or empty URI clears the current link (id 0). Non-empty URIs are
 * interned (max 64 unique, 2048 bytes each); a full table leaves id 0.
 */
void terminal_buffer_set_hyperlink(TerminalBuffer *tb, const char *uri_or_null);

/**
 * URI at a view cell (same coordinates as selection / get_view_row).
 * Returns the interned string, or nullptr if the cell has no link.
 */
const char *terminal_buffer_get_hyperlink_at(const TerminalBuffer *tb,
                                             size_t row, size_t col);

/**
 * Word span on a view row as half-open [col_begin, col_end).
 * A word is a run of cells whose codepoint is not space and that are not
 * width-0 continuation cells. Clicking a continuation snaps begin to the
 * base cell. Empty / space cells yield begin == end. False on NULL, bad
 * view row/col, or NULL out pointers.
 */
bool terminal_buffer_word_range(const TerminalBuffer *tb, size_t view_row,
                                size_t col, size_t *col_begin, size_t *col_end);

/**
 * Line span on a view row as half-open [0, cols). False on NULL, a view
 * row outside the grid, or NULL out pointers.
 */
bool terminal_buffer_line_range(const TerminalBuffer *tb, size_t view_row,
                                size_t *col_begin, size_t *col_end);

/**
 * Mark every visible view-row dirty (resize, alt-screen swap, full redraw).
 * Also drops any pending live-grid scroll shift.
 */
void terminal_buffer_mark_all_dirty(TerminalBuffer *tb);

/**
 * Mark a live-grid row dirty. Maps to the current view_row (scroll_offset).
 */
void terminal_buffer_mark_row_dirty(TerminalBuffer *tb, size_t live_row);

/**
 * True if the displayed row needs a redraw. view_row uses get_view_row
 * coordinates (scroll_offset). Out of range or NULL is false.
 */
bool terminal_buffer_row_is_dirty(const TerminalBuffer *tb, size_t view_row);

/**
 * Clear all dirty bits after a successful presented frame.
 * Also drops any pending live-grid scroll shift.
 */
void terminal_buffer_clear_dirty(TerminalBuffer *tb);

/**
 * Consume the coalesced live-grid pixel shift, if any.
 * top/bottom are inclusive live-grid rows; delta is a signed row count
 * (positive = content moved up). Clears the pending shift. Out pointers
 * may be NULL (take and drop). False if tb is NULL or no shift is pending.
 */
bool terminal_buffer_take_scroll_shift(TerminalBuffer *tb, size_t *top,
                                       size_t *bottom, int *delta);

/* ---- Accessors (read-only state) ---- */

size_t terminal_buffer_get_rows(const TerminalBuffer *tb);
size_t terminal_buffer_get_cols(const TerminalBuffer *tb);
void terminal_buffer_get_dimensions(const TerminalBuffer *tb, size_t *rows,
                                    size_t *cols);
bool terminal_buffer_is_reverse_screen(const TerminalBuffer *tb);
uint32_t terminal_buffer_get_config_fg(const TerminalBuffer *tb);
uint32_t terminal_buffer_get_config_bg(const TerminalBuffer *tb);
bool terminal_buffer_is_cursor_visible(const TerminalBuffer *tb);
uint32_t terminal_buffer_get_default_fg(const TerminalBuffer *tb);
uint32_t terminal_buffer_get_default_bg(const TerminalBuffer *tb);
uint8_t terminal_buffer_get_current_attr(const TerminalBuffer *tb);
void terminal_buffer_get_scroll_region(const TerminalBuffer *tb, size_t *top,
                                       size_t *bottom);
bool terminal_buffer_is_origin_mode(const TerminalBuffer *tb);
bool terminal_buffer_is_auto_wrap(const TerminalBuffer *tb);
bool terminal_buffer_is_pending_wrap(const TerminalBuffer *tb);
size_t terminal_buffer_get_scrollback_capacity(const TerminalBuffer *tb);
bool terminal_buffer_has_scrollback(const TerminalBuffer *tb);

/* ---- Accessors (mutable state; prefer escape sequences in production) ---- */

void terminal_buffer_set_default_fg(TerminalBuffer *tb, uint32_t fg);
void terminal_buffer_set_default_bg(TerminalBuffer *tb, uint32_t bg);
void terminal_buffer_set_current_attr(TerminalBuffer *tb, uint8_t attr);
void terminal_buffer_set_pending_wrap(TerminalBuffer *tb, bool pending);
void terminal_buffer_set_origin_mode(TerminalBuffer *tb, bool enabled);
void terminal_buffer_set_auto_wrap(TerminalBuffer *tb, bool enabled);
void terminal_buffer_set_cursor_visible(TerminalBuffer *tb, bool visible);
void terminal_buffer_set_reverse_screen(TerminalBuffer *tb, bool enabled);
void terminal_buffer_set_insert_mode(TerminalBuffer *tb, bool enabled);
bool terminal_buffer_is_insert_mode(const TerminalBuffer *tb);

#endif /* MITR_TERMINAL_BUFFER_H */
