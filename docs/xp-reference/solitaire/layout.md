# XP Solitaire: geometry, rendering and interaction visuals (ground truth)

Source: `sol.exe` (XP RTM, 56,832 bytes, PE timestamp 0x3B7D8480, image base 0x01000000, no public symbols)
plus the XP `cards.dll` (71 x 96 cards). Companion documents: `rules.md` (game logic, scoring, clock) and
`resources.md` (strings, dialogs, bitmaps). Message names below follow `rules.md` §0.4.

I used the same two methods as for FreeCell (`../layout.md`):

* **(B) Disassembly.** capstone over the whole `.text`, segmented at every call target
  (`<sr>/visuals/sol.asm`, generator `<sr>/visuals/disasm.py`).
* **(A) Live observation under Wine** (Wine Devel 11.x, own prefix, Windows version XP) with a research
  copy of the repo's e2e driver (`<sr>/visuals/drv/`). On top of `tools/wine` it adds `peek`/`poke`
  (ReadProcessMemory/WriteProcessMemory), `solcols` (dumps the 13 piles: rect, cards, positions),
  `solset`/`solcount` (build arbitrary boards), `dragto` (mouse moves with the button held), `post`,
  `capseq` (capture sequences) and **`gdilog`**: the hook DLL patches sol.exe's IAT entries for
  `cdtDraw`, `cdtDrawExt`, `BitBlt`, `PatBlt`, `MoveToEx`, `LineTo`, `SetROP2`, `SetPixel`,
  `InvertRect`, `FrameRect` and logs every call with a microsecond timestamp. All drawing claims marked
  **[Wine-verified]** come from these call logs and/or pixel captures.

`<sr>` = `/private/tmp/claude-502/-Users-erko-IdeaProjects-xp-cards/4b6e3af5-e647-41ff-a37b-eccdd0578cdc/scratchpad/sol-research`.
Captures are in `<sr>/shots/` (index at the end). Units are XP pixels. Card **cw x ch = 71 x 96**
(`cdtInit` writes `[0x1007308]` = cw and `[0x100730C]` = ch at 0x1001B5D).

> **Wine caveat for every future Solitaire e2e run:** Wine ships a builtin `cards.dll` (blue cross-hatch back,
> different art). It is used even when the XP `cards.dll` sits next to `sol.exe`. Set
> `WINEDLLOVERRIDES="cards=n"`, or captures do not show XP's cards. (`tools/wine/run.sh` does not set it.)

---

## 0. Key takeaways for the clone

* Table **RGB(0,128,0) = #008000** (FreeCell's is #007F00). Default client **585 x 384** = (7cw + 8g) x 4ch with g = 11.
* **XP Solitaire re-lays out on resize, horizontally only:** gap g = max(cw/8 + 3, (W − 7cw)/8). Columns spread
  evenly across the width. Every y coordinate is fixed, cards are never scaled, and tall columns are never compressed.
* Steps: face-down **3 px** (ch/25), face-up **15 px** (4ch/25). Draw-three waste fan **+14 px x, +1 px y** per card.
  Piles of the stock, waste and foundations get a **3-D edge of (+2,+1) per 10 cards** (stock, waste) or
  **per 4 cards** (foundations).
* Empty stock shows cards.dll's **green O** (recycle allowed) or **red X** (Vegas, no passes left). Empty
  foundation shows the **dotted ghost**. Empty waste and empty tableau columns show **nothing**.
* There is no animation anywhere except two: the **zip-back** of an illegal drop (LineDDA, no delay, so it
  is instant on modern hardware) and the **win cascade**. The deal, turning a card over, stock clicks,
  double-click and right-click autoplay all draw instantly.
* Dragging moves a **screen-grabbed image** of the whole stack with flicker-free double save-under blits. The
  "Outline dragging" option draws R2_NOT rectangles instead and inverts the hovered target card.
* Win cascade: **exact integer physics** in §8. It is verified against all 52 logged trajectories of two
  wins (8,228 and 9,416 frames), with 100% match.
* The status bar is a white custom child window ("Stat") 18 px tall, of which 17 px are visible:
  "Score: N Time: N" (MS Shell Dlg 9 pt **bold**), red when negative.
* **No animated backs in XP** (cdtAnimate is a stub). The 250-ms tick still pokes the stock, the hook the
  16-bit versions used for animation.

---

## 1. Window

| Item | Value | Evidence |
|---|---|---|
| Class | `"Solitaire"`. Style `CS_DBLCLKS \| CS_BYTEALIGNWINDOW` (0x2008), so there is **no CS_HREDRAW/VREDRAW**. Cursor IDC_ARROW (the class cursor, never changed). Background = the table brush. Menu resource 1. Icon 500 (+ a 16x16 small icon) | RegisterClassExW @0x1001D3A |
| Table brush | `CreateSolidBrush(mono ? 0xFFFFFF : 0x008000)` = **RGB(0,128,0)**. White on a 2-colour display (`GetDeviceCaps(NUMCOLORS)==2` → `[0x1007168]`) | 0x1001C01–0x1001C1A; pixel (0,128,0) **[Wine-verified]** |
| Style | `WS_OVERLAPPEDWINDOW \| WS_CLIPCHILDREN` (0x02CF0000), ex-style 0. A `/I` on the command line adds WS_MINIMIZE | CreateWindowExW @0x1001DD8 |
| Position | `CW_USEDEFAULT`. **Neither position nor size is ever saved.** The registry holds only `Options` and `Back` | same; `rules.md` §10 |
| Default size | `AdjustWindowRect({0,0, 7cw+8g, 4ch}, WS_OVERLAPPEDWINDOW, menu)` with g = cw/8+3 = 11, so the **client is 585 x 384**. The outer height is clamped to the screen height (VERTRES) | 0x1001D49–0x1001DA6 |
| Measured | Wine: outer 593 x 437, client 585 x 384 (Wine menu 19 px). XP Luna is expected to give 593 x 438 (UNVERIFIED). The width is the same everywhere | `v01_launch(_w).png` **[Wine-verified]** |
| Min/max size | No WM_GETMINMAXINFO handling: the system defaults apply. Any size works, down to a tiny window (`v05_300x250.png`), and maximize works (`v06_maximized.png`, 1800 x 1079) | wndproc 0x10016BD **[Wine-verified]** |
| Title | Always `"Solitaire"` (string 100). There are no game numbers | **[Wine-verified]** |
| Start-up | After ShowWindow/UpdateWindow it posts WM_COMMAND 1000 (Deal), unless it is started minimized (show cmd 6/7). It also starts `SetTimer(hwnd, 666, 250 ms, TimerProc 0x100142B)` | 0x1001E05, 0x1001E7A |
| Tiny screens | If VERTRES < 300 (`[0x100718C]`=1): ch is halved (0x1001BEA) and the face-up step drops by 1. This is irrelevant for XP and HD | |

