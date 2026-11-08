/**
 * font_manager.h - Font management for terminal rendering
 *
 * Handles loading and managing TrueType fonts using SDL_ttf.
 * All glyphs are rendered into a single GPU texture atlas so that every
 * SDL_RenderCopy in the hot loop draws from the same source texture,
 * allowing SDL2 to batch them into very few draw calls.
 */

#ifndef MITR_FONT_MANAGER_H
#define MITR_FONT_MANAGER_H

#include "common.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef struct FontManager FontManager;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * Initialize font manager and load font
 *
 * @param font_family Family resolved by the native platform font service
 * @param font_path Path to .ttf font file (nullptr/empty resolves font_family)
 * @param fallback_path Explicit second face (nullptr/empty = system fallback)
 * @param font_size Font size in drawable pixels (already scaled for HiDPI)
 * @param sdl_renderer SDL renderer for texture creation
 * @return Pointer to FontManager on success, nullptr on failure
 */
FontManager *font_manager_init(const char *font_family, const char *font_path,
                               const char *fallback_path, int font_size,
                               SDL_Renderer *sdl_renderer);

/**
 * Destroy font manager and free resources
 *
 * @param fm FontManager to destroy
 */
void font_manager_destroy(FontManager *fm);

/**
 * Get a glyph from the atlas.  On first call for a given glyph the
 * character is rasterised and copied into the atlas; subsequent calls
 * return the cached region in O(1).
 *
 * Applies VT100 attributes (bold, dim, reverse, concealed) internally.
 *
 * @param fm        FontManager
 * @param codepoint Unicode codepoint (UTF-32)
 * @param fg_color  Foreground color (RGB888)
 * @param bg_color  Background color (RGB888, used for reverse/concealed)
 * @param attributes VT100 text attributes
 * @param out_src   Output: source rectangle inside the atlas
 * @param out_width Output: glyph pixel width
 * @param out_height Output: glyph pixel height
 * @return The atlas SDL_Texture (do NOT destroy), or nullptr on error
 */
SDL_Texture *font_manager_get_glyph(FontManager *fm, uint32_t codepoint,
                                    uint32_t fg_color, uint32_t bg_color,
                                    uint8_t attributes, SDL_Rect *out_src,
                                    int *out_width, int *out_height);

/**
 * Clear all cached glyphs and reset the atlas
 *
 * @param fm FontManager
 */
void font_manager_clear_cache(FontManager *fm);

/**
 * Get character dimensions in drawable pixels (atlas / dest-rect space).
 *
 * @param fm FontManager
 * @param width Output: character width
 * @param height Output: character height
 */
void font_manager_get_char_size(FontManager *fm, int *width, int *height);

#endif /* MITR_FONT_MANAGER_H */
