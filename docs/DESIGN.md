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
   * v1.4 (see "FreeCell HD extras (v1.4)"): **Game > Undo All** and **Ctrl+Z** (Undo, repeating while
     held) are always there; the options **"Single click moves a card"** and **"Drag and drop cards"**
     are off by default.
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
  Undo All, Ctrl+Z, Ctrl+Y, F11, Solitaire D/C/F4) are always available — no option. The same goes for
  passive affordances that only appear when useful and can simply be ignored (the on-table Finish
  button shown once the rest of the game is a sure win) and for a change of timing that leaves what
  happens alone (2d: eased, overlapping card flights, never slower than XP's).
* Anything that changes what normal input does or makes the game act on its own (click semantics,
  drag, auto-moves, auto-turn, auto-finish, warnings, deal selection) is an **Options checkbox, off by
  default**, so the default experience is exactly XP.

## Input model

Click to select, click the destination (plus XP's double-click to a free cell, digit keys and
right-button peek) — exactly as XP. FreeCell descends from Paul Alfille's 1978 PLATO game, played on
touch-screen terminals (touch the card, then the target), and Jim Horne's Windows version kept that
model. It also fits the rules: you only say *where* a card goes and the game computes how many cards
a supermove takes (rules.md §2.3), while dragging would require grabbing exactly the right card of
a run. Since v1.4 two opt-in modes exist on top of it ("FreeCell HD extras (v1.4)"): a single click
that moves the card to its best place, and drag and drop on Solitaire HD's (now the engine's) drag
code, whose drop is still XP's move to that pile (XP counts the cards).

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
  whole 10–15.6 ms clock tick on XP); late frames are dropped, the landing frame never is. Since 2d
  this is the pace a flight may not fall behind: flights are eased and overlap ("Motion (2d)").

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

## FreeCell HD extras (v1.4)

The UX batch of ROADMAP §2b, under the extras policy. The session (`src/freecell/session.c`) does the
rules and is unit tested (`tests/freecell/test_session.c`); the Win32 layer only maps the mouse.

