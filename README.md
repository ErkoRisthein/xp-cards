# FreeCell HD

Windows XP FreeCell, re-implemented from scratch for a freely resizable window. The cards scale
with the window and stay crisp on a 1920x1080 screen. It runs on the real thing: Windows XP SP2/SP3,
32-bit.

XP's FreeCell draws fixed 71x96-pixel cards in a window no wider than 640 pixels. FreeCell HD keeps
XP's layout, rules, menus, dialogs, keys and statistics, and scales the whole board to the window. At
1:1 (a 632x427 client area) it matches XP's geometry pixel for pixel. Maximized at 1080p the cards are
about 190x257 pixels.

![FreeCell HD, game #1, maximized on a 1920x1080 screen (1904x996 client area)](docs/screenshots/game1-1904x996.png)

*Game #1 in the client area of a maximized 1920x1080 window, rendered by `make snapshots`.*

The same engine runs **Solitaire HD**, XP's Klondike (`SolitaireHD.exe`, see [Solitaire HD](#solitaire-hd)
below).

## Download

Get `FreeCellHD.exe` from [GitHub Releases](https://github.com/ErkoRisthein/xp-cards/releases). The game
is that one 32-bit file, with no installer and no DLLs. Copy it anywhere and run it. It needs a
Pentium Pro / Pentium II, an AMD Athlon or Duron, or any later x86 CPU (it uses CMOV; SSE is not
needed). A Pentium MMX, AMD K6 or early VIA C3 cannot run it.

XP's built-in browsers can no longer open GitHub because they lack TLS 1.2. Download the file on another
computer and copy it over with a USB stick or a network share, or use `make deploy` (see below).

FreeCell HD uses XP FreeCell's own registry key for statistics and options, so your existing record
carries over, and the two programs stay in sync.

## How to play

The rules are XP's: the same deals for the same game numbers, the supermove limit of
(free cells + 1) x (empty columns + 1), the same automatic moves to the home cells, and the same
"Move to Empty Column" choice. Click a card to select it, then click where it should go.

| Input | Action |
|---|---|
| F2 | New game (a random deal from 1 to 32767, as in XP) |
| F3 | Select game: 1 to 1,000,000, or the special deals -1 and -2 |
| F4 | Statistics |
| F5 | Options: messages on illegal moves, quick play (no animation), double-click to a free cell, and the extras below |
| F10 or Ctrl+Z | Undo (Ctrl+Z is new; hold it to undo move after move) |
| Ctrl+Y | Redo (new) |
| Game > Undo All | Back to the start of the game, after a question (new); Redo brings it all back |
| H | Hint (new): the card to move flashes twice, then the place it goes; H again shows the next-best move |
| F6 | Finish (new): send every card home, once the rest is a sure win |
| F11 or Alt+Enter | Full screen (new); Esc leaves it |
| F1 | Help. Opens XP's `freecell.chm` when it is present, otherwise a built-in summary |
| 1 to 8 | Select that column or move to it. Press it again on a selected column to show each of its cards in turn |
| 0 | Select the next free-cell card, or move the selected card to a free cell |
| 9 | Move the selected card to its home cell |
| Double-click | Send a column's bottom card to the leftmost empty free cell |
| Right button (hold) | Peek at a covered card |

XP's hidden Ctrl+Shift+F10 dialog is there too.

## What differs from XP

- The window can be resized and maximized, and the board scales with it. A column too long for the
  window is drawn with its cards closer together.
- Undo is unlimited, and Ctrl+Y redoes. Each step is one action plus its automatic moves, as in XP.
- The window size, position and maximized state are remembered between runs.
- The art is new: CC0 vector card faces rendered at high resolution, and a new king, icon and cursor.
  No Microsoft bitmaps are used.
- A few XP bugs are fixed instead of copied, for example a stale Undo after a new deal and two
  messages with missing spaces. `docs/xp-reference/rules.md` lists them.
- Game > Full Screen shows the board without a window frame (the menu bar stays).
- Select Game says when you have won a game before, and Statistics counts the different games won.

## Extras

Options > Extras has these, all off by default, so the game looks and plays like XP until you turn
them on:

- **Show time and moves** in the menu bar, next to "Cards Left".
- **Standard multi-card moves**: (free cells + 1) x 2^(empty columns) instead of XP's limit.
- **New Game picks from all 1,000,000 games** instead of XP's 1 to 32767.
- **Warn when the game can't be won**: a built-in solver checks the position in the background
  after every move and tells you, once, when the game can no longer be won, so you can undo.
- **Finish automatically**: as soon as the rest is a sure win, every card goes home.
- **Single click moves a card**: a click sends the card to the best place: home when it is safe there,
  else the first column it fits on, an empty column, home, a free cell. If there is none, the card
  stays selected as in XP, and the next click puts it where you want.
- **Drag and drop cards**: drag a card (or the run below the card you grab) to where it should go. The
  drop is XP's move to that pile, with XP's supermove limit and "Move to Empty Column" choice; a move XP
  would refuse slides back. Clicking works as before.
- **Enhanced animations**: effects XP never had, all quick: dragged cards cast a soft shadow, a new
  game's cards fly in from below the board, and the hint fades in and out instead of blinking.

The solver also drives two new Game menu items. **Hint** (H) shows a winning move: the card to move
flashes twice, then its destination. If the game can no longer be won, it says so. Repeated hints are
instant while you follow them. Pressing H again in the same position shows the next-best move (the other
moves ranked by how much they leave to do), and after the last one the first again. **Finish** (F6) is available once every remaining card can go home in
order, and moves them there as one move; while it is available a small **Finish** button sits on the
table below the king (ignore it to play the cards yourself).

Cards fly as in XP, only smoother: each flight eases in and out and never takes longer than XP's
(160 ms at most), and the cards of an autoplay or a Finish overlap (the next one leaves while the
previous one is still on its way), so a long cascade is over sooner. Quick play still flies nothing.

See [docs/ROADMAP.md](docs/ROADMAP.md) for what comes next: Spider and Hearts.

## Solitaire HD

`SolitaireHD.exe` is Windows XP Solitaire (Klondike, `sol.exe`) re-implemented the same way: XP's deals
(the same random number generator and shuffle), rules, Standard / Vegas / Cumulative / None scoring,
timed game, menus, dialogs, keyboard interface and win cascade, with the board scaled to the window.
It uses XP Solitaire's own registry key for its options and card back, so the two stay in sync.

| Input | Action |
|---|---|
| Drag | Move cards, as in XP (Options > Outline dragging too) |
| Double-click | Send a card to its foundation |
| Right button | Play every card that can go to the foundations |
| F2 | Deal |
| Ctrl+Y | Redo (new); Game > Undo is unlimited |
| Ctrl+Z | Undo (new); hold it to undo action after action |
| Game > Undo All | Back to the deal, after a question (new); Redo brings it all back |
| D | Turn over cards from the deck (new) |
| C | Select Card Back (new) |
| H | Hint (new); H again shows the next move |
| F6 | Finish (new): every card home, once the stock and the waste are used up and every card is face up |
| F4 | Statistics (new) |
| F11 or Alt+Enter | Full screen (new); Esc leaves it |
| Arrows, Tab, Home, End, Enter, Space, Esc | XP's keyboard play |

Differences from XP: the scaled, resizable window and full screen; unlimited undo and redo; the window
placement remembered; new card art and 12 new card backs (none animated, as in XP); two deals in the same
second are not the same deal; and the XP bugs listed in `docs/xp-reference/solitaire/rules.md` §11 are
fixed.

**Game menu (always there, they change nothing until used).** **Hint** (H) flashes a sensible legal
move: the cards to move twice, then where they go. It only uses what you can see, never the face-down
cards or the order of the stock. Pressing H again in the same position shows the next move of its
ranking, and after the last one the first again. Once a whole pass through the stock has shown nothing
useful (or the stock is used up), it says "There are no more useful moves." instead of suggesting
another draw. **Finish** (F6) sends every card home, the flying cards overlapping, as one move you
can undo; while it is available a small **Finish** button sits on the table next to the foundations. **Statistics** (F4) shows games played and won, the win percentage, the current
and longest streaks, the best time and the best score, kept separately for each of the six modes (Draw
One / Three x Standard / Vegas / None), and, as in Windows 7, the five best scores with their dates
(Standard: timed games and untimed ones apart) and, for Vegas, the most money won and lost in a game
and the winnings so far. A game counts once you have made a move; a game you leave
unfinished counts as lost, unless "Save game on exit" keeps it for the next start.

**Options > Extras**, all off by default, so Solitaire HD plays exactly like XP until you turn them on
(`HKCU\Software\xp-cards\Solitaire HD`):

- **Turn cards over automatically**: a face-down card left on top of a column turns over by itself,
  scoring the same 5 points as XP's click (Standard scoring).
- **Single click moves a card**: a click that does not drag sends the card (and the cards on it) to
  the best place: a foundation first, then a column. Dragging and double-clicking work as before.
- **Finish automatically**: Finish runs by itself as soon as it can.
- **Deal only winnable games**: Deal still uses XP's deals, but skips to the next one that the built-in
  solver has proven winnable for the current draw and scoring (Vegas: its pass limit).
- **Save game on exit, resume at start**: the game in progress, with its score, time and undo history,
  is saved when you exit and comes back at the next start. With it, a game in progress gets Windows 7's
  questions: Deal asks "Quit and Start a New Game / Restart This Game / Keep Playing", Exit asks "Exit
  and Save My Game / Exit and Don't Save / Don't Exit", and the start asks "Continue Saved Game / Play
  New Game".
- **Warn when the game can't be won**: a solver checks the position in the background after every move
  and tells you, once, when the game can no longer be won, so you can undo.
- **Move cards home automatically**: after each move, every card that is safe on its foundation (an ace
  or a two, or a card whose rank - 1 of the other colour is already home) flies there, as part of that
  move (one Undo), scored as usual.
- **Click to select, click to move**: click a card to select it (it turns inverted), then click where
  it should go. Dragging still works. With "Single click moves a card" on too, a click moves a card that
  has a place to go and selects one that has none.
- **Tell me when there are no more moves**: when a whole pass through the stock (or the used-up stock)
  offers nothing useful, Windows 7's question: End Game (counts as a loss, then "Deal Again?") or Return
  to Game (and use Undo).
- **Apply option changes to the next game**: changing Draw, Scoring or Timed game during a game asks
  "Play New Game / Finish This Game" instead of XP's immediate new deal; the game you finish keeps its
  own settings.
- **Enhanced animations**: effects XP never had, all quick: dragged cards cast a soft shadow, a card
  turns over with a flip, a new deal flies out of the stock, cards sent home with a double-click or the
  right button fly there, and the hint fades in and out instead of blinking.

Statistics and the saved game are files in `%APPDATA%\xp-cards\Solitaire HD`.

## Building

The exe is cross-compiled with mingw-w64 for i686. It links `msvcrt.dll` (XP has no UCRT) and
links the GCC runtime statically, so the result is a single file.

**macOS**

    brew install mingw-w64
    make                  # build/FreeCellHD.exe and build/SolitaireHD.exe
    make freecell         # build/FreeCellHD.exe only
    make release          # the same, stripped (-s), then the XP import check

Homebrew's toolchain defaults to the UCRT. The Makefile passes `-mcrtdll=msvcrt-os` (GCC 14+).

**Linux (Ubuntu 24.04)**

    sudo apt install make gcc python3 gcc-mingw-w64-i686 binutils-mingw-w64-i686
    make

Ubuntu's GCC 13 has no `-mcrtdll`, but its mingw-w64 already defaults to `msvcrt.dll`. The Makefile
detects this and leaves the option out. With a toolchain that has neither, the build fails instead of
producing an exe that XP cannot load.

**Copying to an XP machine.** `make deploy` copies the exe to a share on the XP machine over SMB1, which
macOS's own SMB client no longer supports. It needs a Python with [impacket](https://pypi.org/project/impacket/):

    python3 -m venv $HOME/.venvs/impacket && $HOME/.venvs/impacket/bin/pip install impacket
    make deploy PYTHON=$HOME/.venvs/impacket/bin/python XP_HOST=MYXPBOX XP_IP=192.168.1.50

`XP_SHARE`, `XP_DIR`, `XP_USER` and `XP_PASSWORD` are read from the environment (see `tools/deploy_smb.py`).

## Tests

    make test         # native unit tests of the engine and of the rules, controller and layout (ASan + UBSan)
    make snapshots    # render boards at several sizes to build/snapshots/*.png
    make xpcheck      # every function the exes import must exist on Windows XP SP2 and SP3
    make e2e          # end-to-end scenarios under Wine (tests/e2e/fchd_*.txt, solhd_*.txt)
    make sol-xp-compare   # Solitaire with every extra off plays exactly as v1.0 (the same random input)
    make fc-xp-compare    # FreeCell with the v1.4 extras off plays exactly as 1.3 (the same random input)
    make seed-tables  # Solitaire: re-solve every XP deal for "Deal only winnable games" (about 40 min)

The game logic and the shared engine (`src/freecell`, `src/solitaire`, `src/engine`, outside their `win32`
directories) are plain C with no Windows headers, so they are tested natively.
`make e2e` drives the real exe under Wine: it clicks, presses keys, answers dialogs, reads pixels and the
registry, and captures screenshots. The driver is documented in [tools/wine/README.md](tools/wine/README.md).
`make e2e` is set up for the macOS development machine. On Linux, run the smoke scenario under a virtual
display:

    xvfb-run -a -s "-screen 0 2560x1600x24" tools/ci/xvfb_wm.sh \
        tools/wine/run.sh -o build/e2e/smoke tests/e2e/fchd_smoke.txt

This needs `wine`, `wine32:i386`, `xvfb`, `openbox` and ImageMagick. `WINE`, `WINESERVER` and
`WINEPREFIX` override the defaults.

**Continuous integration.** `.github/workflows/build.yml` runs on every push and pull request on Ubuntu
24.04. It builds the stripped exes, runs `make test`, `make xpcheck` and `make snapshots`, and uploads the
exes and the snapshots as artifacts. A Wine smoke test runs as an informational job that cannot fail the
build. Pushing a tag `v*` (for example `git tag -a v1.1 -m "What's new" && git push origin v1.1`) also
publishes a GitHub Release with `FreeCellHD.exe` and `SolitaireHD.exe`. The release notes come from
`tools/ci/release-notes.md`, and an annotated tag's message becomes their "What's new" section. GitHub
disables Actions on a fork until its owner enables them on the fork's Actions tab.

## Repository layout

| Path | Contents |
|---|---|
| `src/engine` | the card-game engine shared by the games ([docs/ENGINE.md](docs/ENGINE.md)): images, card sprites, drawing, persistence, and in `win32/` the Windows pieces every game uses |
| `src/freecell` | FreeCell HD: rules, the XP game controller, statistics, the solver and the hint / finish logic, scalable layout, board renderer; `win32/` the window, menus, dialogs, registry, help |
| `src/solitaire` | Solitaire HD: rules, the XP game controller, the win cascade, scalable layout, board renderer; the extras: fair hint / click-to-move / finish, statistics, saved game, solver and the winnable-deal table; `win32/` the window, drag and drop, status bar, dialogs, registry, help, the solver's worker |
| `res` | `common/`: the card art shared by the games; `freecell/`, `solitaire/`: resource scripts, king art, icon, cursor, manifests |
| `tests` | native unit tests (`engine/`, `freecell/`, `solitaire/`), the snapshot renderer, Wine end-to-end scenarios |
| `tools` | XP import checker, Wine driver, SMB deploy, asset scripts, CI helpers |
| `docs` | [design](docs/DESIGN.md), [roadmap](docs/ROADMAP.md), reverse-engineered XP reference |

The original Windows XP binaries in the repository root (`freecell.exe`, `sol.exe`, `spider.exe`,
`mshearts.exe` and `cards.dll`) come from the upstream archival repository
[esc0rtd3w/xp-cards](https://github.com/esc0rtd3w/xp-cards). They are Microsoft's, are kept here only as
the reference for the reverse engineering, and are not part of FreeCell HD or Solitaire HD.

## Credits

- Card faces: SVG playing cards by Adrian Kennard — https://cards.revk.uk (CC0)
- King busts, program icon and cursor: made for this project and dedicated to the public domain (CC0).
  The king is derived from the CC0 King of Spades above. Details are in [res/LICENSE-ART.md](res/LICENSE-ART.md).
- [stb_image](https://github.com/nothings/stb) by Sean Barrett (public domain, or MIT)

## Disclaimer

FreeCell HD and Solitaire HD are independent re-implementations. They are not affiliated with or endorsed
by Microsoft. FreeCell was created by Jim Horne at Microsoft, after Paul Alfille's 1978 game for the PLATO
system; Windows Solitaire was written by Wes Cherry at Microsoft. Windows and Windows XP are trademarks of
Microsoft Corporation.