### 1.1 Resizing behaviour (WM_SIZE @0x1001730)

```
g' = (LOWORD(lParam) - 7*cw) / 8                // signed, truncating
gmin = cw/8 + 3                                 // 11
if (g' < gmin) { if (g == gmin) goto skip;  g' = gmin; }
g = g'  ([0x1007338]);  RelayoutColumns() (0x10040FE);  InvalidateRect(hwnd, NULL, TRUE)
skip: reposition the status bar (0x1005D94)
```

* **Only x changes.** RelayoutColumns makes every card's x relative to its pile (`x -= pile.left`), recomputes
  the 13 pile rects from the new g, and adds the new left back. Every y (pile tops and card y) stays as dealt.
* Each accepted WM_SIZE (whenever g ≥ 11, including height-only changes) **invalidates and erases the whole
  client**, so the board flickers while being dragged. Below 585 px width g stays 11 and the board is
  clipped on the right.
* Live gaps **[Wine-verified]** (`solcols` after `resize_client`):

| Client W | g | Tableau x (col 1..7) | Foundations x |
|---|---|---|---|
| 300, 500, 592 | 11 | 11, 93, 175, 257, 339, 421, 503 | 257, 339, 421, 503 |
| 585 (default) | 11 | same | same |
| 593, 600 | 12 | 12, 95, 178, 261, 344, 427, 510 | 261 ... 510 |
| 640 | 17 | 17, 105, ..., 545 | 281 ... 545 |
| 800 | 37 | 37, 145, ..., 685 | 361 ... 685 |
| 1024 | 65 | 65, 201, ..., 881 | 473 ... 881 |
| 1800 (maximized) | 162 | 162, 395, ..., 1560 | 861 ... 1560 |

* With the default size there is no right-margin asymmetry: 585 − (503 + 71) = 11 = g.

---

## 2. Board geometry

### 2.1 Pile rectangles (RelayoutColumns 0x10040FE; pile pointers at game+0x6C, rect at pile+8..+0x14)

top = `MulDiv(ch, 5, 100)` = **5** (0x1004153). k = 0..6 are the tableau column indexes.

| Pile (index) | left | top | right | bottom | 1x value |
|---|---|---|---|---|---|
| Stock (0) | g | 5 | g + cw + 10 | ch + 10 | (11,5)-(92,106) |
| Waste (1) | 2g + cw | 5 | left + 7cw/5 + 4 | ch + 10 | (93,5)-(196,106) |
| Foundation j (2..5) | g + (3+j)(cw+g) | 5 | left + cw + 6 | ch + 10 | (257,5)-(334,106), (339..), (421..), (503..580,106) |
| Tableau k (6..12) | g + k(cw+g) | ch + 11 = **107** | left + cw | top + ch + 6·dyDn + 12·dyUp = top + 294 | (11,107)-(82,401) ... |

* The **foundations sit exactly above tableau columns 4–7**. The waste is the second column position and has
  no tableau column of its own above it.
* The extra widths cover the pile's 3-D edges: stock and waste +10 (up to 5 layers of +2), foundations +6
  (3 layers of +2).
* The tableau rect height 294 leaves room for 6 face-down and 13 face-up cards. It matters for hit-testing
  an empty column (§6.3) and for the erase below the last card.
* Annotated against the default capture **[Wine-verified]** (`v01_launch.png`): stock top card at (15,7)
  (24 cards = three layers), foundation ghosts at x = 257/339/421/503 y = 5, tableau tops at y = 107, and
  column 7's face-up card at y = 125 (6 × 3 px below the first face-down card).

### 2.2 Card positions inside a pile (ComputeCardPositions 0x1003AEA)

Each pile class has (dxUp, dyUp, dxDn, dyDn, nUp, nDn). It is created by `PcolclsCreate` 0x100322F from the
game init at 0x10055EB. Starting at the pile's (left, top), each card is placed and the running offset then
advances. For a face-up card it advances by (dxUp, dyUp) when `i % nUp == nUp−1`. For a face-down card,
or when the function is called with fForceDown, it advances by (dxDn, dyDn) when `i % nDn == nDn−1`.

