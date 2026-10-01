# Roadmap

Priorities are set by value to actual play divided by effort, with work that unlocks later items done first.
Primary target: a Windows XP machine at 1920x1080. Newer Windows and VMs come later.

## 0. FreeCell HD v1 (in progress)
- Core rules/session, scalable graphics, resources, Win32 shell, test tooling.
- Verification: native unit tests, snapshot renders, XP import check, Wine end-to-end runs, pixel
  comparison against XP FreeCell at 1x.
- **Exit:** the user plays it on the real XP machine at 1080p, and the issues found are fixed.

## 1. FreeCell polish + release pipeline
- Fixes from real-XP testing (theme metrics, fonts, performance on that hardware).
- GitHub Actions: build the exe with mingw-w64, run native tests and the XP import check, (later)
  a Wine smoke test; publish versioned GitHub Releases so each iteration is one download away.
- Candidate extras (only if wanted): redo, borderless full-screen, auto-finish.

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
