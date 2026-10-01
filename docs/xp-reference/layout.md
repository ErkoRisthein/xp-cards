# XP FreeCell: geometry, rendering and interaction visuals (ground truth)

Source: `freecell.exe` (XP RTM, PE timestamp 0x3B7D8476, image base 0x01000000) plus `cards.dll`.
I used two methods:

* **(B) Disassembly.** I ran capstone over the whole `.text`. The listing is
  `research/layout/freecell.asm`, segmented at every call target.
* **(A) Live observation.** I ran the real exe under Wine 11.18 with the real XP `cards.dll`. A helper
  (`research/layout/fchelp.c`, plus the in-process capture hook `fchook.c`) drove it. The helper sent
  messages, read and wrote the game's globals with `ReadProcessMemory` / `WriteProcessMemory`, and
  captured the client area from inside the FreeCell process through a `WH_CALLWNDPROC` hook DLL.
  Cross-process `GetDC` capture returns black under Wine, which is why the hook was needed.

The globals I read live matched the formulas from the disassembly exactly.

Units: XP pixels. The card is **71 x 96** (`cw` x `ch`). `cdtInit` writes `cw` to `[0x1008370]` and
`ch` to `[0x1007868]`; see `sub_0100190b`. Ratios are given as multiples of `cw` and `ch`.

Screenshots are in `research/layout/shots/`. Files ending `_w.png` are the whole window, including the menu bar.

---

## 1. Window

| Item | Value | Evidence |
|---|---|---|
| Class | `FreeWClass`; style `CS_DBLCLKS` (8) only, with no `CS_HREDRAW`/`CS_VREDRAW`; `hCursor = NULL`; `hbrBackground` = green brush; icon 601; menu `FreeMenu` | RegisterClassW @0x1002218, wndproc 0x1001af9 |
| Style | `WS_OVERLAPPEDWINDOW` (0x00CF0000), ex-style 0 | CreateWindowExW @0x10016e7 |
| Default position | `CW_USEDEFAULT`, `CW_USEDEFAULT` (system cascade). **Position is not remembered.** | same |
| Default size (outer) | **640 x min(480, SM_CYSCREEN)** | 0x1001690–0x10016d4 |
| Client at default size | **632 x 427** under Wine (cxframe 4, cycaption 26, cymenu 19). On real XP the height depends on theme metrics, roughly 426–434 (UNVERIFIED). The width is always 632. | live `info` |
| Max tracking width | `WM_GETMINMAXINFO`: if `SM_CXSCREEN > 640`, set `ptMaxTrackSize.x = min(ptMaxTrackSize.x, 640)`. **The window can never be wider than 640.** Height is unlimited. | 0x1001c0a |
| Observed clamping | `SetWindowPos(1600x1000)` produced 640x1000 (`s10`). Maximize under Wine produced 640 x full height at the left edge (`s12_maximized_w_half.png`). The maximize result is Wine-observed; on real XP it is UNVERIFIED but expected to be the same. | live |
| Minimum size | None set by the app; the system default applies. Shrinking only clips the content (`s11_size500x350_w.png`). | |
| Title | `"FreeCell"` (str 301) at start. After a deal: `"FreeCell Game #%d"` (str 303) via SetWindowTextW. | 0x10016b6, 0x100320d |
| Window size/position saved? | No. The registry stores only stats and options (`messages`, `quick`, `dblclick`). | strings @0x1001230.. |

### Resizing behaviour (important)

The layout is computed **once**, in `WM_CREATE` (`sub_01002c39`, its only caller is 0x1001929), from the
client rect at creation. `WM_SIZE` (0x1001b3c) only calls `DrawMenuBar` and redraws the "Cards Left" text.
It does not recompute the layout and does not invalidate.

Consequences:

* The board is **anchored top-left** and never centres or rescales.
* Enlarging the window adds only empty green space to the right (impossible beyond 640 outer) and below.
* Shrinking clips the right and bottom (`s11`).
* Because the creation width is always 640, the effective client width is always **Wc = 632**.