| Pile | dxUp | dyUp | dxDn | dyDn | nUp | nDn | Result |
|---|---|---|---|---|---|---|---|
| Stock | 0 | 0 | 2 | 1 | 1 | **10** | all face down: +2,+1 after every 10th card. 24 cards at (11,5)×10, (13,6)×10, (15,7)×4; a full deck has 6 layers up to (21,10) |
| Waste | **cw/5 = 14** | **1** | 2 | 1 | 1 | 10 | see §3 (fan of the last three) |
| Foundation | 2 | 1 | 0 | 0 | **4** | 1 | +2,+1 after cards 4, 8, 12: A–4 at (x,y), 5–8 at (x+2,y+1), 9–Q at (x+4,y+2), **K at (x+6,y+3)** |
| Tableau | 0 | **4ch/25 = 15** | 0 | **ch/25 = 3** | 1 | 1 | face-down 3 px, face-up 15 px |

All rows are **[Wine-verified]** by `solcols` (for example foundation K♠ landing at (509,8), stock (15,7) on top).
In ratios: face-down step 0.03125 ch, face-up 0.15625 ch, waste fan 0.197 cw, top margin 0.052 ch, and
tableau top = ch + 11 (1.115 ch).

### 2.3 No compression of tall columns

ComputeCardPositions has no height test, and nothing reads the client height. A column with 6 face-down and
13 face-up cards ends at 107 + 18 + 180 + 96 = **401**. That is below the default 384-px client and runs
behind the status bar, which starts at 367. It is simply clipped (`v20_poked.png`, column 1) **[Wine-verified]**.
The only remedy in XP is to make the window taller.

### 2.4 Painting

* Paint (game msg 7, 0x1002C49) sends Paint (pile msg 19) to all 13 piles, back to front, **only if
  game+0x24 (`fDealt`) ≠ 0**. Before the first deal, and after a win, a repaint therefore shows **only the
  table colour** (§8.6).
* Render (pile msg 18, 0x100383E) draws cards with `cdtDrawExt(hdc, x, y, 71, 96, cd, md, 0x008000)`:
  md 0 for a face-up card, md 1 for a back (`cd` = back id 54–65). The card id is rank·4+suit.
  * Corners come out **rounded**: cards.dll saves and restores 3 background pixels per corner, as in FreeCell.
  * Cards completely hidden under the same position (identical x,y, face down) are skipped.
  * Afterwards the part of the pile rect not covered by cards is PatBlt'ed with the table brush
    (0x10032CA). That is how 3-D edges, a shrunk waste fan, etc. are erased.
* Stock top card fix-up (StockRender 0x10046DF): after drawing the stock's top card, the 3 pixels of its
  bottom-right corner, (x+70,y+95), (x+69,y+95) and (x+70,y+94), are SetPixel'ed to the table colour
  **[Wine-verified]** (`gdilog`). Without that fix-up the layer under it would show in the rounded corner.
* There is no double buffering. Exposed areas are erased by the class brush and the piles are re-rendered.

---

## 3. Stock and waste visuals [Wine-verified unless noted]

* **Click on the stock** (draw three is the default) draws the 3 new waste cards at once
  (`cdtDrawExt` × 3 within 1 ms), PatBlts the uncovered strips, then redraws the stock top. There is no
  movement animation (`stock_click1.txt` log, `stock/s01.png`).
* **Waste fan** (WasteMove 0x10048D2):
  1. The waste's existing cards from index n−3 on are re-positioned as face-down, so they collapse onto the
     3-D pile.
  2. The new cards are appended with the special flag `[0x1007190]`, which places the first new card *on*
     the previous top, with no offset.
  3. Then (+14,+1) is added per card.
  * Result: the top three cards at (x0, y0), (x0+14, y0+1), (x0+28, y0+2), where (x0,y0) is the 3-D pile top.
  * Measured: 3 cards at (93,5), (107,6), (121,7). With all 24 cards in the waste: (97,7), (111,8), (125,9).
  * The waste rect width 103 = cw + 28 + 4 covers exactly this.
* Draw one uses the same code with one card per click, so there is no fan. UNVERIFIED live; it follows from the code.
* **Empty stock** draws `cdtDrawExt(..., cd 0, md 7)` = cards.dll bitmap 68: a bright green **O**, RGB(0,255,0),
  bbox x 7–63, y 18–78 inside the card, ring about 6 px thick, on (0,128,0) with a 1-px black outline and cut
  corners.
  * In **Vegas with no passes left** (passes == max − 1, `[0x100702C]`) it uses md 6 = bitmap 67, a red
    **X**, RGB(255,0,0), bbox 7–64 x 19–77 (`stock/s08.png`, `stock/vegas_x.png`, `sheets/vegas_x_marker_status_x2.png`).
* Recycling (click on the O) redraws the stock (back at (11,5), (13,6), (15,7)) and PatBlts the whole waste
  rect green. No animation.
* **Empty foundation** draws md 3 (mdGhost): a table-colour fill AND bitmap 53. That gives a black 1-px
  outline with cut corners plus a grid of black dots (`sheets/foundation_ghost_x8.png`). **Empty tableau and
  empty waste** are PatBlt with the table colour, so nothing is visible (`natwin/n00_board.png`).
* The stock's 3-D edge: each layer shows a 2-px strip of the card below on the left and top
  (`sheets/stock_corner_x8.png`).

---

## 4. Status bar [code + Wine-verified]

(`resources.md` §4.2 has the strings. This section covers geometry and behaviour.)

* **Window:** class `"Stat"` (RegisterClassW @0x1005F6E: WHITE_BRUSH background, IDC_ARROW, wndproc
  0x1005EBF, which only handles WM_PAINT), style `WS_CHILD | WS_BORDER`.
  * Created at 0x1005C79 only if the option *Status bar* is on (`[0x1007020]`, default on).
  * Geometry (0x1005D94 on every WM_SIZE/WM_MOVE): `MoveWindow(-1, H − h + 1, W + 2, h)` with
    **h = tmHeight(system font) + 2 = 18** on XP and Wine at 96 DPI.
  * The left, right and bottom borders fall outside the parent client. Visible: a **1-px top border at
    y = H − 17** (367 at default size) and a **white 16-px strip** (y 368–383).
  * Border colour: WS_BORDER uses COLOR_WINDOWFRAME, black on XP (UNVERIFIED). Wine draws RGB(158,158,158).
  * Because of WS_CLIPCHILDREN, **cards drawn over that area (long columns, drags, the win cascade) go
    behind the status bar.**
