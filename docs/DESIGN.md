# FreeCell HD — design

A from-scratch re-implementation of Windows XP FreeCell whose window is freely resizable, with cards
that scale with it (crisp at native 1920x1080), running on Windows XP SP2/SP3 (32-bit), built on macOS
with mingw-w64. It is built on the card-game engine it shares with Solitaire HD (`docs/ENGINE.md`).

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
   * **Unlimited undo** (each undo step = one user action + its autoplay, as XP's single undo) and
     **redo** (Ctrl+Y).
   * **Window size/position/maximized state remembered** between runs.
   * v1.1, each behind an option (Options > Extras) that defaults to XP: time and move counter in the
     menu bar, the standard supermove rule (f+1)·2^e, New Game from all 1..1,000,000 deals; plus
     borderless full screen (Game > Full Screen, F11 / Alt+Enter) and the won-deals record (Select Game,
     Statistics).
   * v1.2, on the solver (see "Hint, warning and Finish"): **Game > Hint** (H) and **Game > Finish**
     (F6, enabled only on a sure win) are new menu items that change nothing until used; the options
     **"Warn when the game can't be won"** and **"Finish automatically"** are off by default.
3. Fix XP bugs listed in rules.md §11 instead of replicating them. Also fix the missing spaces in
   "lose.There" and "cards.You", and drop the stray `%` in the statistics strings (render what XP
   shows on screen).

## Build / runtime constraints

* Language: C99 (GNU dialect ok). Win32 API only (user32, gdi32, kernel32, advapi32, comctl32,
  shell32, winmm; hhctrl.ocx loaded dynamically). No C++ / no GDI+ required.
* Toolchain: Homebrew `i686-w64-mingw32-gcc` (GCC 16, mingw-w64 14, **UCRT by default — we MUST use
  `-mcrtdll=msvcrt-os`** so the exe imports `msvcrt.dll`, which XP has). `-march=i686` (no SSE2
  assumption). `-static` (static libgcc). `_WIN32_WINNT=0x0501`, `WINVER=0x0501`, `UNICODE`.
* CPU: P6 class or later (Pentium Pro/II, Athlon/Duron, VIA C3 Nehemiah): the code uses CMOV and FCOMI,
  and so does mingw-w64's prebuilt i686 runtime (gdtoa, printf), so `-march=i586` alone would not make
  the exe run on a Pentium MMX, K6 or early C3. No SSE (`STBI_NO_SIMD`; Athlon Thunderbird/Duron work).
* x87 is slow, so hot loops in `src/engine` (and the games' renderers) are integer: floating point
  (the anti-aliased card shape, bevel rings, the master edge test) runs once per size or on a few
  pixels per row, not per pixel per card.
* Every imported function must exist on Windows XP SP2 — checked by `tools/xp_imports_check.py`.
* Portable core: everything in `src/engine` and `src/freecell` (outside their `win32/` directories) is
  plain C with no Windows headers, so it is unit-tested natively on macOS (`make test`) and renders
  snapshot PNGs (`make snapshots`).
* Output: `build/FreeCellHD.exe` — a single self-contained exe (art embedded as RCDATA PNGs). `make`
  also builds `build/SolitaireHD.exe` from the same engine.

## Source layout

The engine (`src/engine`, `src/engine/win32`, `res/common`) is described in `docs/ENGINE.md`: images,
the card set, clipped drawing, persistence interfaces, and the Win32 shell pieces (back buffer,
animation clock, menu-bar text, placement and full screen, registry, dialogs, help, worker thread).
FreeCell HD's own code:

```
src/freecell/game.{h,c}      rules primitives: deal, predicates, capacity, move planning, autoplay,
                             no-moves
src/freecell/session.{h,c}   XP controller/state machine (selection, clicks, keyboard, undo history,
                             new/select/restart flow, win/lose, stats bookkeeping) — platform-
                             independent, talks to the UI through a callback table
src/freecell/stats.{h,c}     statistics/options model + XP percentage/streak math (storage: CeStore)
src/freecell/wondeals.{h,c}  the won-deals set and its file format (I/O: engine CeBlobIO)
src/freecell/solver.{h,c}    solver for Hint / "no longer winnable" / auto-finish (ROADMAP 1b): session
                             actions, weighted best-first search, sure-win test
src/freecell/assist.{h,c}    v1.2 state machines on the solver: Hint, the unwinnable warning, the solution
                             cache (Finish itself is a session action in session.c)
src/freecell/sprites.{h,c}   FcCardSet: the engine card set (faces) plus the three kings
src/freecell/layout.{h,c}    scalable geometry + hit testing
src/freecell/render.{h,c}    draws the board into an image through the engine's CeDraw (used by Win32
                             and by native snapshot tests)
src/freecell/win32/*.c       WinMain, window proc, view (incremental rendering, flights, cursors,
                             "Cards Left"), dialogs, storage keys, help text, the solver's worker job
res/freecell/                .rc, resource.h, manifest, icon, cursor, king PNGs (+ sources, icon tools);
                             the card PNGs are res/common/cards (res/common/cards.rc)
tests/freecell/              native unit tests + snapshot renderer + solver benchmark
tests/e2e/fchd_*.txt         Wine end-to-end scenarios
tools/                       XP import checker, Wine end-to-end driver, asset scripts, crispness lab
```

## Extras policy

* Extra **commands** that act only when invoked (menu items, keys XP doesn't use: Hint H, Finish F6,
  Undo All, Ctrl+Z, Ctrl+Y, F11, Solitaire D/C/F4) are always available — no option.
* Anything that changes what normal input does or makes the game act on its own (click semantics,
  drag, auto-moves, auto-turn, auto-finish, warnings, deal selection) is an **Options checkbox, off by
  default**, so the default experience is exactly XP.

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
tableau columns (index 0 = furthest from the player). See `src/freecell/game.h`.

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
* Animation step: `37 s` px per frame; 10 ms per frame (XP had no delay; this mimics period hardware),
  paced against `timeGetTime` with `timeBeginPeriod(1)` during flights (a plain `Sleep(10)` lasts a
  whole 10–15.6 ms clock tick on XP); late frames are dropped, the landing frame never is.

## Rendering

Software compositing into a 32-bit top-down DIB section (premultiplied BGRA), then BitBlt.
Card masters (400x560 RGBA PNG) are decoded once, premultiplied, and resampled with a high-quality
area-averaging filter (dark-biased and lightly sharpened for small cards, see "Crispness decisions")
to `cw x ch` whenever the size changes; a crisp 1-px dark rounded outline is
drawn after scaling (the art's 1.67-px master outline vanishes when downscaled). During live
resizing a cheaper filter may be used, with the HQ rescale after `WM_EXITSIZEMOVE` (a fast request at
the card size already built keeps the HQ sprites; above ~2660x1570 the fast sprites come from
half-size copies of the masters, so a live resize never decodes a PNG). Selection =
XP's colour inversion (`255 - RGB`, corners untouched). Table colour RGB(0,127,0).

### Crispness decisions (2026-10-01, v1.1.1)
Chosen with the crispness lab (`tools/crisplab/`: a numpy model of this pipeline, faithful to the C
within 1 LSB, plus comparison and blind sheets; the finalists, the C spec and the costs are in
`tools/crisplab/FINALISTS.md`). Two blind judges both ranked "art + dark bias + sharpen" first, ahead
of the dark bias alone, the art change alone and v1.1.
* **Art** (`tools/make_assets.sh cards` via `tools/edit_card_svg.py`; the source SVGs stay untouched):
  rank index stroke 80 → 130 (v1.1: 115). Court-card linework (`stroke="#44F"`, widths 3 to 72 in
  the 1300x2000 court art) 1.6x wider and dark navy `#223`, and the court picture frame `#223` at
  1.5 units: the original sub-pixel light-blue lines washed the courts out to pastel even at 1080p.
  Blue *fills* stay.
* **Resampling** (card masters at best quality, `ce_image_resample_card` in `src/engine/image.c`): with
  `f = 560 / ch` and `t = clamp((f - 2) / 2, 0, 1)` (full strength at ch ≤ 140, 0.75 at h160, 0 from
  h280), the exact-area box filter runs on `v^g` with `g = 1 - 0.4 t` (a dark bias, "stem
  darkening": thin dark strokes keep their weight), then a 3-tap sharpen `[-a/4, 1 + a/2, -a/4]` with
  `a = 0.2 t` along x then y, **each sample clamped to the [min, max] of the three it came from** (no
  overshoot: removes the stray near-white/cyan halo pixels the plain sharpen left in the court art),
  then back with `v^(1/g)`. Flat colours are unchanged exactly (white stays 255, table and frame are
  drawn separately), the card shape and frame still come from `ce_image_card_finish`. `t < 0.1`
  counts as 0, so from h255 up (1080p maximised is h257) the v1.1 box code runs, bit for bit.
  Integer inner loops: 16-bit power-space LUTs (`pow()` runs 4352 times per *size change*, in
  `ce_card_filter_new`), 16.16 box weights, 4.12 sharpen weights, a 3-row ring for the y pass. Only
  opaque sources take this path (the cleaned masters are); kings, the half-size mips and the
  live-resize bilinear path (quality 0) are unchanged.
* Rejected: Lanczos-3 + a light unsharp mask (the plan before the lab): 3.8-5.4x the taps of the box,
  still ringing at h257, and dominated on acutance, halo and court noise by the dark-biased, sharpened
  box (`tools/crisplab/rounds/r3_notes.md`).
* Cost: a 52-card HQ rebuild takes about 1.4x the box at ch ≤ 200 (natively 14.9 ms against 10.8 ms at
  h96) and the same from h255 up (`tools/crisplab/FINALISTS.md` §6). On an XP-era CPU (roughly 15-25x
  slower) that is about 0.3 s instead of 0.2 s at h96, and sprites are built lazily per card.

## Solver

`src/freecell/solver.{h,c}` (v1.2; used by Hint, the warning and Finish, below). Platform independent,
integer only, no globals.

* **Moves are session actions.** A solver move is one click pair as `fcs_click` plays it: the user's
  part through the same game.c primitives the session calls (`fc_queue`, `fc_supermove` or
  `fc_move_cards_std` per the supermove option, `fc_move_run_via_free_cells` for XP's "Move column"),
  then `fc_autoplay`. Each `FcSolveMove` carries the two clicks (source and destination `(col,pos)`) and
  whether the "Move to Empty Column..." dialog appears and how to answer it (table in `solver.h`). A
  fresh deal, which XP never autoplays, also offers the bare select-and-click-again move. Left out are
  only actions that cannot change the position up to free-cell and column order (free cell to free
  cell, a lone card or a whole column into an empty column), so an exhausted search proves that no
  sequence of session actions wins.
* **Search:** weighted best-first, priority `6 g + h` (g = actions), a bucket queue (newest first among
  equals). `h` = 4 per card not home + 12 per column card above a lower card of its column (4 if it
  rests on its natural parent) + 2 per card covering the next card a home pile needs + 12 per occupied
  free cell - 8 per empty column (weights tuned on all 32000 deals). States are canonical 60-byte
  strings (free cells sorted, columns ordered by their deepest card, home piles by size), kept in a
  fixed pool of 80-byte nodes with an open-addressing hash table (load <= 1/2), allocated once per
  solver: the default budget of 100000 nodes is 8.7 MiB, the maximum 360000 is 31.5 MiB. Moves are
  stored by card identity, so the path is resolved to clicks by replaying it on the real board. A
  cancel flag is read before every expansion. Results are deterministic and identical on 32-bit
  Windows (the i686 build of `tests/solver_bench.c` under Wine matches the native run deal by deal).
* **Sure win** (`fc_sure_win`, for auto-finish): moving any card that can go home, autoplay safety
  ignored, empties the board; returns the moves (lowest card first, each followed by autoplay).

Deals 1..32000, XP rule, default budget (`make solver-bench`, Apple M3 Pro, -O2, one thread):
31997 solved, deal 11982 proven unsolvable (61643 positions, the whole reachable set), 2 gave up
(8044 and 10692 need 115k and 217k nodes). Time median 0.49 ms, p95 3.1 ms, p99 9.2 ms, max 125 ms
(a gave-up deal); nodes median 1046, p95 5348, p99 13832; solutions median 44 actions (max 70);
about 0.5 us per node. The standard supermove rule gives the same counts. The search is integer code,
so assuming the Athlon XP is 20x slower (conservative), Hint answers in ~10 ms typically, 99.35% of
deals within ~0.2 s, 99.97% within ~1 s, and gives up after ~2 s.

## Hint, warning and Finish (v1.2)

The state machines are platform independent (`src/freecell/assist.c`, Finish in `session.c`) and unit
tested with a fake UI (`tests/test_assist.c`); the Win32 layer adds a worker thread and the drawing.

* **One background search at a time, for the current position.** The session asks the UI to solve a
  copy of the board (`solve_start(id, board, rule)`), and the answer comes back through
  `fcs_solve_done(id, ...)`. Every change of position (move, Undo, Redo, deal, win/loss) replaces or
  drops the request, so an answer with an older id is ignored. Hint and the warning share it: a hint
  asked while the warning's search for that position runs waits for it.
* **Cache.** The last solution (the board before each of its moves) and the last 8 positions proven
  unwinnable are kept, per supermove rule. On the cached solution the hint is its next move, at once,
  as long as the player follows it (also after Undo back onto it, or Restart). A move from an
  unwinnable position is unwinnable without a search (were it winnable, so would the one before be).
* **Hint** (Game > Hint, the H key as WM_CHAR 'h' / 'H'; XP's OnChar only knows '0'-'9'): the selection
  is dropped (no autoplay), the search runs (cursor IDC_APPSTARTING; input still works and cancels it),
  then the move is flashed: its source card(s) inverted twice, then its destination twice, 200 ms per
  step (on, off, on, off for each, 1.6 s in all), timer driven, the board untouched. Inverted means
  what a selection looks like; a column run shows all the cards that move; an empty free cell, home
  cell or column shows an inverted card-shaped area (any card sprite's alpha as the mask). Any click,
  digit key or command ends the flash. Unwinnable: "There are no more winning moves."; the search gave
  up (node budget) or took over 5 s: "No hint is available." (XP's information message box).
* **Warning** (option "Warn when the game can't be won", registry `WarnUnwinnable`): the deal and every
  position after a committed move, Undo or Redo is checked. When a committed move or Redo leads to a
  position proven unwinnable: "This game can no longer be won. Use Undo to go back." — once; it re-arms
  only when a position is found winnable again (through Undo) or on a new game. If the deal itself was
  proven unwinnable (XP's -1, #11982), the first move says "This game cannot be won." instead. A search
  that gives up proves nothing and says nothing.
* **Finish** (Game > Finish, F6; XP's keys are F1-F5, F10, Shift+F1, Ctrl+Shift+F10, the v1.1 extras
  F11, Alt+Enter, Ctrl+Y; the mnemonic is "i" because "F" is Full Screen): enabled when `fc_sure_win`
  holds (re-evaluated whenever the menu state is). It plays `fc_sure_win`'s moves, each followed by
  autoplay, as **one action** (one undo step, one move on the counter), animated card by card like any
  move (none with Quick play), and the commit ends in the normal win. **"Finish automatically"**
  (`AutoFinish`) runs it right after any committed move or Redo that leaves a sure win; then it is not
  counted as a move (like autoplay). It comes before XP's no-moves check (a sure win always has a move),
  so a position with one legal move left does not start the "one move left" window flash just before
  the win; with the cheat's "lose" armed the move still loses.
* **Win32** (`src/freecell/win32/solve.c` on the engine's `CeWorker`): one worker thread (CreateThread
  on first use, THREAD_PRIORITY_BELOW_NORMAL so the UI and card flights come first on a single core)
  owns the solver (fc_solver_new once, 8.7 MiB). A request replaces a job not yet started and sets the
  running job's cancel flag; the job holds its own board copy and only posts its result back
  (WM_APP_SOLVED). The result is handed to the session only when no session call is running and no modal
  dialog is up (a modal loop dispatches posted messages); until then it is held and retried after the
  next input or when the dialog closes. WM_DESTROY cancels and joins the thread.
* Measured (the exe under Wine on the M3): game #1's hint in 1 ms (1062 positions); #11982 proven
  unwinnable in 116 ms (61643 positions; natively 92 ms, 1.5 us per position as the larger table misses
  the cache). At the solver section's 20x the Athlon XP needs about 2 s for that proof, in the
  background; the e2e line of #31364 needed a single search (the moves follow its cached solution).

## Persistence

* Statistics and options: **the same registry key and format as XP**
  (`HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\FreeCell`, 4-byte REG_BINARY values
  `won lost wins losses streak stype messages quick dblclick AlreadyPlayed`), so the user's existing
  XP FreeCell statistics carry over and both programs stay in sync.
* Window placement (extra): `HKCU\Software\xp-cards\FreeCell HD`, value `WindowPlacement`
  (REG_BINARY WINDOWPLACEMENT). First run: centred, sized to ~75% of the work area height with the
  board's aspect.
* The registry keys and the won-deals file are reached through the engine (`engine/win32/regstore.h`:
  a `CeRegStore` per key, REG_BINARY for XP's key and REG_DWORD for ours; `CeAppFile` for the file);
  `src/freecell/win32/storage.c` names them.
* The extras' options (same key, REG_DWORD 0/1, written on Options > OK): `ShowTimeMoves`,
  `StandardSupermove`, `FullRangeDeals`, `FullScreen` (v1.1), `WarnUnwinnable`, `AutoFinish` (v1.2).

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

* `make test` — native unit tests of the engine (`tests/engine/`: images, the card set incl. backs,
  clipped drawing, the store helpers) and of FreeCell (`tests/freecell/`): core + layout (deals vs
  Rosetta Code, capacity, run length, autoplay, no-moves, undo, stats math, session flows incl. messages
  on/off, MoveCol dialog choices, keyboard, cheat; random-playout invariants), and of the image code:
  the card resampler against a float reference and against golden values from the crispness lab's model
  (`tests/card_golden.h`, regenerated by `tools/crisplab/proto/make_golden.py` when the masters or the
  resampler change).
* `make test` also runs `tests/freecell/test_solver.c`: 300 deals (XP rule) and 100 (standard rule)
  solved and replayed through `fcs_click` with scripted MoveCol answers to a win; the solver's move set
  compared with every click pair the session accepts from 160 positions; deal 11982 and constructed dead
  positions proven unsolvable and cross-checked by independent exhaustive searches (one using the
  session itself as the move oracle); mid-game solves; sure win; budget, cancel, determinism.
* `make solver-bench` — the full 1..32000 sweep (BENCH_ARGS, e.g. `"1 1000 -std -n 50000 -v"`), every
  solution replayed through the session.
* `make test` runs `tests/freecell/test_assist.c` too: the v1.2 state machines with a fake UI and a
  synchronous solver stub (requests are answered by the real solver, or with a scripted status, as the
  worker's posted message would): hint request, wait cursor, the eight flash steps and their cells,
  following hints to a win on one search, the cache across Restart, cancel by input / new game, the 5-s
  limit, gave up / cancelled / no solver, answers while busy or stale; the warning once, derived without
  a search, re-armed through Undo, again after Redo, the unwinnable deal, rule changes; Finish grayed
  until the sure win of #31364 (the solver's line), one counted action of single-card flights, Finish
  automatically (not counted), the registry values. `tests/freecell/test_layout.c` checks the hint
  drawing (a card as a selection, both cancel out, runs, empty cells and columns, clipped renders).
* `tests/e2e/fchd_v12.txt` (Wine): the hint on game #1 captured mid-flash (source, then destination,
  polled with the driver's `wait_pixel`), the next hint after following it, the warning, the unwinnable
  deal, Finish by hand and automatically on #31364 via the solver's embedded click line, the options'
  registry values across a restart.
* `make snapshots` — native renders of the board to PNG at several window sizes/states for review.
* `make xpcheck` — verifies every import of the exes exists on Windows XP SP2.
* `tools/wine/` — end-to-end driver run under Wine (launch, click, capture the client area).
* `make e2e` runs `tests/e2e/fchd_*.txt` (`make e2e-freecell`) and Solitaire HD's `tests/e2e/solhd_*.txt`
  (`make e2e-solitaire`: mouse and keyboard play, the stock and Vegas, the dialogs and the registry, the
  window, the win cascade).
