# Roadmap

Priorities are set by value to actual play divided by effort, with work that unlocks later items done first.
Primary target: a Windows XP machine at 1920x1080. Newer Windows and VMs come later.

## 0. FreeCell HD v1 (in progress)
- Core rules/session, scalable graphics, resources, Win32 shell, test tooling.
- Verification: native unit tests, snapshot renders, XP import check, Wine end-to-end runs, pixel
  comparison against XP FreeCell at 1x.
- **Exit:** the user plays it on the real XP machine at 1080p, and the issues found are fixed.

v1 extras: unlimited undo + **redo** (Ctrl+Y), remembered window placement. Everything else is XP.

## 1. FreeCell polish + release pipeline (v1.1)
- Fixes from real-XP testing (theme metrics, fonts, performance on that hardware).
- GitHub Actions: build the exe with mingw-w64, run native tests and the XP import check, (later)
  a Wine smoke test; publish versioned GitHub Releases so each iteration is one download away.
- Extras, each behind an option that defaults to XP behaviour/look:
  - Timer & move counter in the menu bar next to "Cards Left" (off by default).
  - Track won deal numbers (shown in Select Game and Statistics).
  - Borderless full-screen mode.
  - Supermove rule option: XP (f+1)(e+1) default, standard (f+1)·2^e optional.
  - New Game range option: XP 1–32767 default, 1–1,000,000 optional.

## 1b. Solver-based extras (v1.2) — done, except drag-and-drop
- [x] Solver (`src/core/solver.c`): weighted best-first over session actions; 31997 of deals
  1..32000 solved with the default budget, #11982 proven unwinnable.
- [x] Hint: Game > Hint (H) flashes the move (source, then destination); cached along the solution.
  Option "Warn when the game can't be won" (off by default).
- [x] Auto-finish: Game > Finish (F6), enabled only when the rest is a sure win; option "Finish
  automatically" (off by default).
- [ ] Drag-and-drop as an optional input mode: waits for Solitaire, which builds the drag code (XP
  click-click stays the default).

## 2. Solitaire HD (Klondike, sol.exe)
- [x] Extract the shared card-game engine from FreeCell first (docs/ENGINE.md): images, the card set
  (now with optional card backs), clipped drawing, stats/options persistence interfaces, Win32 shell
  pieces (back buffer, animation clock, menu-bar text, placement, full screen, registry, dialogs, help,
  worker thread); FreeCell HD unchanged (same tests, snapshots and e2e captures). `make` builds
  `build/SolitaireHD.exe`, for now a stub window with XP's menu.
- [x] The game (docs/xp-reference/solitaire/): XP's rules and deals (XP's RNG), Standard / Vegas /
  cumulative / timed scoring, unlimited undo + redo, the keyboard interface, both cheats, XP's two bugs
  fixed; the scalable layout and renderer (XP's formulas scaled, the draw-3 fan, 3-D piles, O / X /
  ghost); 12 HD card backs (CC0, one per XP back; none animated, as in XP); the bouncing-cards win,
  frame-exact with XP's logs. No deal or flip animations (XP has none).
- [x] Win32 front end (`src/solitaire/win32/`): drag and drop (the lifted stack, Outline dragging, the
  zip-back of a refused drop), double-click and right-click autoplay, XP's status bar, the cascade and
  "Deal Again?", Options / Select Card Back, menu graying, help and About, full screen, the remembered
  placement; Wine end-to-end scenarios (`tests/e2e/solhd_*.txt`).
- The same fidelity process as FreeCell: reverse-engineer sol.exe and match XP pixel-for-pixel at 1x.

## 3. Spider HD, then Hearts HD
- Spider: rules + difficulty levels, hints, deal animation, on Solitaire's infrastructure.
- Hearts: computer opponents worth playing against, passing, scoring; XP network play dropped.

## 4. Beyond XP
- Windows 10/11: per-monitor high-DPI awareness, optional 64-bit build.
- VMs/emulators (VirtualBox, 86Box): check performance and scaling there.

## 2b. UX batch after Solitaire extras (decided 2026-10-01; behaviour changes opt-in, defaults = XP)
- Solitaire: safe auto-moves to the foundations (FreeCell's autoplay rule adapted to Klondike).
- Solitaire: click-click mode (click a card, then its destination) alongside dragging.
- FreeCell: single-click to move (best destination) and drag-and-drop (engine drag code from Solitaire).
- Both (always available, no option — they only act when invoked): Undo All menu item (with confirmation), Ctrl+Z hold-to-repeat undo; Solitaire keys D (draw), C (card back), F4 (Statistics).
- Source of ideas: docs/msc-feature-gap.md.

## 2c. Windows 7-inspired batch (decided 2026-10-01; source docs/win7-feature-gap.md; behaviour changes opt-in)
- Solitaire: "No More Moves" detection (End Game / Return to Game).
- Hint cycling: pressing H again shows the next-best move (FreeCell + Solitaire).
- Solitaire statistics: top-5 high scores with dates; Vegas most won / most lost / current winnings.
- "Large Print" card style (big indices, from the RevK generator's Large index option) — opt-in.
- Richer New Game / Exit / saved-game prompts when save-on-exit is enabled (XP Yes/No stays default).
- Solitaire: changing Draw/Scoring can apply to the next game instead of redealing (opt-in).