* **Right part** (DrawStatus 0x1005203, game msg 16):
  * Font `CreateFontW(-MulDiv(9, LOGPIXELSY, 72), …, FW_BOLD, DEFAULT_CHARSET, "MS Shell Dlg")`, so
    −12 px at 96 DPI. It is **bold** (Wine reports tmHeight 14, weight 700).
  * Laid out right to left with `DrawTextW(DT_RIGHT|DT_SINGLELINE|DT_NOCLIP)` into (0, 0, statusW − 4, 16).
    The text is top-aligned, with no vertical centring.
  * Order, right to left:
    1. `"Time: <s>"`, only if *Timed game* is on.
    2. The score number + `" "`. Red (RGB 255,0,0) if negative, else black. Vegas adds the currency
       (`-$52`).
    3. `"Score: "`. There is no score text at all for scoring None.
    4. A white PatBlt 4·tmMaxCharWidth wide to the left, which wipes longer old text.
  * Measured default: glyphs on rows 371–379, last pixel x = 578 (4 px + bearing from the right edge)
    (`sheets/status_bar_x6.png`). The Vegas sample shows `-$52` red at x 503–530 (`stock/vegas_start.png`).
* **Left part** (0x1005DED): the whole status client is PatBlt'ed white, then `TextOutW(4, 0, text)` in the
  DC's **default system font** (bold, 16 px). It is used for:
  * menu help on WM_MENUSELECT, where the string id = the command id (`"Deal a new game"` captured,
    `act/status_menuselect.png`). It is cleared for popups and on menu close (0x1FFF → empty);
  * the win message (§8).
* **Refresh cadence:** the 250-ms timer (game msg 17, 0x1005189) runs only when timed, dealt, started and not
  minimized. It increments the tick, then redraws the right part **on every tick**. The test
  `~(tick & 3)` at 0x10051F0 is never zero, a compiler quirk, so the status is repainted every 250 ms and
  not once per second. The log shows the 60 x 16 erase every 250 ms **[Wine-verified]**.
  * Displayed seconds = tick >> 2. The counter saturates at 0x7FFE, so the clock shows at most 8191 s.
* With the status bar off nothing is reserved. The layout never subtracts the status bar height anywhere.

---

## 5. Turning, dealing, clicks: all instant [Wine-verified by gdilog]

| Action | What is drawn | Timing |
|---|---|---|
| Deal (F2 / start-up) | (1) The whole client is PatBlt'ed with the table colour (0x10021AA). (2) The full stock is drawn as 6 layers (11,5)…(21,10), then 4 foundation ghosts. (3) The tableau is dealt **row by row, left to right**: row r gives column r its face-up card and columns r+1..7 a face-down card. (4) The stock is redrawn smaller after each row | all 41 cdtDrawExt calls within ≈4 ms: **no deal animation** |
| Click on a face-down top tableau card | `cdtDrawExt(md 0)` in place (TableauHitTest 0x10042EF turns it over) | instant, no flip animation |
| Click on the stock | §3 | instant |
| Double-click a top card that can go home | the source is re-rendered and the card is drawn on the foundation | instant (no flight) |
| Right-click (Autoplay, 0x1002A45) | each card that goes home is drawn on its foundation and its source column re-rendered, one after another | ≈0.5 ms apart: **no visible sequence** |
| Deck change (Deck dialog) | `InvalidateRect(NULL, TRUE)` (0x1001444) | full repaint |

* There is no selection highlight in mouse play: XP Solitaire never inverts a card (unlike FreeCell).
  InvertRect is used only for the outline-drag target (§6.2).
* **Keyboard play** moves the real mouse pointer with `SetCursorPos` (KeyHit 0x1002EB8, `rules.md` §9.1). I did
  not test it live, because it moves the host's cursor under Wine.

---

## 6. Drag and drop

Mouse plumbing:

* **WM_LBUTTONDOWN** → `SetCapture(hwnd)` + MouseDown (game msg 3) with the client point.
* **WM_MOUSEMOVE** → MouseMove (msg 5), only while game+0x4C (button down) is set.
* **WM_LBUTTONUP** → `ReleaseCapture` + MouseUp (msg 4).
* **WM_KILLFOCUS** during a drag sends MouseUp, which cancels the drag.
* The code never uses GetCursorPos. Posted mouse messages drive it completely, which is why the Wine
  driver can drag with `ldown` / `dragto` / `lup`.

### 6.1 Normal ("full") dragging [Wine-verified, `drag1_*.txt` logs, `sheets/drag_full.png`]

* **Button down on a face-up card (pile msg 6 Select):**
  1. `BitBlt(memImg, 0,0, cw, hStack, screen, x, y)` copies the **on-screen pixels** of the picked-up stack
     into the drag image. hStack = (n−1)·15 + 96.
  2. The "save-under" of the origin is pre-rendered without those cards: a table-colour PatBlt of
     cw × (pile height), the card above them redrawn at its relative position (for example the face-down
     card at y = −3), and a table-colour PatBlt below it.
  * Nothing changes on screen at button down.