The formulas themselves are written in terms of `Wc`. The top row is flush to both edges, the king is centred,
and the column spacing stretches with `Wc`. So applying the same formulas live to the current client width
gives a "stretch spacing" layout, which is the natural scalable generalisation.

---

## 2. Board geometry (from `sub_01002c39`; values for Wc = 632, verified live by memory read and pixels)

Globals: `[0x100706c]` homeX; `[0x1007070..0x100708c]` colX[1..8]; `[0x1007090]` topGap; `[0x1007098]` kingX;
`[0x1007980]` vertical step.

| Element | Formula (original code) | XP px | Ratio |
|---|---|---|---|
| Free cell i (0..3) | x = i*cw, y = 0, size cw x ch | x = 0, 71, 142, 213 | x = i*cw; **no margin at all** (flush with client left and top) |
| Home cell i (0..3) | x = Wc − 4cw + i*cw, y = 0 | x = 348, 419, 490, 561 | flush with client right edge |
| Gap between the two groups | Wc − 8cw | 64 | 0.901 cw |
| King bitmap (32x32) | x = (Wc − 32)/2 (signed /2), y = (ch − 32)/3 | (300, 21) | size 0.4507 cw = 0.3333 ch; y = (ch − K)/3 = 0.2188 ch |
| King frame | 1 px lines at x = kx−3 and kx+34, y = ky−3 and ky+34 | outer rect (297,18)–(334,55), 38x38 | 2 px background gap inside the frame |
| Column gap g | g = floor((Wc − 8cw)/9) | 7 | 0.0986 cw |
| Column x (k = 1..8) | x_k = g + floor((k−1)*(Wc − g)/8) | **7, 85, 163, 241, 319, 397, 475, 553** | pitch (Wc−g)/8 = 78.125 = 1.1004 cw; right margin 632 − 624 = 8 |
| Column top y | y0 = ch + topGap; topGap = 10 (4 if SM_CYSCREEN ≤ 350) | 106 | topGap = 0.1042 ch (0.1408 cw) |
| Vertical step | step = floor(9*ch/46); if SM_CYSCREEN ≤ 350, step = floor(step*4/5) | **18** (14 on a small screen) | 0.1875 ch (unfloored 9/46 = 0.1957) |
| Card at (col k, pos p) | (x_k, y0 + p*step) | e.g. col 1 card 6: (7, 214) | `sub_01002e28` |
| Max column slots | 21 entries per column (array 0x1007500, 0x54 bytes per column; column 0 = top row) | | |

* **No compression of tall columns.** The step is a constant computed at creation. `WM_PAINT` draws pos p at
  `y0 + 18p` unconditionally.
  * Verified with a poked 19-card column: its bottom card ends at y = 106 + 18*18 + 96 = 526, below the
    427 px client. It is simply clipped (`s14_longcol_640x480.png`). At 640x700 it is fully visible (`s15`).
  * With the default client height, columns up to 13 cards fit (106 + 12*18 + 96 = 418 ≤ 427).
* Empty tableau columns draw **nothing**: no outline, only background (`s13_poked.png`, column 6).
* Annotated diagram: `shots/annotated_layout_x2.png`.

### Hit testing (`sub_01002cfc`; matches the drawn geometry)

* **Top row** (y < ch):
  * x < 4cw → free cell `x / cw`.
  * homeX ≤ x < homeX + 4cw → home cell `4 + (x − homeX) / cw`.
  * Otherwise, including the king area, it is a miss.
* **Columns** (y ≥ ch + topGap):
  * col = (x − colX1) / (colX2 − colX1) + 1. It is a miss if x > colX[col] + cw (the gap between columns).
  * pos = min((y − y0) / step, 21).
  * A buried card is hit over its visible 18 px strip. The bottom card is hit over its whole height. Below the
    bottom card is a miss.
  * An empty column returns "miss" but still reports the column number. The cursor logic uses that (section 6).

