# The card-game engine

FreeCell HD and Solitaire HD (and later Spider HD and Hearts HD) share one engine. The engine is
everything that is not one game's rules or look: images and card sprites, clipped drawing,
persistence, and the Win32 shell pieces every XP card game needs. Each game keeps its own rules,
controller, layout, renderer, resources and Win32 glue. docs/DESIGN.md describes FreeCell HD on top of
it.

## Layout

```
src/engine/            portable C99, no Windows headers, unit-tested natively (prefix ce_ / Ce)
  geom.h               CeRect and inline helpers (overlaps, union, clip)
  image.{h,c}          CeImage (premultiplied BGRA = a 32-bpp DIB), PNG decode (stb_image), resampling,
                       the card resampler, compositing, the card shape and frame, bevel rings
  cardset.{h,c}        CeCardSet: the 52 faces + optional backs at any size, lazy, two qualities,
                       memory policy, ring cache
  draw.{h,c}           CeDraw: clipped drawing (fill, sprites, inversion (also partial), XP bevel, HD ring,
                       R2_NOT frame)
  ease.{h,c}           easing curves (cubic-bezier, fixed-point tables) and flight durations (2d)
  store.{h,c}          CeStore (32-bit values by name), CeBlobIO (whole-file I/O), CRC-32
src/engine/win32/      Win32 shell pieces (XP SP2 APIs only); shell.h includes them all
  winutil.{h,c}        timing log, UTF-8 -> UTF-16, LoadString, RCDATA asset loader, time seed,
                       mouse-over test, message loop
  backbuf.{h,c}        CeBackBuf: DIB back buffer + scratch DIB, WM_PAINT, live-resize quality
  anim.{h,c}           CeAnimClock: timeBeginPeriod(1), frame pacing, eased flights (ce_anim_fly), the
                       flight scheduler CeFlights (overlapping cascades, flips)
  drag.{h,c}           CeDrag: cards dragged with the mouse (the lifted stack's sprite and its optional
                       shadow over the back buffer, the zip-back of a refused drop, the drag threshold)
  button.{h,c}         CeTableButton: a push button drawn on the table (the Finish button)
  menubar.{h,c}        CeMenuBarText: text at the right end of the menu bar ("Cards Left")
  window.{h,c}         window sizes, first-run rect, WindowPlacement load/fix/save, CeFullScreen
  regstore.{h,c}       registry CeStore (REG_BINARY or REG_DWORD, entpack.ini migration),
                       registry
                       blobs, %APPDATA%\xp-cards\<game> HD\<file>.bin (atomic writes)
  dialog.{h,c}         dialog centring / moving, clamped to the work area, logged
  help.{h,c}           HtmlHelp (.chm, run-time hhctrl.ocx), ShellAbout with writable copies
  worker.{h,c}         CeWorker: one background job at a time (FreeCell's solver)
src/freecell/          FreeCell HD, portable (prefix fc_ / fcs_): game, session, assist, solver, stats,
                       wondeals, layout, render, sprites (= the engine card set + the kings)
src/freecell/win32/    FreeCell HD's window, view, dialogs, storage keys, help text, solver thread
src/solitaire/         Solitaire HD, portable (prefix sol_ / Sol): game, session, cascade, layout, render,
                       winanim
src/solitaire/win32/   Solitaire HD's window, view (drag and drop, the win cascade), status bar, dialogs

res/common/            shared art: cards/ (52 x 400x560 PNG), cards-svg/, cards-src/ (provenance),
                       cards.rc (the 52 RCDATA lines, #included by each game's .rc)
res/freecell/          freecell.rc, resource.h, manifest, icon, cursor, king/ (+ sources), icon/ (tools)
res/solitaire/         solitaire.rc, resource.h, manifest
res/LICENSE-ART.md     licences and provenance of all art

tests/engine/          test_image.c, test_engine.c
tests/freecell/        the FreeCell suites, snapshots.c, solver_bench.c
tests/assets_native.h  the native asset loader (res/common/cards, res/freecell/king)
tests/e2e/             fchd_*.txt (FreeCell), solhd_*.txt (Solitaire), their runners
```

`make` builds `build/FreeCellHD.exe` and `build/SolitaireHD.exe`; the engine is linked as a static
library (`build/win/libengine.a`), so each exe takes only what it uses. `make test` runs the engine
suites, then each game's. Per game: `make freecell`, `make test-freecell`, `make xpcheck-freecell`,
`make e2e-freecell` (and the same for solitaire); `make snapshots` and `make solver-bench` are
FreeCell's.

## What a new game uses

