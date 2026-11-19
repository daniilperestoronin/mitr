# mitr architecture

mitr runs a shell on a POSIX PTY, parses its output into a cell grid, and
draws that grid with SDL2. Modules are flat, with explicit ownership.
For build instructions and supported sequences, see [README.md](README.md).
For contribution rules, see [AGENTS.md](AGENTS.md).

## Modules and ownership

Headers live in `include/`, implementations in `src/`, with matching names.

| Module | Responsibility |
|--------|----------------|
| `common.h` | Error codes, colors, checked allocation, size and display-scale helpers |
| `terminal_buffer` | Cells, cursor, scrolling, alternate screen, history, hyperlink IDs, search |
| `unicode` | Character width and combining marks |
| `ansi_parser` | VT state machine, buffer updates, terminal modes and callbacks |
| `input_encoder` | Key, paste, mouse, and focus sequences; no SDL |
| `app_input` | SDL event routing, shortcuts, clipboard, mouse capture |
| `app_search` | Search query, wraparound, and revealing matches; no SDL, optional renderer |
| `pty_manager` | Shell process, PTY reads, queued writes, resize |
| `renderer` | Full-frame SDL2 drawing, cursor, selection, search overlay |
| `font_manager` | Font faces, glyph atlas, fallback cache |
| `font_resolver` | Fontconfig/CoreText family and fallback lookup |
| `mouse` | Selection ranges and text extraction |
| `config` | YAML settings, defaults, OSC 52 policy |
| `main.c` | Initialization, event loop, parser callbacks, cleanup |

`AppState` in `main.c` owns the application resources. The PTY is started
before SDL; cleanup handles partial initialization. `TerminalBuffer`,
`AnsiParser`, `PtyManager`, `Renderer`, `FontManager`, `FontResolver`, and
`MouseManager` are opaque. `TermCell` is public; its layout is defined in
[terminal_buffer.h](include/terminal_buffer.h).

SDL types stay in the renderer/font initialization APIs, the app layer,
and SDL tests. Special keys, paste, and mouse reports pass through
`app_input` and `input_encoder`, rather than being encoded in `main.c`.

## Event loop

```text
SDL events → app_input → app_search (local query)
                      → input_encoder → PTY → shell

shell → PTY → ansi_parser → terminal_buffer → renderer → SDL_RenderPresent
```

The loop reads PTY output in bounded bursts so input and drawing can still
run. Resize events are coalesced and applied after event handling. Parser
callbacks deliver window titles, clipboard writes, device replies, and
resize requests to the app. Replies use `app_input_pty_write`; the parser
has no SDL or direct PTY dependency. An unset callback drops its output.

Synchronized output (DEC 2026) continues to update the buffer while holding
presentation. Reset presents the accumulated changes; a 150 ms timeout
also releases the hold. Each enable increments `ansi_parser_sync_generation`
and restarts the timer, including a disable/enable pair within one read.

## Screen and history

`TerminalBuffer` owns a flat `rows * cols` grid, cursor and saved cursor,
attributes, tab stops, scrolling region, alternate screen, and a history
ring. Resize stages allocations before changing the live state. Callers
must not retain cell or row pointers across resize, scrolling, clearing,
or destruction.

`TermCell` stores a codepoint, foreground/background colors, attributes,
width, and continuation flags. Grid width comes from `unicode_wcwidth`,
not font metrics. Insert mode shifts cells by the incoming character's
width; combining marks do not shift the line. Soft reset restores replace mode.

Hyperlinks use parallel `uint16_t` arrays for the main grid, alternate grid,
and history. ID 0 means no link; IDs 1–64 refer to interned URIs of at most
2048 bytes each. A full table ignores new links. Cell moves, resizing, and
screen swaps must move the matching link IDs too.

Dirty-row bits and a coalesced scroll shift record buffer changes. The
renderer currently repaints the whole visible grid and clears this metadata
after a completed presentation.

### Search

History row 0 is the oldest retained line; the last `rows` entries are the
live screen. `terminal_buffer_get_history_row` maps this index into the ring
or grid. Total length is `scrollback_used + rows`.

`terminal_buffer_search` decodes up to 256 query codepoints on the stack
and scans without allocating or wrapping. Case-insensitive matching folds
ASCII A–Z only. Matches cannot start on continuation or zero-width cells;
continuation cells are skipped while matching.

`app_search` owns wraparound and keeps query text out of the PTY. It searches
only live rows on the alternate screen. A history hit is revealed at view
offset `scrollback_used - hit_row`; a live-screen hit resets the offset to 0.

## Parser

```text
GROUND ──ESC──> ESCAPE
                  ├──[──> CSI ──digit/;──> CSI_PARAM ──final──> GROUND
                  ├──]──> OSC ──BEL/ST──> GROUND
                  ├──P/_/^/X──> IGNORE_ST ──terminator──> GROUND
                  ├──(/)──> CHARSET ──> GROUND
                  └──other──> execute, GROUND
```

CSI accepts up to 16 numeric parameters. Private prefixes are stored as
characters, keeping DEC `?` modes distinct from secondary DA (`>`).
The last graphic character used by REP is updated only by printable/UTF-8
output in GROUND, not by control characters.