---

## 3. Colours, pens, cell outlines

| Item | Value | Evidence |
|---|---|---|
| Table background | **RGB(0,127,0) = #007F00**, measured exactly (0,127,0) | CreateSolidBrush(0x7F00) @0x10021a3 |
| Light pen | **RGB(0,255,0)**, width 1, PS_SOLID | CreatePen(0,1,0xFF00) @0x1002193 |
| Dark pen | stock `BLACK_PEN` | GetStockObject(7) |
| Monochrome display (NUMCOLORS == 2) | white brush, black "light" pen | 0x1002157–0x100217e |
| King bitmap's own background | palette entry (0,128,0) = #008000, one unit off the table green and practically invisible | resource palette, pixel probe |

**Empty free/home cell** (a cw x ch bitmap pre-rendered in WM_CREATE @0x10019dc–0x1001a48, blitted when a slot is empty).
This is a sunken bevel, 1 px:

* Black: left edge x = 0 for y = 0..ch−2, and top edge y = 0 for x = 0..cw−2.
* Green #00FF00: right edge x = cw−1 for y = 1..ch−1, and bottom edge y = ch−1 for x = 1..cw−1.
* The corners (cw−1, 0) and (0, ch−1) stay background.
* Pixel-verified (`s02_game1.png`): black runs (0, 0..94) and (0..69, 0); green runs (70, 1..95) and (1..70, 95).

**King frame** (`WM_PAINT` @0x10035b5) is the opposite bevel, raised: **green on top and left, black on bottom and right**.

* Green: left x = 297, y = 18..54; top y = 18, x = 297..333.
* Black: right x = 334, y = 19..55; bottom y = 55, x = 298..334.
* Pixel-verified.

**Cards** come from `cdtDrawExt(hdc, x, y, 71, 96, card, mode, 0xFF)` (0x10033b7). Card index = rank*4 + suit,
with suit 0 = clubs, 1 = diamonds, 2 = hearts, 3 = spades. The value 52 marks an empty slot and draws the
empty-cell bitmap.

* Card corners are transparent: 3 pixels per corner (for the top-left, (0,0), (1,0) and (0,1)), plus one
  RGB(128,128,128) anti-alias pixel diagonally inside (1,1). The table or the card beneath shows through
  (`card_corner_x10.png`).
* FreeCell itself saves and restores exactly these 12 corner pixels with GetPixel/SetPixel around animations
  (`sub_0100470c`, `sub_010047e6`).

---

## 4. King

* Resources: `KINGBITMAP` (looks right, default), `KINGLEFT` (looks left), `KINGSMILE` (win). Each is 32x32 at 4 bpp.
  Extracted to `shots/res_*.png` and `*_x8.png`.
* King state `[0x1007058]`, initially 2:
  * 2 = KingBitmap (right)
  * 3 = KingLeft
  * 4 = KingSmile (never used in the small box)
  * 0 = blank: a 32x32 patch filled with table green, drawn inside the frame
* The drawer `sub_0100303d(hdc, state, draw)` BitBlts at (kingX, (ch−32)/3).

When the state changes (all verified live via `[0x1007058]`):

* **Mouse move over a top-row cell** (`sub_010051e7`): over a free cell the king looks **left** (3); over a home
  cell it looks **right** (2).
  * It is not based on the left/right half of the window.
  * Over the king, the gap or the tableau, the state is unchanged, so the king keeps its last direction
    (`s04_king_left`, `s05_king_right`, `kl_x4.png`, `kr_x4.png`).
* **Selecting a free-cell card** → left (0x1003837).
* **A move that ends in the top row** (`sub_01004db2` @0x1004eb1) → left if the destination is a free cell,
  right if it is a home cell.
