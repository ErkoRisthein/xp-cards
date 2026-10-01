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

## 1b. Solver-based extras (v1.2)
- Hint (built-in solver suggests a move; can warn when the deal is no longer winnable).
- Auto-finish: Game menu "Finish" + shortcut, enabled only when the rest is a sure win; option to
  trigger it automatically.
- Drag-and-drop as an optional input mode (after Solitaire builds the drag code; XP click-click stays
  the default).

## 2. Solitaire HD (Klondike, sol.exe)
- Extract the shared card-game engine from FreeCell first: scaling layout primitives, sprite cache,
  renderer, animation, stats/options persistence, Win32 shell pieces, Wine test harness.
- New infrastructure: drag-and-drop, card backs (CC0 designs in the spirit of XP's 12, some
  animated), deal/flip animations, Standard/Vegas/timed scoring, status bar, bouncing-cards win.
- The same fidelity process as FreeCell: reverse-engineer sol.exe and match XP pixel-for-pixel at 1x.

## 3. Spider HD, then Hearts HD
- Spider: rules + difficulty levels, hints, deal animation, on Solitaire's infrastructure.
- Hearts: computer opponents worth playing against, passing, scoring; XP network play dropped.

## 4. Beyond XP
- Windows 10/11: per-monitor high-DPI awareness, optional 64-bit build.
- VMs/emulators (VirtualBox, 86Box): check performance and scaling there.