* **Undo All** (Game > Undo All, `FCS_CMD_UNDO_ALL`, enabled with Undo): asks first, as XP asks to
  resign (the beep, "Do you want to undo all your moves and return to the start of the game?", Yes /
  No; `ui.confirm`). Yes undoes every action back to the deal at once, without flights (a whole game of
  flights would take many seconds); the move counter is as after that many Undos. The actions undone
  form one group on top of the redo stack: the next **Redo** brings them all back, again without
  flights, and then settles as after a commit (Finish automatically, XP's no-moves check, the warning).
  A new action, a deal, a win or a loss drops the group with the redo stack.
* **Ctrl+Z** is a second accelerator for Undo (F10 stays). An accelerator repeats while it is held
  (each auto-repeated key-down is translated), so holding Ctrl+Z undoes move after move, each flown back
  unless Quick play is on.
* **Single click moves a card** (`SingleClick`): a click on a card while nothing is selected selects
  it as XP's click and then makes XP's move to its best place (`fcs_single_dest`), first that applies:
  1. its home cell, when it may go there and is safe there (XP's autoplay rule, rules.md §3);
  2. the leftmost non-empty column it fits on; from a column that is XP's move to that column (the run
     part that fits, within the supermove limit of the current rule);
  3. the leftmost empty column, unless the whole column is one ordered run (that would gain nothing);
     a run gets XP's "Move to Empty Column..." dialog;
  4. its home cell, when it may go there but is not safe;
  5. the leftmost empty free cell (from a column).
  Nowhere: the card stays selected, exactly as XP's click, so the next click places it. A click while
  something is selected is XP's destination click. The double-click that follows a click which moved a
  card is ignored (it would move the next card); keys and commands are unchanged.
* **Drag and drop cards** (`DragDrop`): the press selects as XP's click (`fcs_press`); once the pointer
  passes the system's drag threshold the cards from the pressed one to the bottom of its column (only
  when they form an ordered run; a free cell's card alone, `fcs_drag_cards`) are lifted: hidden from the
  board and floated over the back buffer as one sprite (`fc_render_stack`, the engine's `CeDrag`,
  Solitaire's drag). The cursor shows XP's destination arrows meanwhile. The drop is XP's move of the
  selection to the pile under the pointer (`FC_HIT_DEST`'s wide zones) as a click there would make it:
  the supermove limit, the messages, the Move-to-Empty-Column dialog, autoplay; XP decides how many cards
  go, as for a click. A drop XP would refuse (`fcs_drop_ok`: an illegal pile, too many cards, the own
  pile, nowhere) slides back first, then gets XP's message (with messages off the card stays selected,
  as XP). The dropped cards are not flown (they are already there); the autoplay that follows is. A
  press released without moving keeps XP's click; with Single click on too it moves the card then
  (`fcs_release`). A press on the selected pile can start a drag of it; released unmoved it is XP's
  click on the selection (deselect). Esc, a lost capture or focus, a command or a resize puts the cards
  back; solver answers wait until the drag is over.
* **With both off, nothing changes.** `make fc-xp-compare` builds FreeCell HD 1.3's session and rules
  (git 904243b: game, session, assist, solver, stats, wondeals) and today's, feeds both the same random
  input (clicks, legal click pairs, double-clicks, keys, the peek, mouse moves, the activation click,
  every timer, New / Select / Restart with the dialogs answered at random, Undo, Redo, the cheat, Hint,
  Finish, Exit, Options changes of XP's three and the v1.1 / v1.2 extras, the background solver's
  answers) and compares the logs of every UI callback, registry access and the observable state byte
  for byte: 300 games, 110793 inputs, 1263074 identical lines (168 wins, 525 losses, 4443 solver
  answers). Today's build also sets the new `confirm` callback: one call of it would be a difference.

## Solitaire HD extras (v1.1)

Solitaire HD v1.0 is XP's sol.exe (`docs/xp-reference/solitaire/`); v1.1 adds extras under the extras
policy above. **Commands** that act only when invoked are always there: Game > Hint (H), Game > Finish
(F6), Game > Statistics... (F4). **Everything that changes what input does or makes the game act on its
own** is a checkbox in the Options dialog's new "Extras" group (to the right of XP's controls, which keep
their positions; `HKCU\Software\xp-cards\Solitaire HD`, REG_DWORD 0/1, all off by default): `AutoTurn`,
`ClickToMove`, `AutoFinish`, `WinnableOnly`, `SaveGame`, `WarnUnwinnable`. The state machines are in the
platform-independent session (`src/solitaire/session.c`, the `SolExtras` it is given) and pure helpers:

```
src/solitaire/assist.{h,c}          fair Hint ranking, click-to-move destination, Finish (pure functions)
src/solitaire/stats.{h,c}           statistics per mode + file format
src/solitaire/savegame.{h,c}        the saved game's file format, exact restore
src/solitaire/solver.{h,c}          full-information solver (winnable table, the warning)
src/solitaire/winnable_seeds.{h,c}  the winnable-deal table (generated: make seed-tables)
src/solitaire/win32/solve.c         the warning's worker thread (engine CeWorker)
```

* **With every extra off nothing changes.** `make sol-xp-compare` builds the v1.0 session (git 5dc1896)
  and today's, feeds both the same random input (mouse and keyboard play, the stock and its cheat, Deal,
  Undo, Redo, the forced win, the dialogs' results, clock ticks, "Deal Again?" both ways) and compares
  logs of every UI callback, registry access and the whole observable state byte for byte: 400 games,
  264693 inputs, 1100048 identical lines (today's build with the statistics attached, as the front end
  runs; any call of a new callback would be a difference).
* **Hint** (Game > Hint, H as WM_CHAR, like FreeCell's): **fair**: `sol_hint_find` reads only what the
  player sees (face-up cards, the number of face-down cards, the foundations, the waste's top card,
  whether the stock is empty), never a face-down card or the stock order; the test shuffles the hidden
  cards of 42000 positions and requires the same answer. The full-information solver is never used. A
  ranking of 12 classes (13 since 2c) picks the move (assist.h has the list): turning a face-down card over, aces and
  twos home, safe foundation moves, moves that uncover a face-down card (more face-down cards under
  them first), the waste's card home, a run moved to free a card for home (never onto another card bound
  for home: no back and forth), the waste's card to the tableau, emptying a column for a visible king,
  other foundation moves, then draw, then recycle (when the pass limit allows). Never suggested: a
  foundation card back down, a sideways move that frees nothing, a king heading its column moved to
  another empty column. Nothing left: "No hint is available.". The flash is FreeCell's: the source cards
  inverted twice, then the destination (an empty pile's slot inverted) twice, 200 ms per step,
  timer-driven (`SOL_TIMER_HINT`), drawn through the view's keyboard-selection overlay; any input ends
  it. Followed blindly with a pass limit (Vegas) the hint always ends, in a win or in "no hint" (400
  test games; with unlimited passes it keeps suggesting draws and recycles, as a fair hint must).
* **Finish** (Game > Finish, F6, enabled only when it applies; mnemonic "i", "F" is Full Screen): with
  the stock and the waste empty and every tableau card face up the game is a sure win (each column is a
  descending run, so the lowest card left is always on top). Finish plays the lowest card home again and
  again as **one action** (one undo step, `SOL_ACT_FINISH`; Redo replays it), each card flown home by the
  view (`view_animate_move`: at most the time of a straight flight of about 60 XP px per 10-ms frame,
  eased and overlapping since 2d), then the normal win (bonus, cascade, "Deal Again?"). **Finish automatically** runs it after any committed action
  or Redo that leaves it possible (not after Undo).
* **Turn cards over automatically**: after an action every face-down card left on top of a column is
  turned over as part of that action (bits in `SolAction.autoturn`, so Undo restores it face down in one
  step and Redo turns it again), scored as XP's click (Standard +5).
* **Single click moves a card**: a press that picks cards up and is released within the system's drag
  threshold (SM_CXDRAG / SM_CYDRAG) is a click: `sol_click_target` (assist.h `sol_click_dest`) names the
  destination and the drop goes there as an ordinary drop (scored, one undo step); none: nothing moves.
  Ranking: a single card to the leftmost foundation that takes it (as XP's double-click); else the
  leftmost column that takes the cards whose top card could not go home itself; else the leftmost column
  that takes them; a king (or its run) to the leftmost empty column unless it already heads its column.
  Drag and double-click are unchanged; the double-click that follows a click which already moved the
  card is ignored (the click did what the double-click would). The keyboard interface is unchanged.
* **Statistics** (Game > Statistics..., F4; "Solitaire Statistics": a mode list, the current game's mode
  first, eight lines, OK, Reset with a confirmation): per mode (Draw One / Three x Standard / Vegas /
  None; Vegas with or without Cumulative is one mode, Timed is not part of it): games played and won, win
  % (rounded, 100% only when every game was won), the current streak (wins or losses), the longest
  winning and losing streaks, the best time (timed games), the best score (Standard with the time bonus;
  Vegas the game's own result without the Cumulative carry-over; none with None). A game counts as
  played at its first committed action, as won at the win (the Alt+Shift+2 cheat too, as XP FreeCell's
  cheat counts), as lost when a new deal or an Options change replaces it or at Exit, unless "Save game
  on exit" keeps it. Always kept (a passive record); `%APPDATA%\xp-cards\Solitaire HD\statistics.bin`
  ("SOLS", version, 6 x 8 integers, CRC-32; version 2 since 2c adds the high scores and the money, see
  "Windows 7-inspired extras (2c)"), written atomically after every change; a damaged file is set
  aside as `statistics.bad` and the statistics start empty.
* **Save game on exit, resume at start**: at WM_CLOSE / WM_ENDSESSION the game in progress is written to
  `game.bin` (same folder, atomically): the board, waste fan, score, clock, recycles, seed and rand state
  (the cascade continues from it), the mode, whether it already counts in the statistics, and the whole
  undo and redo history ("SOLG", version, length, payload, CRC-32; at most 4 MiB, the oldest history
  dropped first). At start-up it replaces the first deal and is restored exactly; the clock waits for the
  first press, as after a deal; the file is emptied once read, so a crash never resumes it twice. A file
  that fails any check (magic, version, lengths, CRC, a board that is not a Klondike position, values out
  of range) is set aside as `game.bad`; one saved under other Draw / Scoring / Timed options (sol.exe
  shares them) is ignored. With the option off, Exit counts the game as lost and empties an old file.
* **Deal only winnable games**: deals stay XP's (seed = time(NULL) & 0x7FFF, XP's shuffle); the session
  takes the first seed from there on (wrapping) that the precomputed table proves winnable for the
  current draw and pass limit (Standard / None: unlimited passes; Vegas: draw one 1 pass, draw three 3).
  The table (`winnable_seeds.c`, 2 bits per seed, 32 KiB) was made by `make seed-tables`: every seed
  solved by the full-information solver for the four cases, every solution replayed through the session
  to a win; a deal the search could not settle within its budget counts as not winnable. Regenerated
  with the fixed solver (its proof check, below) on 2026-10-02: 4620 s on 11 threads, 0 replay failures.
  Winnable: draw one 29099 (88.8%), draw three 25331 (77.3%), draw one Vegas 5782 (17.6%), draw three
  Vegas 16510 (50.4%); proven unwinnable 1244 / 3287 / 16410 / 8783, the rest undecided within the
  budgets (the old table's extra "unsolvable" entries were unchecked proofs: 6 to 46 more deals per
  case were winnable after all and are offered now).
* **Warn when the game can't be won**: the deal and every position after an action, Undo or Redo are
  solved in the background by the full-information solver (`solver.h`: weighted best-first over session
  actions with macro talon moves, safe foundation moves, 32-byte canonical states, a fixed node pool:
  150k nodes, about 10 MiB, integer only, cancellable; a search narrowed by its move filters that finds
  no win is checked by one over every legal move, as the filters can miss a line where one follow-up
  needs two preparing moves). When an action or Redo leads to a position proven unwinnable: "This game can no longer be won. Use Undo to go back." once ("This game cannot be won." if
  the deal itself was), re-armed by a winnable position or a new deal; a search that gives up says
  nothing. Win32 as FreeCell's: one below-normal worker thread (`src/solitaire/win32/solve.c`), a newer
  request cancels the running one, answers delivered only when no dialog is up and the session is idle
  (not dragging cards either: the message waits for the drop).

## Solitaire HD extras (v1.2)

The UX batch of ROADMAP §2b on Solitaire HD, under the same policy; the session (`session.c`) does it
all and `make sol-xp-compare` still proves the extras-off session identical to v1.0's.

* **Always there:** Game > **Undo All** (asks first: "Do you want to undo all your moves and return to
  the start of the game?", Yes / No; then as that many Undos, so the score is the same; the actions
  undone form one group that the next **Redo** brings back whole), **Ctrl+Z** (Undo; XP's Undo had no
  key; it repeats while held, an accelerator), **D** (draw: a click on the deck, also the recycle;
  WM_CHAR like H), **C** (Game > Deck..., the Select Card Back dialog), F4 (Statistics, since v1.1). The
  Undo item shows Ctrl+Z, the Deck item C.
* **Move cards home automatically** (`AutoHome`): XP FreeCell's autoplay rule on Klondike's
  foundations (`sol_auto_home_step`): after every committed action (a drop, a click move, a
  double-click, a draw, a recycle, a turn, autoplay, Redo replays; not after Undo) every face-up card on
  top of the waste or a column that a foundation takes goes there if it is an ace or a two, or once both
  foundations of the other colour hold its rank - 1 (then no card it could hold is still in play). One
  card at a time (the waste first, then the columns left to right, each to the leftmost foundation that
  takes it), flown like Finish's cards, with "Turn cards over automatically" turning what each uncovers;
  it cascades until no card is safe. It is part of the action (one undo step; `SolAction.autos` records
  the sequence of cards and turns, which Redo replays exactly), scored as XP scores foundation moves
  (Standard +10, Vegas +5), and a draw or a turn that ends in a full set of foundations wins (XP never
  wins on a stock click; this can). A safe card taken down from a foundation goes straight back.
* **Click to select, click to move** (`ClickSelect`): a click on a card (a press released within the
  drag threshold, `sol_click`) selects it and the cards on it, drawn inverted as XP FreeCell's
  selection (the view's selection overlay, as the hint's flash). The next press on a pile that takes them
  (on any of its cards, or on an empty pile's place: `sol_layout_hit_empty`) moves them there, as an
  ordinary drop (one action, scored); a press on the selection's own pile only
  deselects (its click selects nothing); a press anywhere else deselects and acts as usual (the deck
  draws, a face-down card turns over, a card is picked up). Dragging works alongside (a press that moves
  is a drag), the double-click still sends a card home, the keyboard plays as in XP (any key ends a
  selection, as do commands, autoplay and the Options).
* **Both click options on:** "Single click moves a card" goes first: a click moves the card when it has
  a place to go (`sol_click_dest`) and selects it only when it has none, so the next click can put it
  where the player wants.
* **The saved game** is version 2 (`savegame.h`): each action carries its auto-home sequence and the file
  the Undo All group; version 1 files (Solitaire HD 1.1) still load.
* Tests: `tests/solitaire/test_sol_extras.c` (the safe rule, a cascade with turns in between, flights,
  scores, one Undo and an exact Redo, the win on a draw, the bounce from a foundation; selection, its
  destination press, deselecting, the stock, keys and commands ending it, the foundation's card, the
  double-click, the precedence with single click; Undo All asked, No, Yes, the score as that many
  Undos, the group Redo, a new action dropping it, never while dragging; D; the version 2 file and a
  version 1 file), `test_sol_playout.c` (the extras-on games also play auto-home, click selections, Undo
  All and its Redo and D: no safe card is left after an action, Undo All lands on the deal and its Redo on
  the position before it), `tests/e2e/solhd_ux.txt` (Wine).

## Windows 7-inspired extras (2c)

ROADMAP §2c (source: docs/win7-feature-gap.md, Windows 7's wording), under the extras policy: hint
cycling, the dead-end tracking behind Hint and the high scores are always there (they act only when
asked or are a passive record); the No More Moves question, the Windows 7 prompts and the deferred
Options are opt-in. Windows 7's questions use its words but XP's look: plain DIALOG templates in MS Shell
Dlg 8 with a message box's question icon (and its beep), push buttons stacked under the text (the first
the default, Esc the safe answer), centred over the window as a message box; no command links. The
"Large Print" deck of §2c belongs to the card-art branch.

* **Hint cycling (both games, always on).** Hint again while the hint shown last is *current* (the same
  position, and no other input since: only the hint's own flash, mouse moves, the H key's own key-down)
  shows the next move of a ranked list; after the last one it wraps to the first. Any other input (a
  press, a key the game uses, a command) or a change of position starts over at the best move.
  * Solitaire (`assist.h` `sol_hint_list`): every candidate of the fair ranking (below), one per source
    card with the ranking's destination (the leftmost), ordered by class, then more face-down cards under
    the source, then the order found (turns, cards home from the waste then the columns, column runs by
    column and card, the waste's card to the tableau, class 9, the stock); a move found for two reasons
    is listed once, at its better place. `sol_hint_find` is its first, so the first press is unchanged.
    Fair as before (the test shuffles the hidden cards of 11630 positions: the same lists).
  * FreeCell (`assist.c`): first the solver's move (a winning line, as before), then every other action
    the session accepts (`fc_solve_moves` without the bare deselect), best first by the solver's estimate
    of the position it leads to (`fc_solve_estimate`: the search's heuristic with its default weights:
    cards not home, cards above a lower card of their column, the cards covering the next card each home
    pile needs, occupied free cells, less for empty columns; ties in `fc_solve_moves`' order), leaving out
    moves into a position known to be unwinnable (the cache). The alternatives are ranked at the first
    repeat, without a search. An unwinnable position or a search without an answer keeps its message.
* **Solitaire: a new hint class** (the ranking now has 13): 9, a foundation's top card taken down onto the
  tableau so that the waste's card, or a run lying on face-down cards, can go on it (never an ace or a
  two: they would go straight back). Moving a run sideways for that never helps (a card of the same rank
  and colour could go straight where the run would go), so that is still never suggested. Followed
  blindly in Vegas the hint now wins 24 of 400 games (19 before).
* **Dead ends (Solitaire, always tracked; `session.h` "Dead ends").** A *useful* move is any of the hint's
  classes 1 to 11 (`sol_useful_move`): everything but a draw or a recycle. After every committed action
  the session notes the first position without a useful move (its stock count); the draws and recycles
  that follow (nothing else: any other action, Undo, Redo or a deal starts over) complete a *whole stock
  cycle* when, after a recycle, the stock is back at that count or at 0 (every position of a pass seen);
  if no position on the way had a useful move, or the stock is used up (empty, and the pass limit allows
  no recycle) with none, there are no more useful moves. Fair: only the visible cards and the stock's
  size are looked at, and only what the player has actually drawn through counts. Then:
  * **Hint** says "There are no more useful moves." instead of suggesting another draw or recycle (it
    used to suggest them forever with unlimited passes); a used-up stock with nothing at all is still
    XP's "No hint is available.".
  * **"Tell me when there are no more moves"** (`NoMoreMoves`, off by default): at that moment, once per
    dead end (until the tracking starts over), "No More Moves": *There are no more moves. What do you
    want to do?* **End Game (counts as a loss)**: the loss is recorded, the game ends (the table stays,
    frozen, as after XP's "Deal Again?" No; nothing to undo), then XP's "Deal Again?"; **Return to Game
    (use Undo)** (Esc): the game goes on.
* **Statistics: high scores and money (Solitaire, always).** Per mode the five highest final scores of
  its games (won or lost, as the best score; an equal score below the older one), each with the local
  date the game ended; Standard keeps two tables, "Timed games" and "Not timed" (Windows 7's Standard
  Timed / Non-Timed), by the game's own Timed setting; Vegas one, the game's own result, plus **Most money
  won** (the best result, $0 if none was positive), **Most money lost** (the worst, as a positive amount)
  and **Current winnings** (the sum of every result since Reset). The Statistics dialog shows them in a
  "High Scores" box to the right of the old one (dates in the user's short date format,
  `GetDateFormatW`); Reset clears them. Stored in `statistics.bin` version 2 (`stats.h`: 136 bytes per
  mode); a version 1 file (Solitaire HD 1.1 / 1.2) is read with the new values empty and written back as
  version 2 at the next change. The date comes from `ui.today` (with `SOLHD_TIME`, that time's date).
* **The Windows 7 prompts with "Save game on exit"** (`SaveGame`; only for a game in progress: an action
  was made, `sol_game_started`; without the option nothing is asked, as XP):
  * **Deal** (F2) asks "Game in Progress": **Quit and Start a New Game (counts as a loss)** (XP's deal),
    **Restart This Game** (`sol_restart`: the same deal from its start, score and clock as at the deal, a
    Vegas game's money given back before the bet is placed again; it still counts as played and is not a
    loss) or **Keep Playing** (Esc). "Deal Again?" after a win or End Game never asks.
  * **Exit** asks "Exit Game": **Exit and Save My Game** (as before), **Exit and Don't Save (counts as a
    loss)** (the game is lost, the old file emptied) or **Don't Exit** (Esc). A deal nobody played is saved
    without a question; a Windows shutdown (WM_ENDSESSION) saves without one.
  * **At start-up** a resumed game in progress asks "Saved Game Found": **Continue Saved Game** (Esc) or
    **Play New Game (counts as a loss)** (a new deal; the saved game's loss is recorded). A saved deal
    nobody played is resumed without a question.
  FreeCell HD keeps no saved game, so its XP resign question stays as it is.
* **"Apply option changes to the next game"** (`NextGameOptions`, off by default): a change of Draw, Timed
  game or Scoring while a game is in progress asks "Changed Game Settings": *The new settings apply to
  your next game.* **Play New Game (counts as a loss)** (XP's immediate redeal) or **Finish This Game**
  (Esc): the game goes on with its own Draw, Scoring and Timed game; the new Options wait
  (`SolSession.pend_opts`) and the next deal applies them (as an Options redeal: a Vegas Cumulative score
  starts over; "Deal only winnable games" picks its seed for them). Status bar, Outline dragging and
  Cumulative apply at once. The Options value is written at once (XP: at OK), and the Options dialog
  shows the waiting settings; the same change again asks nothing (also once the option is off again: an
  OK that leaves the waiting settings alone never redeals); choosing the game's own settings
  again cancels the wait. A saved game whose Options differ from the current ones (normally ignored) is
  resumed with its own when this option is on, the current ones waiting for the next deal.
* **Vanilla unchanged.** `make sol-xp-compare` (every extra off, v1.0's session against today's, with the
  statistics attached and the new `choose` / `today` callbacks unset): identical, 1100048 lines. `make
  fc-xp-compare`: identical, after one change to its input: a Hint (command or H) in the position where
  the last Hint was given is left out (392 of 2812), as it now shows the next move where 1.3 repeated
  the first; every first Hint in a position is still compared.
* Tests: `tests/solitaire/test_sol_win7.c` (the list and the session's cycle, what keeps and what ends it,
  the list in random play: first = `sol_hint_find`, ordered, no duplicates, legal, fair; class 9; dead
  ends: a whole cycle in Draw One and Draw Three, a useful card in the stock never a dead end, the used-up
  Vegas stock, Hint's message, Undo starting over; the question: asked once, re-armed by Undo, End Game's
  loss and "Deal Again?" both ways, off, no UI; high scores: order, ties, five kept, the two tables, dates,
  the money, the dialog's text in every mode and iCurrency, through the session; the file: version 2 round
  trip, damage, impossible contents with a valid CRC, the version 1 migration, Reset; the prompts: off, a
  fresh deal, Keep Playing, Restart (Standard, Vegas with and without Cumulative), Quit, no UI; Exit's
  three answers and the default; the saved game: Continue, Play New Game's loss, an unplayed deal, off;
  the deferred Options: Finish This Game, the dialog's settings, the registry, Restart keeping the game's
  settings, the next deal, Play New Game, cancelling the wait, off, winnable deals, a saved game under
  other Options), `test_sol_playout.c` (the extras-on games also run the No More Moves question, the New
  Game prompt and deferred Options changes with random answers: a dead end never has a useful move),
  `tests/freecell/test_assist.c` (FreeCell's cycle: every other action once, by estimate, the wrap, what
  starts over, an unwinnable alternative left out), `tests/e2e/solhd_win7.txt` (Wine: the Options' two
  boxes, the cycle on deal 64 captured flash by flash, the High Scores box, deal 147's dead end with
  Return to Game, Hint's message and End Game, the statistics' loss and its date, every Windows 7
  question with each of its answers, the deferred Draw One).

## Motion (2d)

ROADMAP §2d. Every card motion the games already had keeps what it does and only changes its timing
curve (always on, as the extras policy allows: nothing new happens); the new effects are one Options
checkbox, **"Enhanced animations"** (`EnhancedAnimations`, off by default); the Finish button is an
ignorable affordance, always there.

* **Easing** (`src/engine/ease.{h,c}`, portable, unit tested): CSS / Material cubic-bezier timing
  functions, each turned on first use into a 257-entry table of progress at equal time steps, computed in
  64-bit fixed point (bisection on the curve parameter, no floating point; the same numbers natively and
  on the Athlon XP), then one lookup and a linear interpolation per frame. *Standard* (0.2, 0, 0, 1) for
  a card moving between piles, *decelerate* (0, 0, 0.2, 1) for a card arriving home (FreeCell's home
  cells, Solitaire's foundations) or dealt, *accelerate* (0.3, 0, 1, 1) for a card leaving the table (no
  motion does that yet). The tables are within 0.0003 of a double-precision cubic-bezier.
* **Durations, never slower than XP's pace.** A flight's time is the straight flight XP's code made for
  the same distance (`ce_flight_ms_xp`: one 10-ms frame per 37 s px in FreeCell, 60 s px for Solitaire's
  Finish and automatic moves, 36 s px for Solitaire's zip-back), capped by the envelope 60 ms + 100 ms x
  d / 640 XP px (d = the distance at s = 1), 160 ms at most (`ce_flight_ms`). XP's pace is already quick
  for short and middle distances, so those keep their exact time and only the curve changes; the longest
  FreeCell flights are capped. Every frame shows the position due one frame later, as XP's AnimateCard
  did (its frame i of N, drawn after i - 1 frames, is i / N of the way), so a flight of N frames' time
  arrives exactly when XP's did. Measured at 1904 x 996 (`tests/freecell/test_motion.c`,
  `tests/solitaire/test_sol_motion.c`, before -> after): FreeCell column -> adjacent column 20 -> 20 ms,
  column -> free cell 50 -> 50, column 1 -> home cell 7 160 -> 152, column 8 -> free cell 0 150 -> 150;
  Solitaire column -> foundation 20 to 50 -> the same; zip-back 30 / 120 -> the same.
* **Cascades overlap.** The cards of one action (a move's steps, autoplay, Undo, Redo, Finish,
  Solitaire's automatic moves) fly through the engine's flight scheduler (`CeFlights`,
  `engine/win32/anim.h`): the session asks for one card at a time as before, and the view returns when
  that card has flown 60 % of its time (`CE_CASCADE_PERCENT`), so the next one leaves while it is still
  on its way. Cards bound for the same pile land in order (a later card starts late enough,
  `ce_flight_delay`), a card leaving a pile waits for the cards still landing there
  (`ce_flights_settle`: a supermove's free cells), and a flight is *held* (never lands) until the
  session has moved its card (the next animation call releases it). The back buffer shows the session's
  board without the cards in the air: one not moved yet is hidden at its source, the others at their
  destination (FreeCell: `FcView.hide_n` / `hide_top`, a column keeping its step; Solitaire: the
  display copy `disp`); a card that lands is rendered into the back buffer and presented with the next
  frame. Each frame presents only the dirty rects (each card's old and new rect, merged when cheaper, plus
  what was re-rendered) with every card composed over the back buffer
  (`ce_backbuf_present_layers`). All of a session call's cards land before anything else happens
  (`view_anim_idle` after the input and before any dialog; Solitaire's win cascade first lands them).
  A cascade is over sooner than XP's one-card-at-a-time flights: FreeCell's autoplay of the four aces
  310 -> 206 ms, a Finish of 52 cards 5280 -> 3204 ms; Solitaire's Finish of 52 cards 2840 -> 1724 ms.
  Quick play (FreeCell) flies nothing, as before. Frames stay 10 ms, frame-timed (late frames dropped).
* **The zip-back of a refused drop** (Solitaire's XP AnimateBack, FreeCell's drag and drop) is the same
  eased flight (standard easing, at most the time of XP's 36 s px steps); Solitaire's outline drag zips
  back its outline along the same curve.
* **The Finish button** (both games, always on): while Game > Finish is enabled (FreeCell: the rest is a
  sure win; Solitaire: the stock and the waste are used up and every card is face up) and no card flies,
  an XP push button "Finish" is drawn on the table (`engine/win32/button.h` `CeTableButton`, drawn into the
  back buffer after the board's region, so flying and dragged cards pass over it): the visual style's
  button (uxtheme.dll loaded at run time: normal, hot, pressed) when a theme is active, else the classic
  3-D button (DrawFrameControl), the text "Finish" (string 405 / 1128) in Tahoma scaled with the board,
  56 x 20 XP px with an 11-px font at s = 1. FreeCell: centred in the 64-px gap between the free cells and
  the home cells, below the king's frame; Solitaire: centred in the empty slot between the waste (with its
  fan) and the first foundation, level with the cards' middle. Neither place ever holds a card; on a board
  too small for it (s below about 0.7) there is none. A press on it (not the activation click XP swallows, not
  while cards fly or are dragged) captures the mouse and shows it pressed while the pointer is over it; the
  release over it is Game > Finish (F6). It never takes the keyboard focus. Solitaire: the second press of a
  double-click on it (it arrives while the cards fly home or the cascade runs) is the button's, never input
  that stops XP's win cascade (`cascade_pump`).
* **Enhanced animations** (off by default; each effect 150 ms or less per card):
  * the dragged cards cast a soft shadow (`ce_image_shadow`: the stack's alpha blurred by two integer box
    passes of 3 XP px, 35 % black, offset 3 right and 4 down), carried by the zip-back too;
  * Solitaire: a card turning over (a click, the keyboard, "Turn cards over automatically", Redo) flips
    in place in 120 ms: the back narrows to its vertical axis (accelerating), the face widens from it
    (decelerating), each frame a nearest-column squeeze of the sprite; a card that goes home right after
    turning ("Move cards home automatically") turns first, then flies (`view_animate_move` lets the turn land
    before its card leaves; a turn is never shown on a pile its card has left);
  * a new deal flies in: Solitaire's 28 cards out of the stock in XP's order (row by row, the face-down
    ones as backs), FreeCell's 52 from below the middle of the board, row by row, 10 / 6 ms apart, each
    decelerating into its place (about 0.4 s in all);
  * Solitaire's double-click and the right button's autoplay, which XP does at once, fly their cards
    home like the automatic moves (the session calls `ui.animate_move` only with the option on;
    `enhanced_flight`);
  * the hint's flash is a soft pulse: its "on" steps fade the inversion in and its "off" steps fade it
    out over 70 ms (eased, `ce_invert_masked_level`, FcView `hint_level` / SolView `sel_level`), timer
    driven at 15 ms only while a fade runs; any other end of the flash cuts it.
  The win animations stay exactly XP's (Solitaire's bouncing cards, FreeCell's big king).
* **Test hooks.** `FCHD_ANIM_SLOW` / `SOLHD_ANIM_SLOW` = N make every animation N times slower; with the
  e2e driver's `async on` (an input returns without waiting for the target; the target answers captures
  while it animates) the scenarios capture cards in mid-air.
* **Vanilla unchanged.** The sessions only gained Solitaire's deal counter (`SolSession.deals`, for the
  view) and the option-gated flights above, so `make sol-xp-compare` (1100048 lines) and `make fc-xp-compare` (1263045 lines) stay
  identical, and `make snapshots` renders the same 60 PNGs byte for byte (the new view fields default to
  XP's look).
* Tests: `tests/engine/test_ease.c` (the control points; endpoints, before the start and after the end;
  monotonic for many durations; the tables against a double cubic-bezier; the shapes: standard ahead of a
  straight flight after its first tenth, decelerate always ahead and 30 % of the way in 10 % of the time,
  accelerate always behind; rounding; the integer square root; XP's pace; every duration never longer than
  XP's, never over 160 ms, monotonic in the distance; 2000 random cascades ending no later than XP's
  one-by-one flights, cards landing in order), `tests/engine/test_image.c` (the shadow's size, darkness,
  symmetry and fall-off; the partial inversion), `tests/solitaire/test_sol_extras.c` (the double-click and
  the autoplay flown with the option, not without, the same game either way), `tests/freecell/test_layout.c` and
  `tests/solitaire/test_sol_layout.c` (a column's in-air cards hidden with its step kept, the top row's
  layers, the pulse levels 0 / 128 / 256), `test_motion.c` / `test_sol_motion.c` (the numbers above),
  `tests/e2e/fchd_motion.txt` (a flight caught mid-air, autoplay with two cards in the air, the Finish
  button absent, shown, gone after Undo, back after Redo, its click finishing the game; the option, the
  deal in mid-air, the drag shadow's pixels), `tests/e2e/solhd_motion.txt` (the automatic moves mid-air,
  the option, two cards turning over mid-flip, the drag shadow, the zip-back mid-air, the deal in
  mid-air, deal 1's ace turning then flying home, the Finish button's double-click leaving the cascade
  running), `solhd_extras.txt` (the Finish button absent, then shown before Finish).

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
  `StandardSupermove`, `FullRangeDeals`, `FullScreen` (v1.1), `WarnUnwinnable`, `AutoFinish` (v1.2),
  `SingleClick`, `DragDrop` (v1.4), `EnhancedAnimations` (2d). Solitaire HD's v1.2 options: `AutoHome`,
  `ClickSelect`; 2c: `NoMoreMoves`, `NextGameOptions`; 2d: `EnhancedAnimations`.
* Solitaire HD: Options and Back in XP sol.exe's own key and format (`HKCU\Software\Microsoft\Solitaire`,
  shared with sol.exe); window placement, full screen and the v1.1 extras' options in
  `HKCU\Software\xp-cards\Solitaire HD`; the statistics and the saved game in
  `%APPDATA%\xp-cards\Solitaire HD\statistics.bin` (version 2 since 2c, with the high scores and the
  money; version 1 is migrated) and `game.bin` (see "Solitaire HD extras").

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
* v1.4 (`make test`, `tests/freecell/test_session.c`): Single click (each step of the destination order,
  the free cell's card, nowhere: selected, the next click, the run onto a column, the dialog for an empty
  column, the ignored double-click, off = XP), drag and drop (the press, the cards a drag lifts, the
  drop's legality, the drop not flown but its autoplay flown, a free cell's card, refused drops with
  messages on and off, the supermove limit, the dialog's Cancel and Move column, the release with and
  without Single click), Undo All (asked, No, Yes, no flights, the move counter, the group Redo, then the
  single Redo, a new action dropping the group); a third random playout with Single click, drags, drops
  and Undo All; `tests/freecell/test_layout.c` checks the lifted stack's sprite over the board without
  it (`fc_render_stack`). `make fc-xp-compare` (above). `tests/e2e/fchd_ux.txt` (Wine): the Options,
  Ctrl+Z held (auto-repeated key-downs), Undo All's question and its Redo, single clicks (a free cell, a
  column, the ignored double-click, a run), drags (a card to a free cell, onto a column, a run lifted
  and captured mid-drag, a refused drop and its message).
* `make snapshots` — native renders of the board to PNG at several window sizes/states for review.
* `make xpcheck` — verifies every import of the exes exists on Windows XP SP2.
* `tools/wine/` — end-to-end driver run under Wine (launch, click, capture the client area).
* `make e2e` runs `tests/e2e/fchd_*.txt` (`make e2e-freecell`) and Solitaire HD's `tests/e2e/solhd_*.txt`
  (`make e2e-solitaire`: mouse and keyboard play, the stock and Vegas, the dialogs and the registry, the
  window, the win cascade; `solhd_extras.txt`: the v1.1 extras' Options group off by default and its
  registry values across restarts, the hint's flash captured mid-flash, single-click moves and the
  ignored double-click, auto-turn scoring with Undo / Redo, the Statistics dialog's text (reset, a loss,
  wins), saving at Exit and resuming, Finish by hand and automatically on deal 64's solver line, the
  unwinnable warning on deal 66 (Draw One, Vegas) and the winnable deal that replaces it).
* Solitaire HD's extras (`make test`): `tests/solitaire/test_sol_extras.c` (auto-turn scoring, one undo
  step and Redo; the click-to-move ranking and flow; Finish order, flights, one action and the win;
  the hint's ranking, its fairness under shuffled hidden cards, its flash, input ending it, following it
  to the end; statistics bookkeeping per mode with streaks, best time and score, the file and damaged
  files; save / resume round trips with the history, Undo / Redo after them, damaged, truncated and
  foreign files; the winnable-seed choice per case; the warning's requests, once, re-arming, stale
  answers; the registry values), `test_sol_playout.c` (random play with every extra on, against a shadow
  history), `test_sol_solver.c` (the solver, every solution replayed through the session, the table
  cross-checked); `make sol-xp-compare` (above).