* **Each mouse move (pile msg 20 Drag, 0x1003976):** five SRCCOPY BitBlts with two swapping save buffers.
  1. Save the screen under the new rectangle.
  2. Patch in the overlapping part of the old save.
  3. Patch the overlapping part of the card image into the old save.
  4. Blit the card image at the new position.
  5. Restore the old position.
  * The stack follows the mouse **with the grab offset preserved**: mouse (540,160) on a card at
    (503,125) moved to (505,195) puts the card at (468,160).
  * There is no lift offset, no shadow and no cursor change (IDC_ARROW throughout).
  * Under the dragged stack the origin column immediately shows the uncovered face-down card in full.
* **Artefact to reproduce or knowingly drop:** because the image is a screen grab, the dragged cards'
  rounded corners carry the pixels that were under them at pick-up.
  * Example: the blue edge of the face-down card underneath shows at the top-left corner of a dragged 6♦
    (`sheets/drag_corner_x10.png`). This is not table green.
  * Inner corners between stacked cards show the card below as usual.
* **Drop target (ValidMovePt 0x1003BCB, scanned by MouseMove/MouseUp over piles 0..12 in order):**
  * The dragged *top* card's rect (mouse − grab offset, cw x ch) must **intersect** the target's *top card
    rect*, or the **whole pile rect** if the pile is empty. An empty tableau column is therefore a target
    over its entire 71 x 294 area.
  * The pile must also accept the move (CanDrop).
  * **The first pile in index order wins:** stock, waste, foundations 1–4, tableau 1–7.
* **Valid drop:** the cards are moved and both piles re-rendered at once. No flight.
* **Invalid drop → zip back (pile msg 28 AnimateBack, 0x1003ECE):**
  * `LineDDA(from = current drag position, to = origin)`. The callback (0x1003E6C) redraws the drag image
    on every **36th** DDA point (counter > 0x23).
  * The DDA steps 1 px along the major axis, so the image moves ≈36 px per step. Measured from (343,275)
    back to (503,125): (378,242), (414,208), (450,175), (486,141), then the origin.
  * There is **no delay**: the 5 steps took 0.2 ms under Wine. On 2001 hardware it was a brief visible slide.
  * Then the source pile is re-rendered and the area below its last card erased.
  * Clone recommendation: a short slide at about 36 px per frame (0.5 cw) with a 10–15 ms frame time keeps
    the feel (UNVERIFIED feel).

### 6.2 "Outline dragging" option (`[0x1007188]`, default off) [Wine-verified, `ol_*.txt`, `sheets/drag_outline.png`]

* Button down draws nothing. The real cards **stay drawn at the origin** during the whole drag.
* On each move, `SetROP2(R2_NOT)` + MoveToEx/LineTo (0x1001FA6) draws an outline at the new position,
  **then** erases the old one (XOR, same call).
  * The outline is the rectangle x..x+71, y..y+(n−1)·15+96 (LineTo excludes the end point, so it is 1 px
    larger than the cards on the right and bottom).
  * It also has **one horizontal line per further card** at y + 15·i, from x to x+71.
  * Then SetROP2(R2_COPYPEN). The pen is irrelevant: R2_NOT inverts the destination, so on green it shows
    **(255,127,255)**.
* **Target highlight (pile msg 22 Hilight, 0x1003D51; outline mode only):** while hovering a valid target,
  its top card (or empty-pile slot) is `InvertRect`'ed. The previous target is inverted back first.
* An invalid drop zips the outline back along the same LineDDA path.

---

## 7. Corners, colours and draw modes summary

| Thing | Pixels |
|---|---|
| Table | (0,128,0) everywhere, including the inside of the O/X and ghost bitmaps |
| Card corners (normal draw) | 3 px per corner restored by cards.dll: the table, or the card beneath |
| Card corners (win cascade, md \| 0x80000000) | **not restored**: the bitmap's own corner pixels show, which are **white**. For example (209,3) = W,W,K / W,K,W **[Wine-verified]** |
| Dragged cards | corners = screen pixels at pick-up (§6.1) |
| Outline drag | R2_NOT lines: (255,127,255) on green, black on white card areas |
| Status bar | white; border COLOR_WINDOWFRAME; text black, negative score (255,0,0) |

cards.dll draw modes used: 0 face, 1 back, 3 ghost, 6 X, 7 O, plus flag 0x80000000 in the cascade.

---

## 8. The win animation ("cascade") — KlondWinner 0x1004DF0 [code + Wine-verified]

### 8.1 Trigger and set-up

* IsWinner (0x1004D1E: all four foundations hold 13) is checked after each successful drop, double-click
  home and autoplayed card.
* The cascade starts **in the same handler, ≈1.5 ms after the last card is drawn on its foundation**. There
  is no pause and no "You win" text.
* The cheat Alt+Shift+2 (WM_COMMAND 1010, ForceWin) fills the foundations logically, **without repainting**,
  and runs the same code.
  * The cascade then plays over the untouched board (`sheets/win_forced_frames.png`). The four "home" cards
    start at the foundation origin (x,y), not at the 3-D offset.
* Set-up:
  * score event Win (Standard time bonus);
  * fDealt = 0 (game+0x24, which disables painting, §2.4) and game+0x2C = 1;
  * the status left text: Standard `"Bonus: <n>  Press Esc or a mouse button to stop..."`, otherwise only
    `"Press Esc or a mouse button to stop..."`;
  * `[0x10071CC]` = 1, which makes the card helper 0x1002082 pass `md | 0x80000000`;
  * one `GetDC` for the whole cascade;
  * `GetClientRect`, read **once**: W = right, yMax = bottom − ch. The bottom **includes the status bar
    area**, so cards sink about 17 px behind the bar when they bounce.

### 8.2 Physics (exact; integer C semantics, `/` truncates toward zero)

