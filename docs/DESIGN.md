# FreeCell HD — design

A from-scratch re-implementation of Windows XP FreeCell whose window is freely resizable, with cards
that scale with it (crisp at native 1920x1080), running on Windows XP SP2/SP3 (32-bit), built on macOS
with mingw-w64.

Reference material (reverse-engineered from the XP binaries in this repo):

* `docs/xp-reference/rules.md` — game rules, state machine, stats, options (authoritative for behaviour)
* `docs/xp-reference/layout.md` — geometry, colours, cursors, animation, king, "Cards Left"
* `docs/xp-reference/resources.md` — menus, accelerators, dialogs, string table (verbatim)
* `docs/card-art.md` — the card art (RevK SVG playing cards, CC0) and its measured geometry

## Goals

1. Faithful to XP FreeCell: same deals for the same game number (MS LCG, -1/-2, 1..1000000), same
   menus/accelerators/dialogs/texts, same rules (supermove capacity (f+1)(e+1), forced run length,
   Move-to-Empty-Column dialog, autoplay rule and triggers), same statistics semantics and registry
   format, same options, king behaviour, cursors, right-click peek, keyboard digits, cheat.
2. Extras (the only intentional behaviour changes):
   * The board **scales with the window** (see Layout). Window is freely resizable/maximizable.
   * **Unlimited undo** (each undo step = one user action + its autoplay, as XP's single undo).
   * **Window size/position/maximized state remembered** between runs.
3. Fix XP bugs listed in rules.md §11 instead of replicating them. Also fix the missing spaces in
   "lose.There" and "cards.You", and drop the stray `%` in the statistics strings (render what XP
   shows on screen).

## Build / runtime constraints

* Language: C99 (GNU dialect ok). Win32 API only (user32, gdi32, kernel32, advapi32, comctl32,
  shell32; hhctrl.ocx loaded dynamically). No C++ / no GDI+ required.
* Toolchain: Homebrew `i686-w64-mingw32-gcc` (GCC 16, mingw-w64 14, **UCRT by default — we MUST use
  `-mcrtdll=msvcrt-os`** so the exe imports `msvcrt.dll`, which XP has). `-march=i686` (no SSE2
  assumption). `-static` (static libgcc). `_WIN32_WINNT=0x0501`, `WINVER=0x0501`, `UNICODE`.
* Every imported function must exist on Windows XP SP2 — checked by `tools/xp_imports_check.py`.
* Portable core: everything in `src/core` and `src/gfx` is plain C with no Windows headers, so it is
  unit-tested natively on macOS (`make test`) and renders snapshot PNGs (`make snapshots`).
* Output: `build/FreeCellHD.exe` — a single self-contained exe (art embedded as RCDATA PNGs).

## Source layout

```
src/core/game.{h,c}      rules primitives: deal, predicates, capacity, move planning, autoplay, no-moves
src/core/session.{h,c}   XP controller/state machine (selection, clicks, keyboard, undo history,
                         new/select/restart flow, win/lose, stats bookkeeping) — platform-independent,
                         talks to the UI through a callback table
src/core/stats.{h,c}     statistics/options model + XP percentage/streak math (storage via callbacks)
src/gfx/image.{h,c}      premultiplied BGRA images, PNG decode (stb_image), HQ resampling, compositing
src/gfx/cardset.{h,c}    card/king sprites at the current size (lazy HQ rescale, outline, inversion)
src/gfx/layout.{h,c}     scalable geometry + hit testing
src/gfx/render.{h,c}     draws the board into an image (used by Win32 and by native snapshot tests)
src/win32/*.c            WinMain, window proc, menus, dialogs, registry, cursors, animation, help
res/                     .rc, resource.h, manifest, icon, cursor, card PNGs, king PNGs
tests/                   native unit tests + snapshot renderer
tools/                   XP import checker, Wine end-to-end driver
```

## Input model

Click to select, click the destination (plus XP's double-click to a free cell, digit keys and
right-button peek) — exactly as XP. FreeCell descends from Paul Alfille's 1978 PLATO game, played on
touch-screen terminals (touch the card, then the target), and Jim Horne's Windows version kept that
model. It also fits the rules: you only say *where* a card goes and the game computes how many cards
a supermove takes (rules.md §2.3), while dragging would require grabbing exactly the right card of
a run. Drag-and-drop is planned as an optional mode once Solitaire builds the drag code (ROADMAP §1b).

## Card encoding and board model

Identical to XP (rules.md §0): `card = rank*4 + suit`, rank 0=A..12=K, suit 0=♣ 1=♦ 2=♥ 3=♠,
-1 = empty. `board[0][0..3]` free cells, `board[0][4..7]` home cells (top card only), `board[1..8][i]`
tableau columns (index 0 = furthest from the player). See `src/core/game.h`.

## Layout (scaling)

All XP geometry (layout.md §2, §10) is expressed in XP pixels at scale `s = 1` (card 71x96) and scaled:

* `s = min(Wc / 632, Hc / 372)` where `Wc x Hc` is the client size. 632 is XP's client width; 372 is
  the height needed for the top row + a 10-card column with the normal step + a small margin
  (106 + 9*18 + 96 + 8). At 1920x1080 maximized this is height-limited: s ≈ 2.68, card ≈ 190x257
  — a balance between card size and long columns (10 cards at XP's step, 13 still at ~0.14 ch).
  Clamp `s >= 0.5`; the window enforces a minimum track size accordingly.
* Board width `Wb = round(632 s)` when height-limited, else `Wb = Wc`; board is centred horizontally:
  `bx = (Wc - Wb) / 2`. All XP formulas then use `Wb` in place of XP's `Wc` and are offset by `bx`.
* `cw = round(71 s)`, `ch = round(96 s)`. Cards art (5:7) is stretched to the 71:96 cell (3.6%,
  invisible) so XP's geometry is kept exactly.
* Free cells `x = bx + i*cw, y = 0`; home cells flush right `x = bx + Wb - 4cw + i*cw`; king
  `K = round(32 s)` at `(bx + (Wb-K)/2, (ch-K)/3)`, its raised frame 3s outside; columns
  `g = floor((Wb - 8cw)/9)`, `x_k = bx + g + floor((k-1)(Wb-g)/8)`; `y0 = ch + round(10 s)`;
  normal step `floor(9 ch / 46)` (= 18 at s = 1, so s = 1 reproduces XP pixel-exactly).
* **Column compression (extra, XP never needed it):** if a column's bottom card would end below
  `Hc - round(4 s)`, that column's step shrinks to fit, but never below `step_min = 0.10 ch` (the top of the rank
  glyph stays visible; the art's rank glyph spans y 20–102 of 560); below that it runs off the
  bottom (as XP).
* Bevels (empty free/home cells: black top/left, #00FF00 bottom/right; king frame the reverse) are
  drawn with line width `max(1, round(s))`.
* Big win king: `320 s` square at `(bx + 10 s, ch + 10 s)`, shrunk to fit the client height if needed.
* Animation step: `37 s` px per frame; ~10 ms per frame (XP had no delay; this mimics period hardware).

## Rendering

Software compositing into a 32-bit top-down DIB section (premultiplied BGRA), then BitBlt.
Card masters (400x560 RGBA PNG) are decoded once, premultiplied, and resampled with a high-quality
area-averaging filter to `cw x ch` whenever the size changes; a crisp 1-px dark rounded outline is
drawn after scaling (the art's 1.67-px master outline vanishes when downscaled). During live
resizing a cheaper filter may be used, with the HQ rescale after `WM_EXITSIZEMOVE`. Selection =
XP's colour inversion (`255 - RGB`, corners untouched). Table colour RGB(0,127,0).

### Crispness decisions (2026-10-01, queued after v1.1)
* Art: rank index stroke 80 → 115 (done). Court-card linework (`stroke="#44F"`, widths 3/6/36 in the
  1300x2000 court art) → 1.6x thicker and dark navy `#223` at every size: the original sub-pixel
  light-blue lines wash the courts out to pastel even at 1080p. Blue *fills* stay. Applied in
  `tools/make_assets.sh`; the source SVGs stay untouched.
* Resampling: Lanczos-3 (premultiplied, fixed point) instead of the box filter, plus a light unsharp
  mask whose strength ramps in only for strong downscales (≈ none at ≤1.5x, full at ≥3x, i.e. small
  windows), so 1080p is essentially unchanged.

## Persistence

* Statistics and options: **the same registry key and format as XP**
  (`HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\FreeCell`, 4-byte REG_BINARY values
  `won lost wins losses streak stype messages quick dblclick AlreadyPlayed`), so the user's existing
  XP FreeCell statistics carry over and both programs stay in sync.
* Window placement (extra): `HKCU\Software\xp-cards\FreeCell HD`, value `WindowPlacement`
  (REG_BINARY WINDOWPLACEMENT). First run: centred, sized to ~75% of the work area height with the
  board's aspect.

## Help

Help ▸ Contents/Search open `freecell.chm` through `HtmlHelpW` loaded from `hhctrl.ocx` at run time
(XP ships `%windir%\Help\freecell.chm`); How to Use Help opens `NTHelp.chm`. If HtmlHelp is not
available or fails, a built-in "How to play" message is shown. About uses `ShellAboutW` like XP.

## Attribution

Credit the card art only in unobtrusive places — never on the table or the cards (the Ace of Spades
deliberately carries no link/text): the GitHub README, Help ▸ About, and the exe's version info.
Wording: "Card faces: SVG playing cards by Adrian Kennard — https://cards.revk.uk (CC0)".
Use the `cards.revk.uk` link (it redirects to https://www.me.uk/cards/).

## Testing

* `make test` — native unit tests of core + layout (deals vs Rosetta Code, capacity, run length,
  autoplay, no-moves, undo, stats math, session flows incl. messages on/off, MoveCol dialog choices,
  keyboard, cheat; random-playout invariants).
* `make snapshots` — native renders of the board to PNG at several window sizes/states for review.
* `make xpcheck` — verifies every import of the exe exists on Windows XP SP2.
* `tools/wine/` — end-to-end driver run under Wine (launch, click, capture the client area).
