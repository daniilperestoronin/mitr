/**
 * ansi_parser.h - ANSI escape sequence parser
 *
 * Parses ANSI/VT100 escape sequences and applies them to terminal buffer.
 */

#ifndef MITR_ANSI_PARSER_H
#define MITR_ANSI_PARSER_H

#include "common.h"
#include "terminal_buffer.h"

/* ========================================================================
 * Parser States
 * ======================================================================== */

typedef enum {
  PARSER_STATE_GROUND,    /* Normal text */
  PARSER_STATE_ESCAPE,    /* ESC received */
  PARSER_STATE_CSI,       /* CSI sequence (ESC [) */
  PARSER_STATE_CSI_PARAM, /* CSI parameters */
  PARSER_STATE_OSC,       /* OSC sequence (ESC ]) */
  PARSER_STATE_CHARSET,   /* Character set selection (ESC ( or ESC )) */
  PARSER_STATE_IGNORE_ST, /* DCS/APC/PM/SOS until ST (ESC \) or BEL */
} ParserState;

/* ========================================================================
 * ANSI Parser (opaque)
 * ======================================================================== */

/**
 * Opaque escape-sequence parser. Created by ansi_parser_init(); valid until
 * ansi_parser_destroy(). Feed with ansi_parser_feed() only while the paired
 * TerminalBuffer is alive and not destroyed.
 */
typedef struct AnsiParser AnsiParser;

/** Snapshot of input-relevant parser mode flags (DECCKM, DECKPAM, etc.). */
typedef struct {
  bool keypad_application_mode;
  bool cursor_keys_application_mode;
  bool bracketed_paste_mode;
  int mouse_report_mode; /* 0=none, 1000=normal, 1002=drag, 1003=motion */
  bool mouse_utf8_mode;
  bool mouse_sgr_mode;
  bool alternate_scroll_mode;
  bool focus_event_mode;
  bool synchronized_output;
} AnsiParserModes;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * Initialize ANSI parser
 *
 * @return Pointer to AnsiParser on success, nullptr on failure
 */
AnsiParser *ansi_parser_init(void);

/**
 * Destroy ANSI parser
 *
 * @param parser Parser to destroy
 */
void ansi_parser_destroy(AnsiParser *parser);

/**
 * Feed data to parser and update terminal buffer
 *
 * @param parser ANSI parser
 * @param data Data to parse
 * @param length Length of data
 * @param tb Terminal buffer to update
 */
void ansi_parser_feed(AnsiParser *parser, const char *data, size_t length,
                      TerminalBuffer *tb);

/**
 * Reset parser to ground state
 *
 * @param parser ANSI parser
 */
void ansi_parser_reset(AnsiParser *parser);

/**
 * Apply configured 16-color palette and bold-as-bright policy.
 * @param colors Array of 16 RGB colors (may be nullptr to keep current palette)
 */
void ansi_parser_set_palette(AnsiParser *parser, const uint32_t colors[16],
                             bool bold_is_bright);

/**
 * Register callback invoked after DECCOLM (and similar) changes buffer size.
 */
void ansi_parser_set_resize_callback(AnsiParser *parser,
                                     void (*callback)(void *userdata,
                                                      size_t rows, size_t cols),
                                     void *userdata);

/**
 * Register callback invoked for OSC 0 and OSC 2 window titles.
 */
void ansi_parser_set_title_callback(AnsiParser *parser,
                                    void (*callback)(void *userdata,
                                                     const char *title),
                                    void *userdata);

/**
 * Register callback invoked for DA / DSR replies (CSI c / CSI n).
 * If callback is nullptr, replies are discarded.
 */
void ansi_parser_set_reply_callback(AnsiParser *parser,
                                    void (*callback)(void *userdata,
                                                     const char *data,
                                                     size_t len),
                                    void *userdata);

/**
 * Register callback invoked for OSC 52 clipboard writes.
 * If callback is nullptr, writes are discarded. Queries (Pd == "?") never
 * invoke the callback.
 */
void ansi_parser_set_clipboard_callback(AnsiParser *parser,
                                        void (*callback)(void *userdata,
                                                         const char *text,
                                                         size_t len),
                                        void *userdata);

ParserState ansi_parser_get_state(const AnsiParser *parser);
size_t ansi_parser_get_param_count(const AnsiParser *parser);
void ansi_parser_get_modes(const AnsiParser *parser, AnsiParserModes *modes);

/** True while CSI ? 2026 h is in effect (synchronized output). */
bool ansi_parser_is_synchronized(const AnsiParser *parser);

/** Count of CSI ? 2026 h seen (0 if @p parser is nullptr). */
uint32_t ansi_parser_sync_generation(const AnsiParser *parser);

/* Present is deferred while synchronized until this many milliseconds elapse.
 */
#define MITR_SYNC_OUTPUT_TIMEOUT_MS 150u

/**
 * Whether a frame may be presented. While @p synchronized is true, Present
 * waits until (now_ms - started_ms) >= timeout_ms. Unsigned wrap is defined
 * (SDL_GetTicks).
 */
bool ansi_parser_sync_should_present(bool synchronized, uint32_t now_ms,
                                     uint32_t started_ms, uint32_t timeout_ms);

/**
 * Start time for the synchronized-output timeout. When @p generation differs
 * from @p seen_generation (a CSI ? 2026 h), returns @p now_ms so the timeout
 * restarts; otherwise returns @p started_ms.
 */
uint32_t ansi_parser_sync_started_ms(uint32_t generation,
                                     uint32_t seen_generation, uint32_t now_ms,
                                     uint32_t started_ms);

#endif /* MITR_ANSI_PARSER_H */