* **New game** → state 2, set without drawing; the full repaint shows it.
* **Win** → state 0. The small box becomes empty but its frame stays. Then **KINGSMILE is StretchBlt'ed 10x to
  (10, ch+10), size 320x320**, or 256x192 if SM_CYSCREEN ≤ 350 (`sub_010034dd`).
  * Flag `[0x1008320] = 1` makes WM_PAINT keep redrawing the big king until the next game (`s16_win_dialog.png`,
    `s17_after_win_w.png`).
  * KINGSMILE's palette is green crown, blue, magenta, lime and yellow hair. This is genuine resource data.
  * Ratios: x = 0.1408 cw, y = ch + 0.1042 ch, size 4.507 cw (3.333 ch).
* The **YouWin** dialog uses its template position (172,85 DLU; it does not call the centring helper), so it sits
  to the right of the big king.
  * Other dialogs (GameNum, YouLose, Stats, Options) are centred on the main window by `sub_01002411`:
    x = window.left + (clientW − dlgClientW)/2,
    y = window.top + SM_CYCAPTION + SM_CYMENU + (clientH − dlgClientH)/2.

---

## 5. Painting and selection

* **WM_PAINT order** (`sub_01003590`):
  1. King frame, then the king.
  2. The 8 top-row slots (card or empty bitmap).
  3. Columns 1..8, each from top to bottom, cards fully drawn and overlapping.
  4. The big king, if won.
  5. After EndPaint, the "Cards Left" text is redrawn.
  * The wait cursor is shown during the paint.
  * There is no double buffering and no partial-invalidation logic. The whole board is drawn with clipping.
* **Selection = colour inversion of the whole card** (cdtDrawExt mode 2, mdHilite): every pixel becomes
  255 − RGB.
  * White → black, black → white, red → cyan (0,255,255), gray 128 → 127.
  * The transparent corner pixels are not inverted; they still show what is underneath.
  * Pixel-verified (`s03_selected.png`, `sel_x4.png`, `s18_freecell_selected.png`).
* Only **one card** is ever highlighted: the bottom card of the clicked column, or the clicked free-cell card.
  * Clicking anywhere on a column selects its bottom card.
  * Home cards and empty slots cannot be selected.
  * Multi-card (super) moves are worked out at drop time; the sequence is never highlighted.
* **Right button held on a buried card** (`sub_010033c1`) draws that card **fully, in place** (no offset) on top
  of the column.
  * Release (`sub_0100344c`) redraws the cards from that position downward, keeping the selection inversion.
  * It does nothing on the bottom card or the top row (`s06_rclick_hold`, `s07_rclick_release`).
* **Keyboard equivalent:** pressing the selected column's digit again starts timer 3 (**300 ms**). Each tick
  draws the next card of the column fully, top to bottom, with a wait cursor. At the end it deselects
  (`sub_010043c9`, `sub_01003f0d`).
* There is **no drag and drop.** Interaction is click to select, click on the target to move (or double-click to
  send to a free cell when that option is on).
  * Clicking on an inactive window only activates it: `WM_MOUSEACTIVATE` HTCLIENT sets a flag that swallows the
    next `WM_LBUTTONDOWN` (0x1001c3c, 0x1001dfe).
* **No deal animation.** A new game deals, calls InvalidateRect and repaints at once.

---

## 6. Cursors (`sub_010051e7`, verified live through an in-process `GetCursor()` probe)

The class cursor is NULL and `WM_SETCURSOR` is not handled. The app calls `SetCursor` on every `WM_MOUSEMOVE`.

| Situation | Cursor |
|---|---|
| No card selected | IDC_ARROW |
| Selected; over a column with cards that is a legal target (anywhere on its cards) | **custom `DownArrow`** (CURSOR res 9, 32x32 mono, hotspot (13,25): a down-pointing arrow with white fill and 2 px black outline; `res_DownArrow_x8.png`) |
| Selected; over an **empty column** (anywhere in its x-range, any y ≥ 106) | **IDC_UPARROW** |
| Selected; over an empty free cell, or a home cell that accepts the card | **IDC_UPARROW** |
| Selected; over an illegal target, the selected card itself, below a column's bottom card, or in a gap | IDC_ARROW |
| Busy (keyboard column reveal), during WM_PAINT, during move animation or undo | IDC_WAIT |

