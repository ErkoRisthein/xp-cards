# Windows 7 card games vs the XP games — feature gap

Source: the English MUI resource files (menus, dialogs, string tables) of the Windows 7 FreeCell,
Solitaire and Spider Solitaire (Oberon Games for Microsoft, 2009), from the community repackage
github.com/sun12yyds/Windows7Games_for_Windows_11_10_8; analysed 2026-10-01 by dumping the resources
(no code executed or analysed). Quotes are verbatim UI text. See also msc-feature-gap.md.

## What Windows 7 added over XP (all three games unless noted)

| Win7 feature (quote) | XP | HD today | Verdict |
|---|---|---|---|
| Options: "Display animations", "Play sounds", "Show tips", "Always continue saved game", "Always save game on exit" | FreeCell "Quick play" only | FreeCell Quick play ✓; Solitaire save-on-exit ◐ | Save/continue ✓ planned. Sounds/tips: skip. |
| Exit prompt: "Exit and Save My Game / Exit and Don't Save (counts as a loss) / Don't Exit"; "Saved Game Found: Continue Saved Game / Play New Game"; "You can turn off this prompt in Options" | resign Yes/No | – | Candidate (opt-in, with save-on-exit). |
| New-game prompt: "Quit and Start a New Game / Restart This Game / Keep Playing" (FreeCell adds "Quit and Choose a Specific Game") | resign Yes/No | – | Candidate: richer prompt only when an extras option is on; XP Yes/No stays default. |
| "Changed Game Settings: Play New Game / Finish This Game — the new settings apply to your next game" | XP Solitaire redeals immediately | – | Candidate (Solitaire): don't kill the game in progress when changing Draw/Scoring. |
| "No More Moves: End Game / Return to Game (use UNDO)" — also Solitaire and Spider | FreeCell only | FreeCell ✓ (XP), warning extra ✓ | **Candidate:** Solitaire "no more moves" detection (cycle of the stock with no progress). |
| Game Won / Game Lost summary: Score, Time, Time Bonus, Total Score, High Score, Games played/won, Win %, Date | XP: "Deal again?" / win king | – | Candidate: optional stats summary in the win dialog. |
| Statistics: Games played/won, Win %, Longest winning/losing streak, Current streak, **High Scores table (top 5 with dates)**; Solitaire per mode ("Standard Timed", "Standard Non-Timed", "Vegas") with "Most Money Won / Most Money Lost / Current Winnings"; Spider per difficulty; "Reset" | FreeCell streaks only | FreeCell XP stats ✓; Solitaire stats ◐ | **Candidate:** add the top-5 high-score table + Vegas money stats to Solitaire's Statistics. |
| Hint (H / menu); debug menus reveal "Toggle Hint Rankings" — hints are ranked and repeated presses cycle | Spider only | FreeCell ✓, Solitaire ◐ | **Candidate:** pressing H again shows the next-best move. |
| Undo: Ctrl+Z, unlimited | single | ✓ | Ctrl+Z alias planned (UX batch). |
| FreeCell "Select a game number from 1 to 1,000,000 … This will count as a loss" | ✓ | ✓ | — |
| FreeCell empty-column dialog: "Move All (using available freecells and empty stacks) / Move Single Card / Do Nothing" | Move column (free cells only) | standard-supermove option ✓ | Already covered by the "Standard multi-card moves" option. |
| "Change Appearance": decks "Classic", "Hearts", "Seasons", "**Large Print Deck**"; backgrounds "Classic Felt", "Red Hearts", "Green Nature", "Red Felt", "Brown Felt"; "Randomly choose deck and background" | backs only | 12 HD backs (Solitaire) | **Candidate: Large Print deck** (opt-in card style with big indices — the RevK generator's "Large" index option) for small windows; table colours: skip (XP green is iconic). |
| Spider: "Select Difficulty" prompt at first start: Beginner (one suit) / Intermediate (two suits) / Advanced (four suits) | Options | — | For Spider HD. |
| Spider statistics show Score and Moves | score | — | For Spider HD. |
| Debug menus (not user features): Force Win/Lose, Autoplay, Toggle Allow Any Move | cheats | — | Our cheats follow XP. |
| Direct3D rendering, "High Resolution Resources", Media Center editions, "Get More Games Online" | – | – | Skip. |

## Suggested additions (all opt-in; defaults stay XP)
1. Solitaire: "no more moves" detection (Win7/MSC) — End Game / Return to Game.
2. Hint cycling: pressing H again shows the next-best move (both games).
3. Solitaire statistics: top-5 high scores with dates; Vegas most won/lost/current winnings.
4. Large Print card style (big indices) — readability at small window sizes.
5. Richer New Game / Exit prompts (Restart / Keep Playing / Save) when save-on-exit is enabled.
6. Solitaire: changing Draw/Scoring applies to the next game instead of redealing (Win7 behaviour).