**Card sprites.** `ce_cardset_new(loader, ctx, nbacks)` decodes the 52 faces (asset ids
`CE_ASSET_CARD0 + rank*4 + suit`, the cards.dll encoding: A = 0 .. K = 12, clubs, diamonds, hearts,
spades). Backs are asset ids `CE_ASSET_BACK0 + i` (up to `CE_MAX_BACKS` = 16), decoded on first use; a
missing or broken back gives NULL once and is not asked for again. A back master is cleaned like a face,
except that the band along its edge takes the colour just inside it (so a coloured back runs up to the
black frame). Any master size of the card's 5:7 shape works; the art is stretched to the game's cell.
`ce_cardset_set_size(cs, cw, ch, quality)` per layout: quality 0 while live-resizing
(`ce_backbuf_quality`), 1 otherwise; a fast request at the size already built keeps the HQ sprites.
`ce_cardset_card(cs, c)` / `ce_cardset_back(cs, i)` return cw x ch premultiplied sprites with
transparent rounded corners and XP's black frame. In an exe the loader is `ce_rcdata_loader` with the
HINSTANCE as ctx; asset ids 1100..1199 are free for a game's own images (FreeCell's kings).

**Drawing.** Render the board into the back buffer's `CeImage` region by region:
`ce_draw_begin(&d, fb, rect)`, then `ce_draw_fill`, `ce_draw_sprite` (inverted = XP's selection),
`ce_draw_invert_mask` (a card-shaped inversion), `ce_draw_bevel` (XP's 1-px bevel, width b),
`ce_draw_ring` (the smooth bevel for scaled boards, cached in the card set), `ce_draw_invert_frame`
(Solitaire's outline dragging). A clipped render gives exactly the pixels of a full one, so a game can
re-render only what changed (FreeCell's view.c diff model).

**Window.** `ce_window_default_rect(&r, client_for_scale)` (first run), `ce_placement_load` /
`ce_placement_fix` (min outer size from `ce_window_outer_size`) / `SetWindowPlacement`, and
`ce_fullscreen_save_placement` on WM_CLOSE / WM_ENDSESSION; key `CE_APP_KEY_ROOT L"<Game> HD"`.
Full screen: `ce_fullscreen_enter` / `ce_fullscreen_leave` (they return 1 when something changed: then
update the menu check mark and redraw the menu bar) and `ce_fullscreen_fit` on WM_DISPLAYCHANGE.

**Back buffer.** WM_SIZE: `ce_backbuf_ensure(&bb, w, h)`, lay out, `ce_backbuf_note_layout`, render;
WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE: `ce_backbuf_enter_sizemove` / `ce_backbuf_exit_sizemove` (returns 1:
lay out again at quality 1); WM_PAINT: `ce_backbuf_paint`; WM_ERASEBKGND: return 1. An animation frame
is `ce_backbuf_present(bb, dc, region, sprite, x, y, w, h)` (flicker-free, the back buffer untouched).

**Dragging.** `CeDrag` (Solitaire HD's drag, used by FreeCell HD's opt-in drag and drop too): the game
renders its board without the lifted cards and gives `ce_drag_begin` their sprite, rect and the grab
point; `ce_drag_move` follows the pointer (each move one flicker-free frame of the union of the old and
new rect), `ce_drag_paint` in WM_PAINT, `ce_drag_zip_back` slides a refused drop home (`ce_anim_fly`),
`ce_drag_end` invalidates the sprite's rect and frees it; `ce_drag_threshold_passed` (SM_CXDRAG /
SM_CYDRAG) tells a drag from a click. `ce_drag_set_shadow` adds a shadow under the stack (`ce_image_shadow`:
the sprite's alpha blurred), drawn and moved with it. Without a sprite only the position is kept (Solitaire's "Outline
dragging" draws the drag as part of its board).

**Animation.** Timing first (`engine/ease.h`, portable): `ce_flight_ms(dist, scale_milli, px_per_frame,
frame_ms)` is a flight's time, XP's straight-flight pace for that distance (`ce_flight_ms_xp`) capped at 60
+ 100 d / 640 ms (d in XP pixels), 160 at most; `ce_ease(curve, t, dur)` its progress (CE_EASE_STANDARD
between piles, DECEL arriving, ACCEL leaving, LINEAR = XP's); `ce_cascade_ms` when a cascade's next card
starts (60 %). `ce_anim_fly` moves one sprite (and an optional shadow) along a curve in that time,
frame-timed (late frames dropped, never the landing frame; each frame shows the position due one frame
later, as XP's AnimateCard), stopping when the abort callback says the layout changed. A game with
cascades uses `CeFlights`: `ce_flights_add` (held until the game has moved the card:
`ce_flights_release`), `ce_flights_run(fl, ce_flights_next(f))` before the next card,
`ce_flights_settle(fl, pile)` before a card leaves a pile cards are still landing on, and
`ce_flights_land_all` at the end; the game hides the cards in the air from its back buffer (it reads
`fl.f[]`) and re-renders a landing card's region in the land callback (`ce_flights_dirty`). A flight can
also be a flip (`CE_FLIGHT_FLIP`: `img` narrows, `img2` widens). Frames present only dirty rects
(`ce_backbuf_present_layers`). Other animations (Solitaire's win cascade) use `ce_anim_begin`,
`ce_anim_frame_wait(t0, i, frame_ms)` and `ce_anim_idle` (call it once the whole replay is over, not per
card: timeBeginPeriod is slow on XP).

**Table button.** `CeTableButton` (`engine/win32/button.h`): `ce_tbutton_place` per layout, the game sets
`shown` and draws it into the back buffer after rendering a region that meets it (`ce_tbutton_draw`: the
visual style's push button through uxtheme.dll loaded at run time, else DrawFrameControl), and routes the
left button, the mouse moves, WM_MOUSELEAVE and WM_CAPTURECHANGED through `ce_tbutton_mouse` (USED,
REDRAW, CLICK).

**Settings.** A game's options and statistics models take a `CeStore`; the exe gives them
`ce_reg_store(&key)` with a static `CeRegStore { key, binary, ini_file, ini_section }`: XP FreeCell's
key writes 4-byte REG_BINARY with the entpack.ini migration, XP Solitaire's
`HKCU\Software\Microsoft\Solitaire` (`Options`, `Back`) REG_DWORD, our own `HKCU\Software\xp-cards\<Game>
HD` REG_DWORD. Both types are read when 4 bytes long. Data files: `ce_app_file_io(&file)` with a static
`CeAppFile { L"<Game> HD", L"<stem>" }`.

**The rest.** `ce_dialog_center` / `ce_dialog_move` (logged, e2e scripts read the log),
`ce_help_open_chm` + `ce_help_shutdown`, `ce_shell_about`, `CeMenuBarText` for text in the menu bar,
`CeWorker` for a background search, `ce_log_open(L"<GAME>_TIMING_LOG")` / `ce_log` / `ce_now_ms`,
`ce_message_loop(&hwnd, accel)`, `ce_time_seed` / `ce_unix_time`.

## Rules for code in the engine (and the games)

* XP SP2/SP3, 32-bit, P6 class without SSE2 (the user's Athlon XP has SSE only): `-march=i686`, no
  SSE2 intrinsics, `STBI_NO_SIMD`. x87 is slow: hot loops are integer; floating point runs once per
  size or per row, not per pixel per card.
* Every import must exist on XP (`make xpcheck`); load anything newer at run time (as hhctrl.ocx).
* No `assert()` in Windows code (under UNICODE it imports `_wassert`, which XP's msvcrt lacks).
* Never pass a string literal to an API that may write into it: XP's ShellAboutW writes a NUL into its
  title (a crash on real XP, not under Wine). `ce_shell_about` copies first.
* Portable code has no Windows headers, so it is tested natively with ASan and UBSan (`make test`).
* Engine symbols are `ce_` / `Ce` / `CE_`; FreeCell's `fc_` / `fcs_` / `Fc` / `FC_`; Solitaire should
  use `sol_` / `Sol` / `SOL_`.

## What stayed in FreeCell, and why

* The controller (session), assist, solver, statistics model, won-deals set: FreeCell's rules and XP
  FreeCell's formats.
* Layout and renderer: XP FreeCell's geometry; they are built on CeRect, CeDraw and CeCardSet.
* The kings (`src/freecell/sprites.c`): FreeCell's only non-card sprites; `FcCardSet` wraps the engine
  card set and adds them, with the old `fc_cardset_*` API.
* The incremental view sync (`view.c`: what the back buffer shows vs the session's board, then dirty
  rects): it depends on FreeCell's board model. A new game copies the pattern, not the code.

## The restructure was checked to change nothing in FreeCell HD (2026-10-01)

Before (HEAD 834971a) and after, on the same machine, the after-build made from a fresh copy of the tree:

* `make test`: the six FreeCell-era suites report the same check counts (test_assist 820, test_game
  42516, test_image 414, test_layout 7597, test_session 480628, test_solver 4148), all passing; the new
  test_engine adds 52.
* `make snapshots`: all 21 PNGs byte-identical.
* `make xpcheck`: PASS for both exes; FreeCellHD.exe imports exactly the same 188 functions.
* `make e2e` (FreeCell, six scenarios): all OK, 68 captures compared pixel by pixel. Two runs of the old
  exe agree on 67; the 68th (`21_maximized`) races the repaint after maximizing. The new exe matched
  all 67 in one run; in another, 66, plus `53_fullscreen_window`, whose only difference is the game
  clock in the menu bar ("Time: 0:06" against 0:05: wall-clock time). The timing logs (1183 lines:
  every click and its hit, render, flight with its frame count, dialog position, solver result with
  its node count) are identical apart from elapsed times.