For column-to-column moves the DownArrow additionally requires that the number of cards to move does not exceed
the current supermove capacity.

---

## 7. Animation and Quick play

All in `sub_01004a57`.

* Start and end positions are the slot positions (`sub_01002e28`). The distance d = isqrt(dx² + dy²) uses a Newton
  iteration with tolerance about 3 (`sub_010046c3`).
* **Steps N = d / 37** (integer). Frames are drawn at i = 1..N−1 at (x0 + dx*i/N, y0 + dy*i/N), with truncating
  integer division. Then the card is drawn at its destination and its 12 corner pixels are restored.
  * A move shorter than 74 px has no intermediate frame.
  * 37 px = 0.521 cw per frame.
* **There is no delay or timer.** The exe imports no Sleep or GetTickCount, so the frames run as fast as GDI
  allows. On 2001 hardware a flight was visible but short; on modern hardware it is essentially instant.
  * Recommendation for the clone: keep N = d / (0.521 cw) and add a fixed frame time of about 10–15 ms
    (UNVERIFIED feel).
* **Quick play** (option `quick`, `[0x100712c]`) forces N = 1: no intermediate frames, the card just appears at
  its target.
* Flicker-free technique: save-under buffers plus a region difference, using three memory DCs on card-sized
  bitmaps created in WM_CREATE.
  * What is revealed under a lifted card:
    * Tableau source: the card above, drawn fully into the buffer at y = −step.
    * Free-cell source: the empty-cell bevel.
    * Home source (undo only): the same suit's previous rank, or the empty bevel for an Ace.
  * During flight the moving card's corners can show stale pixels. This is negligible.
* **Move sequencing:**
  1. A user move, plus every automatic move to home that follows, is first computed and logged (16-byte records
     at 0x10079c0).
  2. The board is reset to the pre-move snapshot (0x1007140).
  3. The moves are **replayed one card at a time, each with its own flight** (`sub_01004fc7` → `sub_01004db2`),
     with `UpdateWindow` before each.
  * A supermove is therefore shown as individual cards flying to free cells and empty columns, then the base
    card, then the cards flying back (`sub_0100530a`, `sub_01005397`).
  * Each card that lands on a home cell decrements Cards Left and redraws the text immediately.
  * Undo replays the logged batch backwards with the same animation (`sub_01003fcd`).
* **"One legal move left" warning:** timer 2 every **400 ms** calls FlashWindow 4 times (`sub_0100423c`,
  `sub_01003ed3`). With no legal moves left, the YouLose dialog appears.

---

## 8. "Cards Left: N" in the menu bar

* Drawn **directly into the non-client menu bar** with `GetWindowDC` + `TextOutW` (`sub_01002e86`).
  * It is not a menu item; the menu resource has only `&Game` and `&Help`.
  * Format: string 308 `"Cards Left: %u"`. It shows `Cards Left: 0` at startup (`s01_menubar_right_x4.png`)
    and `52` after a deal.
* **Font:** the system menu font (`NONCLIENTMETRICS.lfMenuFont` via SPI_GETNONCLIENTMETRICS, `sub_0100183d`).
* **Colours:** text COLOR_MENUTEXT on an opaque COLOR_MENU background.
* **Position, in window-DC coordinates:**
  * x = clientWidth − textWidth. The text's right edge is therefore cxFrame (4 px) left of the client's right
    edge, 8 px from the outer window edge.
  * y = SM_CYFRAME + SM_CYCAPTION + (SM_CYMENU − tmHeight)/2, computed once and cached.
  * Live under Wine: x = 563 for "52", 569 for "0"; y = 33.
* **When it is redrawn:** WM_SIZE, WM_MOVE, after every WM_PAINT, on each card arriving home, and on new game
  and undo. If the text became shorter, the old string is first overpainted in COLOR_MENU (the previous x is
  cached; WM_SIZE resets it to 30000).