```c
for (r = 12; r >= 0; r--)                    // K, Q, ..., A        (pile offset 0xB4 down to 0x24)
  for (f = 0; f < 4; f++) {                  // foundations left to right (piles 2..5)
    vx = rand() % 110 - 65;  if (abs(vx) < 15) vx = -20;     // -65..44, never -14..14
    vy = rand() % 110 - 75;                                  // -75..34 (negative = up)
    x = card[f][r].x;  y = card[f][r].y;     // where it lies: K at (+6,+3), 9-Q (+4,+2), 5-8 (+2,+1)
    while (x > -cw && x < W) {               // until fully off the left or right edge
      cdtDrawExt(hdc, x, y, cw, ch, card, 0 | 0x80000000, 0x008000);   // never erased -> trail
      x += vx / 10;   y += vy / 10;   vy += 3;                 // gravity 0.3 px/frame^2
      if (y > H - ch && vy > 0) vy = -vy * 8 / 10;             // bounce, keeps 80 %
      if (AbortPending()) goto done;                           // also the per-frame wait
    }
  }
```

* **Verified:** brute-forcing (vx, vy) per card reproduces **every logged position of all 52 cards** in a
  forced win (8,228 frames, W 585 x H 384) and in a natural win (9,416 frames): `<sr>/visuals/winphys.py`.
  * vy is always determined exactly. vx is determined up to its tens bucket, because only vx/10 matters.
* Horizontal speed distribution (px/frame, out of 110): −6: 6, −5: 10, −4: 10, −3: 10, **−2: 39** (29 of
  them are the |vx|<15 remap), −1: 5, +1: 5, +2: 10, +3: 10, +4: 5.
  * **73% of cards fly left.** The slowest crawl 1 px/frame and need up to ~500 frames to leave.
* Vertical: the first step is −7..+3 px. The bounce keeps 80%. Once |vy| < 10 the card "rolls" along the
  bottom with 0/±1-px steps, still leaving a solid band. That is the origin of the black arches made of
  stacked left edges.
* There is no rotation, no scaling, no fade, and no second card in flight: **one card at a time**. Each
  card's flight ends before the next is launched.
* Length (Monte Carlo of the model, 300 runs):

| Client | Mean frames | p10–p90 |
|---|---|---|
| 585 x 384 | 8,560 | 7,720–9,460 |
| 800 x 600 | 11,260 | 10,010–12,400 |
| 1024 x 768 | 14,130 | 12,500–15,680 |
| 1800 x 1079 | 24,320 | 21,880–27,160 |

### 8.3 Frame timing

* AbortPending (0x1004D43) = `MsgWaitForMultipleObjects(0, NULL, FALSE, 5, QS_ALLINPUT)`. If input is
  pending, it calls `PeekMessage(PM_NOREMOVE)` for the main window.
  * One of these **aborts and is left in the queue**: WM_KEYDOWN (any key, not only Esc), WM_SYSKEYDOWN,
    WM_LBUTTONDOWN/RBUTTONDOWN/MBUTTONDOWN, WM_NCLBUTTONDOWN/NCRBUTTONDOWN/NCMBUTTONDOWN or WM_MENUSELECT.
  * Any other message is removed, translated and dispatched. Timers and paints keep running; a WM_PAINT
    erases the trails in its region and draws nothing, because fDealt = 0.
  * The peek filters on the main window, so a click on the status bar does *not* abort.
* So **one frame ≈ max(5 ms, timer resolution) + draw time**.
  * Under Wine: 5.4–8 ms, mean ≈5.8 ms (≈170 fps); a cascade ≈ 50 s. 333 frames took 2.13 s in the abort test.
  * On real XP a 5-ms timeout normally rounds up to the clock tick: 15.6 ms (≈64 fps, cascade ≈2.2 min) or
    10 ms, or ≈5–6 ms if any program has called `timeBeginPeriod(1)`. **UNVERIFIED on hardware.**
  * Recommendation for the clone: use the same call, `MsgWaitForMultipleObjects` with 5 ms and the same
    abort set. Then the speed matches the original on the same XP machine.
* Abort [Wine-verified]: a posted WM_KEYDOWN('A') after 2 s stopped the cascade immediately.

### 8.4 Rendering details

* Cards are drawn with **square white corners** (no corner restore) and are **never erased**, so the
  trail is the union of every frame.
* Drawing uses the main window DC, which is clipped by WS_CLIPCHILDREN, so the status bar stays on top.
* The foundation a card left still shows it until the next card of that pile is drawn at the same spot,
  because nothing is erased.
* Frames: `sheets/win_natural_frames.png` (6 real captures of a natural win: before, +1 s, +2 s, +6.5 s,
  +9.5 s, +12.5 s), `natwin/mseq_*.png` and `win/wseq_*.png` (40 forced-win captures, 300 ms apart).
* `natwin/win_replay.gif` is a **frame-exact replay** of the logged natural-win draw calls (every 8th
  frame, 40 ms per GIF frame ≈ Wine pace). It is rendered from the XP bitmaps by `<sr>/visuals/replay.py`.
  * Against the real captures it matches 98–98.5% of pixels at the best-aligned frame; the remainder comes
    from 5-frame sampling and the base image.

### 8.5 End

1. ReleaseDC.
2. Clear the status left text.
3. **PatBlt the whole client with the table colour.** All trails and all cards vanish.
4. DefGmProc Winner (0x10030C8): `MessageBoxW(hwnd, "Deal Again?", "Solitaire", MB_YESNO |
   MB_ICONEXCLAMATION)` (`win/w99_dialog.png`).
   * **Yes** posts WM_COMMAND 1000, a new deal (verified: deal drawn).
   * **No** leaves an **empty green table**. Even a full repaint (resize) draws nothing, because fDealt = 0
     (`natwin/n99_after_no_resized.png`) **[Wine-verified]**. The status keeps "Score: N Time: N". The next
     action is Deal.
