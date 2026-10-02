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

## 1b. Solver-based extras (v1.2) — done
- [x] Solver (`src/core/solver.c`): weighted best-first over session actions; 31997 of deals
  1..32000 solved with the default budget, #11982 proven unwinnable.
- [x] Hint: Game > Hint (H) flashes the move (source, then destination); cached along the solution.
  Option "Warn when the game can't be won" (off by default).
- [x] Auto-finish: Game > Finish (F6), enabled only when the rest is a sure win; option "Finish
  automatically" (off by default).
- [x] Drag-and-drop as an optional input mode (v1.4, §2b; XP click-click stays the default).

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
- [x] Solitaire HD v1.1 extras (docs/DESIGN.md "Solitaire HD extras"): Game > Hint (H, fair: only
  visible cards, heuristic ranking, FreeCell's flash), Game > Finish (F6, animated, one undo step),
  Game > Statistics (F4, per Draw x Scoring mode: played/won/%/streaks/best time/best score, in
  %APPDATA%); Options > Extras, all off by default: turn cards over automatically, single click moves a
  card, finish automatically, deal only winnable games (XP seeds + the solver's precomputed table), save
  game on exit / resume at start, warn when the game can't be won (background solver). With all of them
  off the session is v1.0's (`make sol-xp-compare`).

## 3. Spider HD, then Hearts HD
- Spider: rules + difficulty levels, hints, deal animation, on Solitaire's infrastructure.
- Hearts: computer opponents worth playing against, passing, scoring; XP network play dropped.

## 4. Beyond XP
- Windows 10/11: per-monitor high-DPI awareness, optional 64-bit build.
- VMs/emulators (VirtualBox, 86Box): check performance and scaling there.

## 2b. UX batch after Solitaire extras (decided 2026-10-01; behaviour changes opt-in, defaults = XP) — done
docs/DESIGN.md "FreeCell HD extras (v1.4)" and "Solitaire HD extras (v1.2)".
- [x] Solitaire: "Move cards home automatically": safe auto-moves to the foundations (FreeCell's autoplay
  rule on Klondike's foundations), after every committed action, cascading, flown, one undo step,
  scored as XP's foundation moves.
- [x] Solitaire: "Click to select, click to move" alongside dragging (with "Single click moves a card"
  too: a click moves when the card has a place, selects when not).
- [x] FreeCell: "Single click moves a card" (home if safe, a column, an empty column, home, a free cell;
  nowhere: XP's selection) and "Drag and drop cards" (the engine's drag code, `engine/win32/drag.h`, from
  Solitaire; the drop is XP's move to that pile). `make fc-xp-compare`: with both off, 1.3's session.
- [x] Both (always available, no option — they only act when invoked): Game > Undo All (asks first;
  one Redo brings it all back), Ctrl+Z (Undo, repeating while held); Solitaire keys D (draw), C (card
  back) (F4 Statistics: done in v1.1).
- [x] Solitaire's winnable-deal table regenerated with the fixed solver (`make seed-tables`).
- Source of ideas: docs/msc-feature-gap.md.

## 2c. Windows 7-inspired batch (decided 2026-10-01; source docs/win7-feature-gap.md; behaviour changes opt-in)
docs/DESIGN.md "Windows 7-inspired extras (2c)".
- [x] Solitaire: "No More Moves" detection (End Game / Return to Game): option "Tell me when there are
  no more moves" (off); the fair dead-end tracking (a whole stock cycle, or the used-up stock, with no
  useful move) also makes Hint say "There are no more useful moves." instead of suggesting draws forever.
- [x] Hint cycling: pressing H again shows the next-best move (FreeCell + Solitaire; always on; the
  rankings are documented in assist.h / session.h).
- [x] Solitaire statistics: top-5 high scores with dates (Standard timed / not timed, Vegas); Vegas most
  won / most lost / current winnings; statistics.bin version 2 (version 1 migrated).
- "Large Print" card style (big indices, from the RevK generator's Large index option) — opt-in. (Card
  art: the cards-xplike branch.)
- [x] Richer New Game / Exit / saved-game prompts when save-on-exit is enabled (XP's plain behaviour stays
  the default); Restart This Game.
- [x] Solitaire: changing Draw/Scoring/Timed can apply to the next game instead of redealing: option
  "Apply option changes to the next game" (off).

## 2d. Motion polish (decided 2026-10-01) — done
docs/DESIGN.md "Motion (2d)".
- [x] Replace linear card flights with Material-style easing: standard easing cubic-bezier(0.2, 0, 0, 1)
  for moves between piles, decelerate (0, 0, 0.2, 1) for cards arriving (deal, autoplay home),
  accelerate (0.3, 0, 1, 1) for cards leaving the table; durations SNAPPY — never slower than the
  current (XP-paced) flight for the same move: ~60 ms short hops to ≤ 160 ms longest flights, scaled by
  distance; fast-start curves so a card leaves instantly; consecutive autoplay/finish cards overlap
  (next starts at ~60% of the previous) so long cascades finish faster than XP's, frame-timed (engine animation driver), Quick play = none.
  Applies to every existing motion (FreeCell moves/autoplay/undo/redo/finish, Solitaire auto-moves,
  double-click, finish, hint). Default ON: it only changes the timing curve, not what happens.
  (Done: engine/ease.h + the flight scheduler; at 1080p a FreeCell Finish of 52 cards 5.3 -> 3.2 s,
  Solitaire's 2.8 -> 1.7 s, no single flight longer than before.)
- [x] New effects XP never had — behind one Options checkbox "Enhanced animations" (default off):
  drag lift + soft shadow, slide-back of an illegal drop (eased), card turn-over flip, deal animation,
  hint pulse instead of hard inversion blinks; anything MSC animates that fits the XP look. (Done: the
  shadow, the Solitaire flip, the deal flying in (both games), the hint pulse, Solitaire's double-click and
  right-button autoplay flown (XP moves them at once); the slide-back is eased for everyone, with the
  shadow under Enhanced.)
- [x] Win animations stay exactly as XP (bouncing cards physics, FreeCell big king).
- MSC reference (game_animation.tuningdata, MSC 4.27): auto-solve plays 0.3 s/card, speeding up to
  0.1 s/card after 3 moves; card-move durations are hard-coded in its exe. Ours stays snappier
  (≤160 ms flights, overlapping cascades).
- [x] Finish button (both games, always on — no toggle, it can simply be ignored): when the rest is a
  sure win, a small XP-style push button appears on the table (MSC's "Solve"); clicking it = Game > Finish (F6). Players
  who prefer can ignore it and play the cards one by one.

## 2e. Stack-legibility card layout (decided 2026-10-01 by the user; tools/crisplab/stacklab.py)
- Ship XPLIKE (lab variant "D"): closest to the XP cards. Rank fitted fully inside the Solitaire strip
  (15/96 of the card; rank_scale .72, stroke 165, bottom .145), classic suit under the rank, pips raised
  to XP's positions (pip_scale 1.1, pip_top .104, pip_xs .94) and the court picture enlarged
  (court_top .113, court_sx 1.035) so pip tops / court art show in every strip like XP. Metrics: at or
  above XP on every strip measure; buried aces show ~40% of their suit (XP: 0%). Regenerate
  res/common/cards via stacklab.py svg --layout XPLIKE in make_assets.sh, refresh tests/card_golden.h.

## Bugs / TODO
- [fix in script, assets not yet regenerated] Rank glyphs (0, 3, 5, 6, 8, 9, Q...) were shaved ~1 px flat at
  top/bottom: thickening the rank stroke (80 -> 130) made it overflow the glyph <symbol> viewBox, which
  clips. tools/edit_card_svg.py now sets overflow="visible" on the rank symbols; regenerate
  res/common/cards together with the XPLIKE layout (2e) and check stacklab's own rank transform too.
- TODO (decided 2026-10-02): "Save game on exit, resume at start" must be silent (Win7's "Always save
  game on exit" / "Always continue saved game"); the Windows 7 prompts move to their own opt-in checkbox
  "As&k before saving or resuming" (AskSaveGame, default off) — gate in session.c (new game ~1258,
  sol_exit_choice, sol_offer_resume), Options row after SaveGame, tests + solhd_extras/solhd_win7 e2e.
