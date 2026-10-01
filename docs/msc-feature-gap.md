# Microsoft Solitaire Collection vs the XP games — feature gap

Source: Microsoft Solitaire Collection 4.27.9181.0 (x64 Store package), analysed 2026-10-01 from its
English UI strings (`loc_archives/en/loc.archive`: a ZIP whose entries are Brotli-compressed
`.binloc` string tables, method id 34) and its data archive layout (`archives/data-*.archive`,
e.g. `solvableseedpacks/{default,supereasy}/<mode>` per game). Only features visible in the UI
strings are listed; no code was analysed. Quotes are verbatim UI strings.

Status columns: **XP** = the original game had it; **HD** = FreeCell HD / Solitaire HD today
(✓ done, ◐ in progress, – not yet). Everything we add stays **opt-in** (default = vanilla XP).

## Features across all games

| MSC feature (quote) | XP | HD | Notes / recommendation |
|---|---|---|---|
| Hints — "Click hint or press [H] to highlight an available move" | Spider only | FreeCell ✓, Solitaire ✓ (v1.1) | Spider: XP already had hints (M key). |
| "Automatic hints" option | – | – | Candidate: show a hint after N idle seconds. Low value. |
| "Single tap to move" — "Selected cards move automatically to the next available location" | – | Solitaire ✓ (v1.1, opt-in) | Candidate for FreeCell (XP FreeCell is click-click, so this would replace the second click; opt-in). |
| "Double-Tap Shortcut" — double-tap to send to the foundation | Solitaire | Solitaire ✓ | FreeCell's XP double-click goes to a free cell (kept). |
| Unlimited undo; "press and hold [Ctrl+Z] to quickly undo multiple moves" | single level | ✓ (F10 / Ctrl+Y redo) | Candidate: Ctrl+Z as an extra Undo key (auto-repeat), cheap. |
| "Undo All" — "Undo all moves and return to the start of the game. This does not reset the gameplay timer." + "Undo all alert" (confirmation) | – | – | **Candidate (cheap):** Game ▸ Undo All with confirmation, both games. |
| Solver — "Use Solitaire Solver to help complete the game", "Solver animation", "Resume Solver", "Auto-solving remaining cards" | – | FreeCell: Finish ✓ | **Candidate:** FreeCell "Solve" that plays the solver's line with animation (solver already exists). MSC's needs Internet/ads; ours offline. |
| Solvable decks — "All decks are solvable", "Random decks might be unsolvable", "Choose difficulty on new games", difficulties Easy/Medium/Hard/Expert/Grandmaster + "Random" | – | Solitaire "winnable only" ✓ (v1.1, opt-in) | Candidate: difficulty tiers from solver effort (nodes/solution length). FreeCell: all classic deals except #11982 are winnable already. |
| "Enter Game number" — "The game with this number will not count toward your statistics" | FreeCell only | FreeCell ✓ | **Candidate:** Solitaire Select Game over XP's 32768 seeds (XP had it as dead code, ids 1005–1009). |
| Statistics — Games played/won, Win percentage, Current winning streak, Longest winning/losing streak, Best time, Average time per game, Average score per game, Total score, High score | FreeCell: won/lost/streaks; Spider: high score, wins | FreeCell XP stats ✓, Solitaire ✓ (v1.1: per mode, best time and score) | Candidate: best/average time in FreeCell's Statistics (opt-in display). |
| "Gameplay timer" toggle | Solitaire "Timed game" | FreeCell ✓ (Time & moves), Solitaire ✓ | — |
| "Show invalid move alert" | FreeCell "messages" | ✓ | — |
| "Victory animation" toggle; random win animation from older Windows versions | fixed | – | Candidate: toggle; optional alternate win animations (e.g. Win7-style) — low priority. |
| "Left Hand Mode" / "Layout direction" — mirror the layout | – | – | Candidate (moderate): mirror board for left-handed play. |
| Full screen — "Alt+Enter or F11 toggles fullscreen" | – | ✓ | — |
| Keys — "F4 Statistics, F5 Options, F7 Themes … H displays hints, C changes card backs, D draws cards" | F2–F5 partly | partly | **Candidate (cheap):** Solitaire: D = draw, C = card back dialog, F4 = Statistics. |
| "Reset Cumulative Vegas" | changing Options resets it | – | Candidate (cheap): explicit reset button in Solitaire Options. |
| "Reset statistics" | FreeCell "Clear" | ✓ both | Solitaire's Statistics has Reset (with a confirmation). |
| Themes, Dynamic Lighting, score/XP animations, mobile cards, tutorials/tips, daily challenges, events, Star Club, achievements/badges/ranks (e.g. "FREECELL GRANDMASTER"), friends leaderboards, cloud stats, ads/premium | – | – | **Skip** — not in the spirit of the XP games. |

## Per game

### Klondike (Solitaire HD)
MSC modes: Draw 1 / Draw 3 × Standard / Las Vegas / Cumulative Vegas — same as XP. "When playing
Vegas scoring, you can cycle through the deck twice per game" (MSC, Draw 1?) vs XP's 1 pass (draw-1) /
3 passes (draw-3): keep XP's limits. Gaps beyond the in-progress extras: Undo All, Select Game
(game number), D/C/F4 keys, Reset Cumulative Vegas, difficulty tiers, left-hand mode.

### FreeCell (FreeCell HD)
MSC adds over XP: single tap to move, solver playback, difficulty-tiered solvable deals, average/best
time stats, Undo All. FreeCell HD already has hints, finish, unwinnable warning, unlimited undo/redo,
time & moves, won-deal tracking, full screen, standard supermove option, full 1..1,000,000 range.

### Spider (next game)
MSC: "Number of suits" 1/2/4 (XP Spider: Easy/Medium/Difficult = 1/2/4 suits — same), solvable decks
per suit count (`spideronesuit`, `spidertwosuits`, `spiderfoursuits` seed packs), hints (XP had),
Undo All, statistics (played/won/streaks/best time/high score), "can't deal a new row while any
columns are empty" (same as XP), completed runs removed automatically (same as XP).

### Pyramid, TriPeaks
Not XP games — out of scope (could be a future "bonus" if wanted).

## Suggested next batch (all opt-in)
1. Undo All (both games; confirmation).
2. Solitaire: Select Game (XP seeds), D / C / F4 keys, Reset Cumulative Vegas.
3. FreeCell: Solve (animated solver playback), single-click to move (opt-in), best/average time.
4. Later: difficulty tiers for winnable deals, left-hand mode, victory-animation toggle.