* **For the clone:** right-align it in the menu bar, for example by owner-drawing or overpainting the menu bar.

---

## 9. Interaction facts that affect visuals

* Double-click with the `dblclick` option: the selected column card goes to the **leftmost empty free cell**
  (`sub_01003cfb`).
* Menu item states:
  * Undo (115) is grayed at every new selection and enabled after a move.
  * Restart (107) is grayed until the first deal.
  * Undo covers only the last batch: the user move plus its automoves.
* Quirk: clicking the board with a card selected, on a spot where the hit test fails, reuses the uninitialised
  column variable (= pixel y).
  * With y ≥ 9 this just deselects.
  * With y < 9 (top 8 px, outside the cells) it attempts a move to column y.
  * The clone can ignore this (UNVERIFIED as user-visible, from disassembly).

---

## 10. Scaling recipe for the re-implementation

Let s = cw/71 be the scale.

* Original virtual canvas: **632 x 427** (Wine client; real XP is about 632 x 426–434).
* All constants scale linearly:
  * cell = 71s x 96s
  * column gap g = floor((Wc − 8cw)/9)
  * x_k = g + (k−1)(Wc − g)/8
  * y0 = ch + 10s
  * step = 9ch/46 (the original floors this to 18 at s = 1, i.e. 0.1875 ch)
  * king 32s at ((Wc − 32s)/2, (ch − 32s)/3)
  * king frame 3s outside the bitmap
  * big king 320s at (10s, ch + 10s)
  * animation step 37s px
* Choosing cw from the window:
  * cw = Wc*71/632 reproduces the original width proportions exactly.
  * Using s = min(Wc/632, Hc/427) keeps the original aspect. Then either anchor top-left (faithful) or centre the
    632s-wide board.
* At 1920x1080 (client about 1904 x 1030) the height limit gives s ≈ 2.41, so the card is about 171 x 231.

---

## Shot index (`research/layout/shots/`)

| File | Shows |
|---|---|
| `s01_launch(_w).png` | default window right after launch |
| `s02_game1(_w).png` | game #1 at default size |
| `annotated_layout_x2.png` | annotated geometry diagram |
| `s03_selected.png`, `s18_freecell_selected.png` | inverted selection |
| `s04_king_left`, `s05_king_right` (+ `kl_x4`, `kr_x4`) | king direction |
| `s06_rclick_hold`, `s07_rclick_release` | right-click reveal |
| `s08_freecell`, `s09_home(_w)` | a card in a free cell, then in home cells |
| `s10_size1600x1000`, `s11_size500x350`, `s12_maximized*` | resizing |
| `s13_poked` | empty column, cursor test board |
| `s14/s15_longcol` | a 19-card column with no compression |
| `s16_win_dialog`, `s17_after_win_w` | big king |
| `res_*` | resource bitmaps and cursor |
| `card_corner_x10`, `sel_x4`, `king_crop_x8` | pixel crops |

Some captures used a poked board state (`s13`–`s15`), so cards are duplicated there.

## Notes

* Under Wine the caption area of `_w` captures is black, because the Mac driver draws the title bar natively.
  Menu-bar metrics are Wine's (cymenu 19).
* To avoid touching the shared prefix's statistics, I set `[0x10074e0]` to the current game number before
  resigning or winning; this is the code's "already counted" guard.
* Other agents also run FreeCell windows in the same wineserver. My helper finds the window with
  `FindWindow("FreeWClass")`, not by PID.
  * Another agent's instance (`research/rules/game/freecell.exe`) started at about 09:28. My last step ran at
    09:28:50: poke `[0x10074e0]`, select game 1, move 3♦ to free cell 0 and select it, producing `s18`.
  * That step **may** have hit the other agent's window. The result is identical either way, and my own
    instance closed correctly on the following `close`.
  * Future runs should filter by PID.
* My FreeCell instance was closed with WM_CLOSE (resign answered) and has exited.