OSC uses a 64 KiB buffer allocated at initialization. Oversized sequences
are dropped rather than executed as truncated titles or clipboard writes.
OSC 0/2 call the title callback. OSC 8 sets the current hyperlink; an empty
URI closes it, and `id=` parameters are ignored. OSC 52 decodes base64 and
uses a clipboard callback registered only when the configured policy allows
it. The app handles confirmation; see [clipboard settings](README.md#configuration).

DCS, APC, PM, and SOS enter IGNORE_ST until BEL, ST, CAN, or SUB. Their
payloads are not interpreted.

`AnsiParserModes` exposes a snapshot of keypad, cursor-key, paste, mouse,
alternate-scroll, focus, and synchronized-output modes. Mouse tracking modes
1000/1002/1003 are mutually exclusive. RIS restores defaults, including
enabled alternate scroll.

## Input and selection

`app_mods_from_sdl` maps Shift, left/right Alt, and Ctrl to `InputKeyMods`;
Mode and GUI are excluded. `input_encoder_encode_key_ex` encodes modifiers,
and `input_encoder_encode_key` supplies the unmodified form. Alt+letter
uses `input_encoder_encode_alt_char`; the following SDL text event is
suppressed to avoid sending the character twice.

Wheel routing selects local history, mouse reports, or alternate-screen
arrow keys according to search, Shift, and parser modes. Fractional X/Y
deltas accumulate separately per route, with perpendicular noise suppressed.
Focus events produce reports only when mode 1004 is enabled.

`AppState` stores a PTY press snapshot for each of the three mouse buttons.
A successfully queued press enables SDL capture. Its release uses the original
encoding even after a mode change or a Shift press; focus loss releases all
outstanding buttons. A local Shift+drag creates no PTY press snapshot.
New clicks, hover, and wheel events must hit the grid; active drags and their
releases clamp to its edges. Local word/line selection uses buffer range
helpers. Link opening allows only HTTP and HTTPS.

## Rendering and fonts

The app requests an accelerated SDL2 renderer with vsync. There are no
custom shaders. Each frame clears and repaints every visible row into a
retained, drawable-sized target texture, then copies the entire texture to
the window with `SDL_BLENDMODE_NONE` and presents it.

If the target cannot be created or rebound after an atlas update, the
renderer discards it and paints the full frame directly to the window.
That fallback continues until resize recreates the texture. Rendering does
not depend on retained swapchain pixels or dirty-row scroll copies.
`renderer_force_full_redraw` is a null-safe intent marker; every render is
already complete.

Drawing fills backgrounds, copies glyphs, then adds the cursor, selection,
and search overlay. The cursor is hidden while viewing history. The search
prompt occupies a bottom row and uses the selection color for matches.
Any partial row below the grid keeps the configured background color.

### HiDPI coordinates

The grid and input use window coordinates. Fonts load at the configured
size multiplied by drawable/window scale. Drawing uses the drawable pitch
of each logical cell, rather than raw font metrics: a 19-pixel glyph at
scale 2 gives a 10-unit logical cell and a 20-pixel drawing pitch. Glyphs
are clipped or centered in that box. Mouse mapping and the search bar use
the same cell dimensions.

### Glyph cache and fallback

A single texture atlas caches `(codepoint, foreground, bold, italic, font)`.
Cache misses rasterize through SDL_ttf; a full atlas resets. Atlas updates
change the SDL render target, so the renderer must rebind its frame texture.
Italic styling is restored after each glyph. SGR reverse and reverse-screen
mode each swap foreground/background; setting both cancels the swap. Dim
and concealed attributes apply afterward.

`FontManager` tries the primary font, an explicit fallback, native system
fallbacks, then emergency faces. It caches at most 16 lazily opened system
faces. A missing explicit fallback is a warning, not an initialization failure.

`font_resolver` resolves family names through Fontconfig on Linux or CoreText
on macOS. Initialization builds an ordered fallback list and retains character
coverage, discarding color-only faces and faces that add no coverage. Drawing
queries this list without repeating native font discovery. Fontconfig preserves
collection face indexes; CoreText collections are scanned for matching family
and style. Fallback glyph advances never change the grid width.

## PTY and shell

Before forking, `pty_manager_shell_path_valid` requires an absolute path
without `..`, pointing to a regular executable that is not world-writable.
The parent opens a nonblocking, close-on-exec master PTY and grants/unlocks
its slave. The child creates a session, opens the controlling slave, sets
terminal attributes and size, connects standard streams, then calls `execve`.

The child environment contains `TERM`, `LANG`, `LC_ALL`, `LC_CTYPE`, `PATH`,
`HOME`, `USER`, and `SHELL`, plus `SSH_AUTH_SOCK` and `DISPLAY` when set.
It also sets `COLORTERM=truecolor` and an empty `PROMPT_EOL_MARK`.
Other parent variables are not passed through. Shell choice, login mode,
and working directory behavior are described in [README.md](README.md#use).
The locale variables all use the parent's `LANG` when it contains `UTF-8`,
otherwise `en_US.UTF-8`.

`pty_manager_read` polls for up to 10 ms; `pty_manager_read_available` reads
without waiting. Writes use a bounded 256 KiB queue and may accept only part
of a payload. Accepted bytes may still be queued. `pty_manager_write_all`
waits for progress with 50 ms polls, stopping after 40 consecutive idle polls.
Callers must handle errors and partial acceptance.

Resize uses `TIOCSWINSZ`. Destruction closes the master to trigger SIGHUP,
waits briefly, then kills and reaps a child that has not exited. Windows platform
macros exist, but there is no ConPTY implementation.
