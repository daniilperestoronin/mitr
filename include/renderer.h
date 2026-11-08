/**
 * renderer.h - Terminal renderer using SDL2
 *
 * Renders terminal buffer to SDL window.
 */

#ifndef MITR_RENDERER_H
#define MITR_RENDERER_H

#include "common.h"
#include "config.h"
#include "font_manager.h"
#include "mouse.h"
#include "terminal_buffer.h"
#include <SDL2/SDL.h>

typedef struct Renderer Renderer;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * Initialize renderer
 *
 * @param window SDL window
 * @param sdl_renderer SDL renderer
 * @param font_manager Font manager to use
 * @return Pointer to Renderer on success, nullptr on failure
 */
Renderer *renderer_init(SDL_Window *window, SDL_Renderer *sdl_renderer,
                        FontManager *font_manager);

/**
 * Destroy renderer
 *
 * @param r Renderer to destroy
 */
void renderer_destroy(Renderer *r);

/**
 * Render terminal buffer to screen
 *
 * Every call clears and rebuilds the complete visible grid in a retained
 * drawable-sized target texture, then blits the whole texture to the window.
 * If the texture is unavailable, the complete grid is painted directly to
 * the window. The rendering model is identical on every SDL backend and never
 * depends on pixels retained from an earlier frame.
 *
 * @param r Renderer
 * @param tb Terminal buffer to render
 * @param mouse_manager Mouse manager for selection rendering (can be nullptr)
 */
void renderer_render(Renderer *r, TerminalBuffer *tb,
                     const MouseManager *mouse_manager);

/**
 * Get terminal size in characters based on window size
 *
 * @param r Renderer
 * @param rows Output: number of rows
 * @param cols Output: number of columns
 */
void renderer_get_term_size(Renderer *r, size_t *rows, size_t *cols);

/**
 * Update renderer after window resize
 *
 * Recreates the retained frame texture at the new drawable size and
 * treats the next present as a full redraw.
 *
 * @param r Renderer
 * @param new_width New window width in pixels
 * @param new_height New window height in pixels
 */
void renderer_update_size(Renderer *r, int new_width, int new_height);

/**
 * Request a complete redraw. Every renderer_render already rebuilds the whole
 * frame, so this remains as an explicit, null-safe caller intent marker.
 */
void renderer_force_full_redraw(Renderer *r);

/**
 * Apply configured appearance (padding, cursor, selection).
 */
void renderer_set_appearance(Renderer *r, int padding_x, int padding_y,
                             uint32_t cursor_color, CursorStyle cursor_style,
                             uint32_t selection_color);

/**
 * Compute terminal grid size for a window-coordinate size, accounting for
 * padding. Cell size is logical (window pixels), not drawable.
 */
void renderer_compute_grid(const Renderer *r, int pixel_w, int pixel_h,
                           size_t *rows, size_t *cols);

/**
 * Logical (window-coordinate) cell size used by the grid and mouse mapping.
 * SDL events are in window pixels; dest-rects use drawable pixels.
 */
void renderer_get_logical_cell_size(const Renderer *r, int *width, int *height);

/**
 * Drawable step between adjacent cells, derived from the logical cell size.
 * Backgrounds, glyphs, cursor, and the search bar must step by this pitch
 * rather than raw TTF char_width/char_height: those may not be divisible
 * by the HiDPI scale (e.g. 19px glyph, scale 2 → logical 10, pitch 20).
 */
int renderer_cell_pitch(int logical_px, float scale);

/**
 * Highlight a match on the current view (same color as selection).
 * ncells is the number of grid columns spanned; disabled when enabled is false.
 */
void renderer_set_search_hit(Renderer *r, bool enabled, size_t view_row,
                             size_t view_col, size_t ncells);

/**
 * Bottom-of-window query bar. Null clears the overlay.
 */
void renderer_set_search_prompt(Renderer *r, const char *text_or_null);

#endif /* MITR_RENDERER_H */