* The message box is centred by Windows (default MessageBox placement), not by the app.

### 8.6 Scaling the cascade for HD

* Simulate in XP units on a virtual canvas of (W/s) x (H/s), with s = cw/71, and draw at (s·x, s·y). This
  keeps the exact trail spacing, bounce count and duration *relative to the board*.
* Scaling the velocities instead changes nothing visible except subpixel smoothness.
* Do **not** simulate in device pixels at 1x physics. On a maximized 1080p board the cascade would last
  3–4x longer and look sparse.
* Draw each frame's card into a persistent back buffer and present only the dirty rect. The trail is
  free; nothing is ever erased until the end.

---

## 9. Animated card backs during play

* **None in XP.** `cdtAnimate` is a stub that returns TRUE, and sol.exe does not import it
  (`resources.md` §10.4).
* The hook is still alive. Every 250-ms tick (TimerProc 0x100142B → KlondTimer 0x1005189) sends pile
  message 27 with the tick count to **pile 0 (the stock)**, but only if all of these hold:
  * Timed game is on,
  * the game is dealt and the clock started,
  * the window is not minimized,
  * no drag/selection is active (game+0x58 == −1).
  * The stock handler (0x1004823) returns TRUE and draws nothing.
* So in 16-bit Solitaire the stock's top back was animated at **4 fps (250 ms)**, frame = tick count. This
  is UNVERIFIED historically, but the plumbing matches.
* The leftover sprite frames 678–684 show the four classic animated backs: card hand ("ace up the sleeve"),
  castle bats, beach sun with sunglasses, and robot dial/lamps (`sheets/cardsdll_backs_and_specials.png`).

---

## 10. Scaling recipe for Solitaire HD

Let s = cw/71 (all constants are XP pixels × s):

* g = max(floor(cw/8)+3, floor((W − 7cw)/8)). Recompute it on every resize, as XP does.
* Stock at (g, 5s); waste at (2g + cw, 5s); foundation j at (g + (3+j)(cw+g), 5s); tableau k at
  (g + k(cw+g), ch + 11s). In XP the 5 is `MulDiv(ch,5,100)` and 107 = ch + 2·5 + 1.
* Steps: face-down floor(ch/25), face-up floor(4ch/25), waste fan (floor(cw/5), 1s).
* 3-D edges: (+2s,+1s) per 10 cards on the stock and waste, per 4 cards on the foundations.
* Default client (7cw + 8g, 4ch) with g = cw/8+3: 585 x 384 at s = 1.
  * Choosing s: s = min(W/585, H/384) gives the original proportions.
  * At 1920x1080 (client ≈1904 x ≈1000 after the menu and status bar) s ≈ 2.6, so the card is ≈185 x 250.
  * XP itself never scales: an HD clone should fix s from the window and then apply XP's own
    horizontal-spread rule to the remaining width.
* Status bar height = system-font height + 2. Scale it with the UI font, not with s.
* Tall columns: XP never compresses. Keep that as the default and offer compression as an option at most.

---

## 11. HD card backs proposal

### 11.1 XP's 12 backs and animation

* XP's backs are cards.dll ids 54–65, 71x96 24-bpp photographs with magenta (255,0,255) corner keys.
  cards.dll restores the background there.
* Selection: Deck dialog. Default when the registry `Back` value is absent: `rand() % 12 + 53`, clamped to
  54–65. That makes **54 twice as likely, and 65 never chosen** (`rules.md` §10).
* **None is animated in XP** (§9). The animated ones existed only in the 16-bit/9x/2000 deck, which had
  different backs: robot, castle, beach and card hand.

### 11.2 Source: RevK generator (CC0)

* Source: `https://www.me.uk/cards/makeadeck.cgi`, the same CC0 source as our faces; see
  `res/LICENSE-ART.md`. The zip contains `1B.svg` (pattern in `blackcolour`) and `2B.svg` (pattern in
  `redcolour`); `backcolour` fills the card.
* Designs offered: **Diamond** (fine lattice), **Goodall** (19th-century ornamental frame, 660 KB SVG),
  **Arrows** (multicolour confetti marks; ignores the colour parameters), **Maze**, **Illusion**
  (checker illusion), **Plain**, and **Marked**.
  * Marked is a marked-cards deck: 108 numbered files, backs that reveal the card. **Unsuitable.**
* Downloaded: all 7 designs × red/blue, plus colour tests. `<sr>/backs/zips/`, rasterised as
  `<sr>/backs/*_1B.png` / `*_2B.png`, overview in `<sr>/backs/generator_backs_sheet.png`.

### 11.3 Proposed set

Each is one generator request: the common query string
`size=poker&ace=Fancy&ace1=&ace2=&qr=&value=0&super=1&pip=1&wider=on&zip=Download` plus
`&back=<Design>&redcolour=%23<pattern>&backcolour=%23<background>`; use `2B.svg`. Saved as
`<sr>/backs/proposal/<id>_<name>.svg/.png`, with 1x/3x renders in `<sr>/backs/proposal_1x_3x.png`, and the
side-by-side **contact sheet `<sr>/backs/contact_xp_vs_proposal.png`**.

| Replaces XP back | XP motif | HD back | Design | Pattern | Background | Animated? (original CC0 idea) |
|---|---|---|---|---|---|---|
| 54 | sky, clouds | **Sky** | Diamond | #3A78E8 | #FFFFFF | – |
| 55 | aqua glass | **Aqua** | Illusion | #0A9FB0 | #D8FBFF | – |
| 56 | tropical fish | **Fish** | Goodall | (black ornament) | #2A6FD6 | bubbles rising from the centre medallion, 3 frames |
| 57 | frog | **Frog** | Maze | #2E9E2E | #FF7A1A | – |
| 58 | red swirl | **Rose** | Diamond | #D0102A | #FFFFFF | – (the classic red back) |
| 59 | palm island | **Island** | Goodall | (black) | #20B8C8 | **sun with sunglasses winks** (homage to sprites 681/682), 2 frames |
| 60 | blue mosaic | **Mosaic** | Illusion | #2F7FD0 | #A8D8F0 | – |
| 61 | purple lights | **Orchid** | Diamond | #A0209A | #FBE6FB | – |
| 62 | desert, moon | **Night** | Maze | #E08A3A | #10204A | **bats flap past a moon** (homage to castle bats, 680), 2 frames |
| 63 | astronaut | **Space** | Arrows | (multicolour) | #0B1030 | **satellite with blinking lamps** (homage to robot lamps, 683/684), 2 frames |
| 64 | orange stripes | **Sunset** | Goodall | (black) | #FFC040 | **ace slides out of the frame** (homage to card hand, 678/679), 3 frames |
| 65 | race cars | **Racing** | Illusion | #128A12 | #FFF3A0 | – |

Notes:

* The mix is Diamond ×3, Illusion ×3, Goodall ×3, Maze ×2, Arrows ×1. It follows XP's dominant colour per
  slot, so a stored `Back` value keeps roughly its colour.
* For Goodall the `redcolour` parameter did not change the ornament: it stays black, with an accent colour
  the generator chooses itself. Recolouring the SVG locally is allowed (CC0) if wanted.
* **Moiré:** Diamond's 6-unit lattice (2.5% of the card width) averages to a flat tint at 71 px wide and is
  crisp from about 2x. Render backs from SVG at the target size with a box/Lanczos filter, never with
  nearest-neighbour downscaling.
* **Animation plan, faithful to the XP/16-bit plumbing:**
  * drive the sprites from the existing 250-ms tick (frame = tick), only on the stock's top card;
  * only while the stock is non-empty, not dragging, and (strict XP semantics) timed play running. A
    "always animate" option is a possible extra.
  * Redraw only the sprite rect.
  * The sprites (sun, bats, satellite, sliding ace) must be **our own drawings** dedicated CC0. Do **not**
    trace cards.dll 678–684 (Microsoft artwork).
  * Default: animation **off**, so the out-of-the-box look matches XP, where nothing animates.

---

## Shot index (`<sr>/shots/`)

| File | Shows |
|---|---|
| `v01_launch.png`, `v01_launch_w.png` | default window 585 x 384, after the start-up deal |
| `v02_800x500`, `v03_1024x700`, `v04_500x400`, `v05_300x250`, `v06_maximized` | horizontal re-layout, fixed y, clipping |
| `v20_poked.png` | 19-card column running under the status bar (no compression), a 4-card face-up run |
| `drag/d00–d03`, `sheets/drag_full.png`, `sheets/drag_corner_x10.png` | full drag; corner pixels from pick-up |
| `drag/o01_mid`, `o02_after`, `sheets/drag_outline.png` | outline drag (R2_NOT) |
| `stock/s01, s02, s08, s09_recycled`, `sheets/stock_waste_x2.png` | waste fan, empty stock O, recycle |
| `stock/vegas_start`, `vegas_x`, `sheets/vegas_x_marker_status_x2.png` | Vegas status `-$52` in red, empty stock X |
| `act/a00–a03` | double-click home, flip, right-click autoplay |
| `act/status_menuselect.png` | menu help text in the status bar |
| `sheets/status_bar_x6.png`, `sheets/stock_corner_x8.png`, `sheets/foundation_ghost_x8.png` | pixel zooms |
| `win/w00_before`, `win/wseq_000–039`, `win/w99_*`, `sheets/win_forced_frames.png` | forced win (Alt+Shift+2), dialog, cleared table |
| `win/a01_before_abort`, `a02_after_abort`, `a03_after_yes` | abort by key, "Yes" deals |
| `natwin/n00_board`, `n01_before_cascade`, `m01`, `m02`, `mseq_*`, `natwin_sheet.png`, `sheets/win_natural_frames.png` | natural win (last K♠ by double-click) |
| `natwin/win_replay.gif` (+ `_last.png`) | frame-exact replay of the logged cascade |
| `natwin/n99_after_no_resized.png` | after "No" + repaint: empty table |
| `sheets/cardsdll_backs_and_specials.png` | cards.dll 53–65, 67, 68, sprites 678–684 |

Logs and tools in `<sr>/visuals/`:

* `win1_gdilog.txt` (forced win) and `natwin2_gdilog.txt` (natural win): full cascade call logs.
* `deal_log.txt`, `stock_click1.txt`, `stock_recycle.txt`, `drag1_{down,move,up}.txt`, `ol_*.txt`,
  `act_*.txt`, `abort_gdilog.txt`.
* `winphys.py` (model fit), `replay.py`, `drv/` (the extended driver), `run.sh`, `retry.sh`.

## UNVERIFIED / caveats

* XP outer window height (Luna metrics); the COLOR_WINDOWFRAME border colour of the status bar (Wine
  draws 158 grey).
* Real-XP cascade frame period (depends on the system timer resolution, §8.3).
* Draw-one waste behaviour was read from code only. The keyboard interface was not exercised live, because
  SetCursorPos would move the host cursor.
* Fonts: Wine substitutes "MS Shell Dlg". Metrics differ slightly from XP's Microsoft Sans Serif Bold.
  Glyph positions in the status bar are therefore Wine's.
* The historical 16-bit animation timing (4 fps via the stock hook) is inferred from the surviving plumbing.
