# XP Solitaire (Klondike): game rules and state logic (reverse-engineered)

Target: `sol.exe`, 32-bit x86, PE timestamp 0x3B7D8480, ImageBase 0x01000000, entry 0x1005F85, 56,832 bytes, with `cards.dll` from the same XP RTM build. No symbols were available (the debug directory only names `sol.pdb`), so everything below comes from disassembly and is cited by virtual address (VA). The program is Wes Cherry's original Solitaire engine: a generic "game" object and generic "column" objects talk to each other through message procedures, and Klondike supplies procedures that override some of those messages.

**Two kinds of evidence:**
- **Static.** A recursive-descent disassembly with capstone covers all code from 0x1001370 to 0x1006D2E. It comes from `sol-research/soldis.py` → `sol-research/sol.asm` in the session scratchpad. Two jump tables are decoded: the game dispatcher at 0x10031DF (20 entries) and the column dispatcher at 0x100408E (28 entries).
- **Empirical.** I ran a private copy of the real `sol.exe` + `cards.dll` under Wine, Windows version set to XP, in a private prefix.
  - Some runs used the `null` graphics driver, so the game's `SetCursorPos` never moved the host cursor.
  - My own helper `sdrv.exe` (`sol-research/sdrv.c`) found the window by PID and read and wrote the game's memory: the game object, the 13 columns and the globals. It also posted mouse, key and WM_COMMAND messages, held modifier keys through `AttachThreadInput` + `SetKeyboardState`, pressed dialog buttons and dumped the registry.
  - Status-bar strings were read with a private build of the repo's `fcdrive` text hook (`drawn_text`).
  - Items checked this way are marked **[Wine-verified]**.

**Card encoding.** Used throughout, and identical to the `cards.dll` `cdtDraw` ordinals.
- `cd = rank*4 + suit`, where `rank` 0 = A … 12 = K and `suit` 0 = ♣, 1 = ♦, 2 = ♥, 3 = ♠.
- A card is stored as a 16-bit word with **bit 15 = face up** (`0x8000`).
- Two cards are opposite colours when `(s1 ^ s2)` is 1 or 2. They are the same colour when it is 0 or 3 (see 0x10042CE).
- Notation below: `#9C` is a face-down nine of clubs and `9C` a face-up one.

---------------------------------------------------------------------------------------------------

## 0. Data model and global map

### 0.1 Columns ("piles")
The game object holds 13 columns. The index is fixed, and it is also the order used for drop-target search, autoplay and the keyboard cursor:

| Index | Role | Column class `tcls` | Procedure | Capacity | Fan parameters (dxUp, dyUp, dxDn, dyDn, every-n-up, every-n-down) at 71×96 |
|---|---|---|---|---|---|
| 0 | Stock ("deck") | 1 | 0x1004823 | 52 | 0, 0, 2, 1, 1, 10. The 3-D edge shifts 2,1 px every 10 face-down cards. |
| 1 | Waste ("discard") | 2 | 0x10049BD | 24 | `cardW/5`=14, 1, 2, 1, 1, 10 |
| 2–5 | Foundations (left to right) | 3 | 0x1004597 | 13 | 2, 1, 0, 0, 4, 1. A face-up stack shifts 2,1 px every 4 cards. |
| 6–12 | Tableau columns 1–7 | 4 | 0x100448D | 19 | 0, `4*cardH/25`=15, 0, `cardH/25`=3, 1, 1 |

Columns and their classes are created by 0x1005581: game 0xA0 bytes, class constructor 0x100322F, column constructor 0x1003274.

**Foundations have no fixed suits.** Any ace can go to any empty foundation.

### 0.2 Structures

**COL (column):**

| Offset | Field |
|---|---|
| +0 | class pointer |
| +4 | procedure |
| +8 | `RECT rc` |
| +0x18 | `pmove`, the selection, or NULL |
| +0x1C | `icrdMac`, the number of cards |
| +0x20 | `icrdMax` |
| +0x24 | `rgcrd[]`, 12 bytes each: the card word, then `POINT pt` at +4/+8. Index 0 is the bottom card and `icrdMac-1` the top (exposed) card. |

**CLSCOL (column class):** +0 `tcls`, +4 procedure, +8 reference count, +0xC dxUp, +0x10 dyUp, +0x14 dxDn, +0x18 dyDn, +0x1C dcrdUp, +0x20 dcrdDn.

**MOVE (single global at 0x1007194):** +0 `icrdSel` (first selected card), +4 `ccrdSel` (number of cards), +8/+0xC offset from the grab point to the card origin, then drag bitmaps and DCs, and +0x34 a LineDDA step counter.

**GM (game object, pointer at 0x1007170):**

| Offset | Meaning |
|---|---|
| +0x00 | game procedure (0x10054A2 for Klondike) |
| +0x04 | `fUndo`: undo record valid |
| +0x08 | "this undo record is a recycle" flag (§6) |
| +0x0C | undo: saved score |
| +0x10 / +0x14 | undo: indices of the two saved columns |
| +0x18 | undo: saved recycle count |
| +0x1C / +0x20 | undo: copies of the two columns (52-card buffers, 0x10056FA) |
| +0x24 | `fDealt`: a game is in progress. Mouse input is ignored when it is 0. |
| +0x28 | `fInput`: set by the first mouse-down. The clock runs only after this. |
| +0x2C | win animation in progress |
| +0x30 | **score** (signed) |
| +0x34 | **clock in 250 ms ticks** (capped at 0x7FFE) |
| +0x38 | time-penalty interval in ticks, constant 40 (0x10055CA) |
| +0x3C | draw count (copied from the option at Init) |
| +0x40 | **number of times the waste was recycled into the stock** |
| +0x44 / +0x48 | previous drag point |
| +0x4C | mouse button down |
| +0x50 / +0x54 | keyboard cursor: column, card |
| +0x58 | `icolSel`: column being dragged, −1 if none |
| +0x5C | `icolHilight`: current drop target, −1 if none |
| +0x64 | number of columns (13) |
| +0x6C | `rgpcol[13]` |

### 0.3 Globals

| VA | Meaning (initial value) |
|---|---|
| 0x1007020 | option Status bar (1) |
| 0x1007024 | option Timed game (1) |
| 0x1007028 | option Scoring: 0x12E Standard (initial) / 0x12F Vegas / 0x130 None. These are the dialog control ids. |
| 0x100702C | option Draw count: 3 (initial) or 1 |
| 0x1007184 | option Cumulative score (0) |
| 0x1007188 | option Outline dragging (0) |
| 0x1007008 | card back, as a `cdtDraw` back id 54–65 |
| 0x1007034 | Standard score table `int[8]`: −2, −20, 10, 5, 5, −15, 0, 0 |
| 0x1007054 | Vegas score table `int[8]`: 0, 0, 5, 0, 0, −5, −52, 0 |
| 0x1007344 | seed of the last deal (written, never read) |
| 0x1007340 / 0x1007370 | `iCurrency` / `sCurrency` (§4.5) |
| 0x100716C | main window is minimized (set in WM_SIZE) |
| 0x1007190 | "fan from the previous card" flag used by the waste (§3.2) |
| 0x10071CC | win animation drawing flag |
| 0x1007308 / 0x100730C | card width and height from `cdtInit` (71 × 96) |

### 0.4 Message numbers
Messages are dispatched by `call [obj]`: the game procedure is called as `(pgm, msg, wp1, wp2)` and a column procedure as `(pcol, msg, wp1, wp2)`.

**Game messages** (generic handler DefGmProc 0x10030F9; Klondike handler 0x10054A2 overrides 3, 6, 8, 12–18):

| # | Message | Handler |
|---|---|---|
| 0 | Init(fResetScore) | 0x10028A0 |
| 1 | End | — |
| 2 | KeyHit(vk) | 0x1002EB8 |
| 3 | MouseDown(ppt, icolFirst) | 0x10028DB; Klondike **0x1004BDD** |
| 4 | MouseUp(ppt, fCancel) | 0x100295B |
| 5 | MouseMove(ppt) | 0x1002B92 |
| 6 | DblClk(ppt) | 0x1002A0D, then Klondike falls back to 0x1004BDD |
| 7 | Paint | — |
| 8 | Deal(fFromOptions) | Klondike **0x1004A7C** |
| 9 | Undo | **0x1002C9D** |
| 10 | SaveUndo(icol1, icol2) | **0x1002D12** |
| 11 | KillUndo | `fUndo=0` |
| 12 | IsWinner | Klondike 0x1004D1E |
| 13 | Winner | Klondike **0x1004DF0** → generic 0x10030C8 |
| 14 | ScoreMove(pcolDst, pcolSrc) | Klondike **0x100505F** |
| 15 | ChangeScore(event, delta) | Klondike **0x10050BF** → generic **0x100306F** |
| 16 | DrawStatus | 0x1005203 |
| 17 | Timer | Klondike **0x1005189** |
| 18 | ForceWin | Klondike 0x1004FB6 |
| 19 | Autoplay | **0x1002A45** |

**Column messages** (generic handler DefColProc 0x1003F21):

| # | Message | Notes |
|---|---|---|
| 1 | Init | — |
| 2 | End | — |
| 3 | Clear | — |
| 4 | NumCards(fFaceUpRunOnly) | — |
| 5 | HitTest(ppt, icrdMin) | returns `icrd`, or 0x1FFF "none", or 0x1FFE "empty stock" |
| 6 | Select(icrd \| 0x1FFC "top", count \| −2 "to the end") | — |
| 7 | EndSel | — |
| 8 | Flip(fUp) | — |
| 9 | Invert | reverse the order of the selected cards |
| 10 | EndDrag(ppt, fAnimateBack) | — |
| 11 | DblClk | — |
| 12 | Remove | — |
| 13 | Insert | — |
| 14 | Move(pcolSrc, icrd \| 0x1FFD "append") | 0x10036DA |
| 15 | Copy | — |
| 16 | **CanDrop(pcolSrc)** | — |
| 17 | ValidMovePt(pcolSrc, ppt) | 0x1003BCB |
| 18 | Render | — |
| 19 | Paint | — |
| 20 | Drag | — |
| 21 | ComputeCardPositions(icrdFirst, fForceDown) | 0x1003AEA |
| 22 | Hilight | — |
| 23 | GetCardPoint | — |
| 24 | CanFocus(fDragging) | — |
| 25 | CanFocusCard(icrd) | — |
| 26 | Shuffle | 0x1003DFE |
| 27 | timer hook (stock: no-op) | — |
| 28 | AnimateBack | LineDDA |

### 0.5 Key functions (VA → my name)
- **Window and setup:** 0x1001B2C InitInstance · 0x10016BD MainWndProc · 0x1001E9E message loop · 0x100142B TimerProc (every 250 ms) · 0x1001468 NewDeal (reseed and deal)
- **Options and registry:** 0x1001504 LoadOptions · 0x1001624 SaveOptions(mask) · 0x1002540 RegReadDword · 0x100247F RegReadString · 0x1002415 RegWriteDword · 0x100575F OptionsDlgProc · 0x1005A1F Options command · 0x1005AB4 BackDlgProc
- **Deal:** 0x10045CB StockInit · 0x1003DFE Shuffle · 0x1004A7C KlondDeal · 0x1004BDD KlondMouseDown (stock clicks)
- **Mouse:** 0x10028DB DefMouseDown · 0x100295B DefMouseUp · 0x1002B92 DefMouseMove · 0x1002A0D DefDblClk · 0x100438C DblClkToFoundation · 0x1002A45 Autoplay
- **Legality and hit tests:** 0x1004279 TableauCanDrop · 0x1004531 FoundationCanDrop · 0x10042EF TableauHitTest (turns cards over) · 0x1004625 StockHitTest (cheat) · 0x1004913 WasteHitTest · 0x100334A DefHitTest · 0x1003BCB ValidMovePt
- **Moving and layout:** 0x10048D2 WasteMove (fanning) · 0x1003AEA ComputeCardPositions · 0x10036DA DefMove
- **Undo, scoring, clock:** 0x1002C9D Undo · 0x1002D12 SaveUndo · 0x100505F ScoreMove · 0x10050BF KlondChangeScore · 0x100306F DefChangeScore · 0x1005189 KlondTimer
- **Win:** 0x1004D1E IsWinner · 0x1004DF0 KlondWinner (animation) · 0x10030C8 "Deal Again?" · 0x1004FB6 ForceWin
- **Keyboard:** 0x1002EB8 KeyHit · 0x1002D93 / 0x1002DDA / 0x1002E4B keyboard cursor
- **Display:** 0x1005203 DrawStatus · 0x10046DF StockRender (X/O symbol)

---------------------------------------------------------------------------------------------------

## 1. Deal algorithm and RNG

### 1.1 Seeding and game number
The RNG is msvcrt `rand`/`srand` (IAT 0x10011F8/0x1001200), the Microsoft LCG `x = x*214013 + 2531011; return (x>>16) & 0x7FFF`.

```c
void NewDeal(BOOL fReseed, BOOL fFromOptions) /*0x1001468*/ {
    if (fReseed) { g_seed = time(NULL) & 0x7FFF; srand(g_seed); }   // 0x100146F-0x1001482
    pgm->proc(pgm, msggmDeal, fFromOptions, 0);
}
```

**Every deal reseeds from the clock**, so there are only **32,768 possible deals**. Two deals within the same second are identical, and the cycle repeats every 9.1 hours.
- **Callers:**
  - WM_COMMAND 1000 "Deal" (F2, and "Deal Again? → Yes"): `NewDeal(1, 0)` (0x10019AE).
  - Options OK when something that forces a redeal changed: `NewDeal(1, 1)` (0x1005A42).
- **Startup:** InitInstance also calls `srand(time(NULL) & 0xFFFF)` (0x1001C26). It then posts WM_COMMAND 1000 at the end of initialization (0x1001E7A), unless `nCmdShow` is SW_MINIMIZE or SW_SHOWMINNOACTIVE. The first deal therefore reseeds too, and the startup seed is irrelevant.
- **Observed seeds** **[Wine-verified]**: 27937, 28172, 28481, 28688, 28994. Each one was ≈ `time(NULL) & 0x7FFF` at the moment of the deal.

**There is no user-visible game number.**
- The seed is stored at 0x1007344 and never shown.
- The string table has "Set game number" (1005), "Print # of cards in each col" (1006), "Assertion failure" (1007), "Heck, I don't know" (1008), "Configure Solitaire for screen shots" (1009) and dialog 102 "Set Next Game Number / Game #". These are debug commands. MainWndProc's WM_COMMAND switch (0x1001968) does not handle 1005–1009, so they are **unreachable in the release build**.
- The "Assertion Failure" dialog 999 is also unused.

### 1.2 Shuffle and deal

```c
void StockInit(COL *stock) /*0x10045CB*/ {
    for (i = 0; i < 52; i++) { stock->rgcrd[i].cd = i; /* face down */ stock->rgcrd[i].pt = stock->rc.topleft; }
    stock->icrdMac = 52;
    Shuffle(stock);                                  // col msg 26
    ComputeCardPositions(stock, 0, 0);               // col msg 21
}
void Shuffle(COL *c) /*0x1003DFE*/ {
    for (pass = 0; pass < 5; pass++)                 // FIVE passes
        for (i = 0; i < c->icrdMac; i++) {           // icrdMac == 52
            j = rand() % c->icrdMac;                 // rand() is called before the swap
            swap(c->rgcrd[i], c->rgcrd[j]);          // whole 12-byte entries
        }
}
BOOL KlondDeal(GM *pgm, BOOL fFromOptions) /*0x1004A7C*/ {
    erase background;
    for (c = 0; c < 13; c++) colmsg(col[c], Clear);
    colmsg(stock, Init);                             // StockInit: 52 cards + shuffle
    gmmsg(KillUndo);
    BOOL fReset = !(scoring == VEGAS && optCumulative && !fFromOptions);   // 0x1004AD9
    gmmsg(Init, fReset);                             // 0x10028A0: score=0 if fReset, clock=0, recycles=0, fInput=0 ...
    status text = "";  pgm->fDealt = 1;
    gmmsg(ChangeScore, EV_DEAL);                     // Vegas -52, Standard 0
    draw stock; pgm->fDealt = 0; draw the 4 empty foundations;
    for (row = 0; row < 7; row++)                    // 0x1004B5B
        for (t = row; t < 7; t++) {                  // tableau column t = col[6+t]
            colmsg(stock, Select, TOP, 0);           // the top (last) stock card
            if (t == row) colmsg(stock, Flip, UP);   // the first card of each row lands face up
            colmsg(col[6+t], Move, stock, APPEND);   // drawn with the normal move rendering
        }
    keyboard cursor = column 0;  pgm->fDealt = 1;
}
```

- Tableau column *t* (0-based) gets *t* face-down cards and one face-up card on top, for 28 cards in all. The other 24 cards stay in the stock in their shuffled order, with `rgcrd[23]` on top.
- Nothing is moved automatically after the deal: aces stay where they were dealt.

### 1.3 Reference implementation and test vectors

```python
def deal(seed):                      # seed = time(NULL) & 0x7FFF
    x = seed
    def rand():
        nonlocal x; x = (x*214013 + 2531011) & 0xFFFFFFFF; return (x >> 16) & 0x7FFF
    deck = list(range(52))
    for _ in range(5):
        for i in range(52):
            j = rand() % 52; deck[i], deck[j] = deck[j], deck[i]
    tab = [[] for _ in range(7)]
    for row in range(7):
        for t in range(row, 7):
            tab[t].append((deck.pop(), t == row))     # (card, faceUp)
    return deck, tab                                   # deck[-1] is the stock's top card
```

**[Wine-verified]** for seeds 27937, 28172 and 28481: the real program's columns read from memory were identical to this code's output (`sol-research/deal.py`). The listings below are bottom → top; `#` is face down.
```
Seed 27937                                    Seed 28172
stock: #9C #2H #9D #QD #QH #5C #6H #TH #9S   stock: #7H #4S #JH #5C #TD #KH #8C #AD #2S
       #6D #4C #8S #KD #2S #4H #AC #TD #KH          #6S #5D #JS #8S #KD #9H #4C #TS #3D
       #8H #QC #2D #4D #JC #7H                      #QD #TH #5H #8D #QH #3S
T1: 6S                                        T1: JC
T2: #JS 7S                                    T2: #9S 4D
T3: #AH #2C QS                                T3: #KC #3H 8H
T4: #8C #8D #5S 5H                            T4: #7D #9D #AS KS
T5: #AD #AS #7D #4S KS                        T5: #6C #9C #6D #6H AH
T6: #KC #JD #TS #9H #3S 7C                    T6: #QC #2H #2C #7S #4H JD
T7: #3H #6C #3D #5D #3C #JH TC                T7: #3C #7C #TC #5S #2D #AC QS

Seed 28481                                    Seed 1 (from the reimplementation)
stock: #5H #9H #4C #QS #AC #2C #QH #TS #7H   stock: #AS #6D #8C #8H #8S #5H #JS #6C #JH
       #3D #7D #TH #KC #3S #AD #6S #KS #8D          #TS #QC #TC #TH #2D #4C #3C #2C #9C
       #2H #7C #QC #6H #3H #8S                      #AH #9S #9H #JD #4H #QS
T1: 6C                                        T1: 5D
T2: #2D JC                                    T2: #7H 6S
T3: #6D #5C KH                                T3: #9D #QD TD
T4: #JD #4S #8H TD                            T4: #6H #5S #AC AD
T5: #9S #AH #7S #TC AS                        T5: #KD #JC #2H #3D 7D
T6: #2S #QD #8C #4H #9C KD                    T6: #2S #7C #3S #KH #KS 4D
T7: #TC #8C #7D #3S #JH #8S KC                T7: #8D #4S #7S #KC #5C #3H QH
```

---------------------------------------------------------------------------------------------------

## 2. Legal moves

### 2.1 Drop predicates (column message 16 "CanDrop")

```c
BOOL TableauCanDrop(COL *dst, COL *src) /*0x1004279*/ {
    cd = src->rgcrd[src->pmove->icrdSel].cd;              // the BOTTOM card of the moving group
    if (rank(cd) == KING) return dst->icrdMac == 0;        // kings (and stacks headed by a king) only onto empty columns
    if (dst->icrdMac == 0) return FALSE;                   // nothing else onto an empty column
    top = dst->rgcrd[dst->icrdMac-1];
    if (!top.fUp) return FALSE;
    s = suit(top) ^ suit(cd);  if (s == 0 || s == 3) return FALSE;   // must be opposite colour
    return rank(cd) + 1 == rank(top);                      // one lower
}
BOOL FoundationCanDrop(COL *dst, COL *src) /*0x1004531*/ {
    if (src->pmove->ccrdSel != 1) return FALSE;            // single cards only
    cd = src->rgcrd[src->pmove->icrdSel].cd;
    if (dst->icrdMac == 0) return rank(cd) == ACE;         // any ace to any empty foundation
    top = dst->rgcrd[dst->icrdMac-1];
    return rank(top) + 1 == rank(cd) && suit(top) == suit(cd);
}
// Stock (0x10048AC) and waste (DefColProc returns 0): never a drop target.
```

### 2.2 What can be picked up (column message 5 "HitTest")
- **Tableau** (0x10042EF, then DefHitTest 0x100334A):
  - If the **top card is face down** and is clicked, the click **turns it over** (score event "turn over") and **kills Undo**. It returns "nothing hit", so no drag starts. **[Wine-verified]**
  - Otherwise the point is tested against the cards from the top down; the first card whose full `cardW × cardH` rectangle contains it is hit. **That card must be face up**: if the search reaches a face-down card first, nothing is hit. The selection is that card and **every card above it**.
  - So any face-up card can be grabbed, together with the run on top of it. The run is not checked for being ordered, but a dealt or played face-up run is always ordered.
- **Waste** (0x1004913): only the **top card** can be hit. `icrdMin = icrdMac-1`. **[Wine-verified]**
- **Foundation:** no override, so it uses DefHitTest with `icrdMin = 0`. Normally this hits the top card, which can be dragged to the tableau or to another foundation.
  - **Foundation → foundation** is legal under FoundationCanDrop. In practice this is only an ace moving to another empty foundation. It scores nothing. **[Wine-verified]**
  - **Bug:** foundation cards fan 2,1 px every 4 cards. A full pile therefore has the top card at +6,+3 and leaves a 2–6 px strip of lower cards visible on the left and top. A press in that strip hits a **lower** card and selects it **plus everything above it**.
    - The group can be dropped on any tableau card that accepts its bottom card. The foundation → tableau score applies once.
    - **[Wine-verified]**: a press at (258,60) on a full ♣ pile picked up 4C…KC, and all 10 cards were dropped on 5H (Vegas −5). See §11.
- **Stock:** cannot be dragged. A press on it draws or recycles (§3).

### 2.3 Drag-and-drop pipeline
There is **no click-to-select / click-to-place mode**. Moves are made by dragging, by double-click, by right-click autoplay or with the keyboard.
- A press and release without any WM_MOUSEMOVE in between moves nothing. **[Wine-verified]**

```c
MouseDown(pt) /*0x10028DB*/:  if (icolSel != -1 || !fDealt) return;  fInput = 1;  fButtonDown = 1;
        for (c = 1; c < 13; c++) if ((i = colmsg(col[c], HitTest, pt)) != NONE) { icolSel = c; break; }   // c=0 handled by 0x1004BDD
MouseMove(pt) /*0x1002B92*/:  colmsg(col[icolSel], Drag, pt);
        for (c = 0; c < 13; c++)                              // FIRST column in index order wins
            if (colmsg(col[c], ValidMovePt, col[icolSel], pt) != NONE) { hilight(c); return; }
        unhilight();                                          // icolHilight = -1
ValidMovePt(dst, src, pt) /*0x1003BCB*/:
        r = card-sized rectangle of the dragged bottom card at its current drag position;
        hit = dst->icrdMac ? IntersectRect(r, rect of dst's top card) : IntersectRect(r, dst->rc);
        return hit && CanDrop(dst, src) ? dst->icrdMac : NONE;
MouseUp(fCancel) /*0x100295B*/:  fButtonDown = 0;  if (icolSel == -1) return;
        if (icolHilight != -1) {
            gmmsg(SaveUndo, icolHilight, icolSel);            // NOTE: before the fCancel test (§11 bug)
            if (fCancel) colmsg(src, EndDrag, animateBack);
            else { colmsg(src, EndDrag, 0);
                   colmsg(dst, Move, src, APPEND) && gmmsg(ScoreMove, dst, src);
                   icolHilight = -1;
                   if (gmmsg(IsWinner)) gmmsg(Winner); }
        } else colmsg(src, EndDrag, animateBack);              // cards fly back along a straight line
        icolSel = -1;
```

- **The drop target is the first column, in index order** (stock, waste, foundations left to right, tableau left to right), whose top-card rectangle (or empty-column rectangle) **overlaps the dragged card's rectangle by any amount** and accepts the card.
  - **[Wine-verified]** with 5S dragged so that it overlapped 6H in tableau 2 by 4 px and 6D in tableau 3 by 56 px: it went onto 6H.
  - The point where the button is released is not used. The target is the one highlighted at the last WM_MOUSEMOVE.
- **Empty tableau drop zone:** the whole column rectangle, `cardW` wide and tall enough for 6 face-down and 12 face-up cards (11,107)–(82,401) at 71×96. A king dropped anywhere in that band lands.
- **Empty foundation drop zone:** its `rc`, which is slightly larger than a card ((257,5)–(334,106) for the first foundation).
- **Outline dragging:** dragged cards are drawn as XOR outlines, and the valid target's top card is shown inverted. Legality and scoring are unaffected.

Rules summary **[all Wine-verified]**:
- tableau → tableau moves a card plus everything on top of it, in alternating colours and descending order;
- **only a king** (alone or heading a run) may go to an empty column, and any other card is refused;
- waste top → tableau or foundation;
- tableau top → foundation (single card; a 2-card group dropped on a foundation is refused);
- foundation top → tableau (and → another foundation);
- **face-down cards are never turned over automatically**; the player must click them.

---------------------------------------------------------------------------------------------------

## 3. Stock and waste

### 3.1 Clicking the stock (KlondMouseDown 0x1004BDD)
This acts on **button-down**: the action happens on WM_LBUTTONDOWN, and the button-up does nothing.

```c
if (icolSel != -1 || !fDealt) return 0;
r = colmsg(stock, HitTest, pt);              // 0x1004625
if (r == NONE) return DefMouseDown(pt, /*first col*/1);
fInput = 1;                                   // the clock starts even if nothing happens below
if (r == EMPTY_STOCK /*0x1FFE: stock empty and pt inside the card-sized rect at the stock's origin*/) {
    if (waste empty) return 0;
    if (scoring == VEGAS && cRecycle == cDraw - 1) return 0;          // pass limit, 0x1004C3B
    cRecycle++;  pgm->fUndoIsRecycle = 1;
    gmmsg(SaveUndo, 1 /*waste*/, 0 /*stock*/);
    colmsg(waste, Select, 0, ALL); colmsg(waste, Flip, DOWN); colmsg(waste, Invert);
    gmmsg(ScoreMove, stock, waste);          // recycle penalty, §4
    colmsg(stock, Move, waste, APPEND);
} else {                                      // r = first card to draw (StockHitTest)
    gmmsg(SaveUndo, 1, 0);
    colmsg(stock, Flip, UP); colmsg(stock, Invert);
    colmsg(waste, Move, stock, APPEND);       // no score for drawing
}
int StockHitTest() /*0x1004625*/ {
    n = (GetKeyState(VK_MENU) & GetKeyState(VK_CONTROL) & GetKeyState(VK_SHIFT)) < 0 ? 1 : pgm->cDraw;  // CHEAT
    icrdSel = max(0, icrdMac - n);  ccrdSel = icrdMac - icrdSel;  return icrdSel;
}
```

- **Draw count:** draw one turns 1 card. Draw three turns `min(3, cards left)`.
- **Order:** the selected packet is flipped and reversed, so the card that was **third from the top** of the stock ends up **on top** of the waste, as when a packet is turned over by hand.
  - **[Wine-verified]**, seed 27937: the stock top was …4D JC 7H, and the waste became 7H JC **4D**.
- **Recycling** turns the whole waste over (flip and reverse), so the next pass sees the cards in the same order as before.
  - It needs an empty stock and a non-empty waste.
  - Clicking an empty stock when the waste is empty does nothing.
- **Pass limits:** **unlimited in Standard and None scoring**. In **Vegas**, recycling is refused when `cRecycle == cDraw−1`:
  - **draw one: 0 recycles (1 pass)**;
  - **draw three: 2 recycles (3 passes)**.
  - **[Wine-verified]** both.
  - Undoing a recycle gives the pass back (§6).
- **Empty-stock symbol** (0x10046DF):
  - `cdtDraw` mode 6 (the "X", no more redeals) when Vegas and `cRecycle == cDraw−1`;
  - otherwise mode 7 (the "O", click to redeal).
- **Cheat:** holding **Ctrl+Alt+Shift** while pressing the stock draws a **single card** in draw-three mode. **[Wine-verified]**: 1 card with the modifiers, 3 without.
- **Double-click on the stock** draws twice, because the DBLCLK falls through to the stock handler. On an empty stock it recycles and then draws. **[Wine-verified]**: 3 cards, then 1.

### 3.2 How many waste cards are shown, and which is playable
- Only the waste's **top card** is playable (§2.2).
- **Display** (WasteMove 0x10048D2 + ComputeCardPositions 0x1003AEA): before new cards are added, the last three waste cards are re-laid in the collapsed "face-down" spacing. That is 2,1 px every 10 cards, which in practice means one pile.
- The new cards are then laid starting **at the position of the previous top card** (flag 0x1007190), each next card offset by **`cardW/5` px right and 1 px down** (14,1 at 71 px).

Waste display by mode **[Wine-verified positions]**:

| Mode / situation | Waste display |
|---|---|
| Draw three | After each draw the 3 new cards fan: `x = pile, pile+14, pile+28` (`y = 5, 6, 7`). Older cards sit under the first one. Example: 7H@(93,5) JC@(107,6) 4D@(121,7). After the next draw: 7H JC 4D 2D all @(93,5), QC@(107,6), 8H@(121,7). |
| Fewer than 3 cards left | Only those are fanned. One card shows no fan. |
| Playing the top waste card | The remaining fanned cards **stay where they are** (2, then 1). When all three are gone, the collapsed pile's top card shows at the pile position. Nothing is re-fanned until the next draw. |
| Draw one | Every card lands at the previous top's position, so the waste is a **single pile with no fan** (8H@(93,5) 9C@(93,5)). |

---------------------------------------------------------------------------------------------------

## 4. Scoring

### 4.1 Score events

**ScoreMove** (0x100505F) is called after every successful move with (destination class, source class):

| Destination ← source | Event |
|---|---|
| stock ← waste (recycle) | **1** |
| foundation ← waste, foundation ← tableau | **2** |
| tableau ← waste | **3** |
| tableau ← foundation | **5** |
| tableau ← tableau, foundation ← foundation, waste ← stock (draw) | nothing |

Other events:
- **4**: a face-down tableau card was turned over (0x1004368).
- **6**: deal (0x1004B16).
- **7**: win bonus (0x1004E14).
- **0**: the 10-second clock tick (0x10051E4) **and Undo** (0x1002CBF).

With scoring = None (0x130), ScoreMove and ChangeScore return immediately.

### 4.2 Event values: Standard and Vegas (score tables at 0x1007034 / 0x1007054)

| Event | Standard | Vegas |
|---|---|---|
| 0 clock (every 10 s) / Undo | **−2** | 0 |
| 1 recycle waste → stock | draw one: **−100** each time; draw three: **0 for the first 3 recycles, −20 from the 4th on** | 0 (recycles are limited instead) |
| 2 card to foundation (from waste or tableau) | **+10** | **+5** |
| 3 waste → tableau | **+5** | 0 |
| 4 turn over a tableau card | **+5** | 0 |
| 5 foundation → tableau | **−15** | **−5** |
| 6 deal | 0 | **−52** |
| 7 win | time bonus (below) | 0 |

**[Wine-verified]**:
- Standard: +5, +10, +5, −15, −2 (undo), −2 (clock), −100, −100, −100 (floored to 0), recycles 1–3 = 0, 4 = −20, 5 = −20.
- Vegas: −52 at the deal, +5, −5, flip 0, waste → tableau 0, clock 0, undo 0.
- The bonus was +7000 at 100 s and +14560 at 48 s.

### 4.3 Exact arithmetic

```c
BOOL KlondChangeScore(GM *pgm, int ev, int arg) /*0x10050BF*/ {
    if (ev < 0) return DefChangeScore(pgm, ev, arg);
    switch (scoring) {
    case NONE:  return TRUE;
    case VEGAS: d = vegasTab[ev]; break;                       // 0x1007054
    case STANDARD:
        if (ev == EV_WIN) {
            if (!optTimed) { d = 0; break; }                    // stdTab[7] == 0
            d = (pgm->ticks >= 120) ? (20000 / (pgm->ticks >> 2)) * 35 : 0;   // >= 30 s; integer division first
            break;
        }
        if (ev == EV_RECYCLE) {                                 // cRecycle was already incremented
            if (gDraw == 1) { if (pgm->cRecycle < 1) return TRUE; d = -100; }   // i.e. always -100
            else if (gDraw == 3) { if (pgm->cRecycle <= 3) return TRUE; d = stdTab[1]; /* -20 */ }
            else return TRUE;
            break;
        }
        d = stdTab[ev]; break;                                  // 0x1007034
    }
    DefChangeScore(pgm, scoring == VEGAS ? -3 : -4, d);
    return ev == EV_WIN ? d : TRUE;                             // the bonus is returned for the status text
}
BOOL DefChangeScore(GM *pgm, int mode, int d) /*0x100306F*/ {
    if (scoring == NONE) return TRUE;
    if (mode == -4) pgm->score = max(pgm->score + d, 0);       // STANDARD: never below 0
    else if (mode == -3) pgm->score += d;                      // VEGAS: may go negative
    else if (mode == -2) pgm->score = d;                       // (set; unused by Klondike)
    redraw status bar;
}
```

- **The Standard score is clamped at 0 after every change**, so a penalty never carries a debt. For example, 50 − 100 = 0, and later bonuses start from 0. **[Wine-verified]**
- **Time bonus** = `35 × ⌊20000 / seconds⌋`, with `seconds = ticks >> 2`, paid only when Standard, Timed game is on, and `seconds ≥ 30` (`ticks ≥ 120`). It is about 700,000 / seconds.
- **The bonus is added to the score.** Examples: 29 s → 0; 30 s → 23,310; 48 s → 14,560; 100 s → 7,000; 600 s → 1,155.

### 4.4 Vegas cumulative ("Cumulative Score" check box)
- At each deal the score is reset to 0 **unless** `Vegas && Cumulative && the deal did not come from the Options dialog` (0x1004AD9). The −52 is then applied.
- **With Cumulative on**, F2/Deal and "Deal Again? → Yes" carry the running total: −52 → −104 → −156. **[Wine-verified]**
- A redeal forced by the Options dialog **always resets** to −52.
- The running total is **never saved**: it lives in the game object and is lost on exit. Only "Options" and "Back" are ever written to the registry (§10). **[Wine-verified]**: after exit the key held only `Options` (and `Back`).
- Vegas has no time penalty and no bonus. A win credits only the +5 per foundation card already earned, so a full win is −52 + 52 × 5 = **+$208**.

### 4.5 Status bar text (DrawStatus 0x1005203)
The status bar is drawn right-aligned, right to left, in the bold 9-pt "MS Shell Dlg" font:
- **`"Time: " + seconds`**, only when Timed game is on;
- then the **score value**: `"-"` if negative, then (Vegas only) the currency symbol, then `|score|`, then **one space**. It is drawn in **red (RGB 255,0,0)** when negative, unless the display has 2 colours. The value is not shown with None scoring;
- then **`"Score: "`**.

Notes:
- The left part of the bar shows menu help strings (WM_MENUSELECT) and the win message.
- Captured strings **[Wine-verified]**: `"Time: 2"`, `"-$52 "`, `"Score: "`, `"0 "`, `"$0 "`. The bar reads "Score: -$52 Time: 2".
- **Currency.** `iCurrency` (DWORD, default 0) and `sCurrency` (REG_SZ ≤ 10 characters, default `"$"`) are read from **HKCU\Software\Microsoft\Solitaire**, not from the Control Panel. The leftover "intl" section name passed in is ignored. Placement follows `iCurrency`: 0 `$1`, 1 `1$`, 2 `$ 1`, 3 `1 $` (0x1005312–0x1005385). These values are normally absent, so the result is `$52` / `-$52`.

---------------------------------------------------------------------------------------------------

## 5. Clock

- **Ticks:** `SetTimer(hwnd, 666, 250, TimerProc)` (0x1001DF0). Every tick sends msggm 17 → KlondTimer (0x1005189).

```c
if (!optTimed || !pgm->fDealt || !pgm->fInput || g_fIconic) return;   // stopped
pgm->ticks = min(pgm->ticks + 1, 0x7FFE);
if (pgm->ticks % 40 == 0) gmmsg(ChangeScore, EV_CLOCK /*Standard -2 per 10 s*/);
redraw status ("Time: " ticks>>2);
```

- **Start:** the clock **waits for the first mouse press on the table** after the deal. Any press counts: on felt, on a card, or on the stock, even if nothing happens. A keyboard Enter/Space counts too, because it is turned into a mouse press. Arrow keys alone do not start it. **[Wine-verified]**
- It **pauses while the window is minimized** (WM_SIZE stores IsIconic). It keeps running while the window is merely inactive or a modal dialog is open.
- It **stops at the win** (`fDealt = 0`) and is reset only by the next deal.
- The displayed time is whole seconds, `ticks >> 2`, maximum 8191 s.
- With **Timed game off** the clock never counts, nothing is shown, and there is neither the −2 penalty nor the bonus. **[Wine-verified]**: ticks stayed 0.

---------------------------------------------------------------------------------------------------

## 6. Undo

**Single level.** The undo record holds copies of the **two columns** involved, plus the score and the recycle count.

```c
SaveUndo(i1, i2) /*0x1002D12*/: copy col[i1] -> buf1, col[i2] -> buf2; undoScore = score;
        undoRecycle = cRecycle - (fUndoIsRecycle ? 1 : 0); fUndoIsRecycle = 0; fUndo = 1;
Undo() /*0x1002C9D, menu Game > Undo (id 1001), no accelerator*/:
        if (!fUndo) return;
        score = undoScore; cRecycle = undoRecycle;
        gmmsg(ChangeScore, EV_CLOCK);              // Standard: -2 after restoring (floored at 0); Vegas: 0
        col[i1] = buf1; col[i2] = buf2;  (cards, face state and card positions); repaint both;
        fUndo = 0;                                 // one level only
```

**What saves an undo record** (each one overwrites the previous record):
- a drag-drop onto a highlighted target (also on Esc-cancel, see §11);
- each draw from the stock and each recycle (columns waste + stock);
- each double-click to a foundation;
- **each single card moved by autoplay**, so only the last autoplayed card can be undone. **[Wine-verified]**

**What clears it:**
- **turning over a face-down tableau card** (0x1004329). The move that exposed the card can no longer be undone once the card is turned. **[Wine-verified]**
- every deal;
- the win;
- Undo itself.

**Other effects:**
- **Score:** Undo restores the score from before the move and then applies event 0, which is **−2 in Standard** (floored at 0). **[Wine-verified]**: 115 → move −15 → 100 → Undo → **113**; a recycle −20 from 80 → 60 → Undo → **78**.
- Undoing a recycle refunds the penalty and, in Vegas, the pass.
- The clock is not rolled back.
- **Menu state** (WM_INITMENUPOPUP 0x10018E6): Undo is enabled iff `fUndo && icolSel == -1`. Deal, Deck… and About are grayed while a card is being dragged (`icolSel != -1`).

---------------------------------------------------------------------------------------------------

## 7. Automatic moves

### 7.1 Double-click (WM_LBUTTONDBLCLK → msggm 6; class style CS_DBLCLKS)

```c
DefDblClk(pt) /*0x1002A0D*/: for (c = 0; c < 13; c++) if (colmsg(col[c], DblClk, pt, c)) return TRUE;
        return FALSE;                     // -> Klondike treats the click as a mouse press: KlondMouseDown(pt)
DblClkToFoundation(col, pt, icol) /*0x100438C; tableau and waste only*/:
        if (top card face up && pt inside the TOP card)
            for (f = 2; f <= 5; f++)                         // the leftmost foundation that accepts it
                if (CanDrop(col[f], col)) { SaveUndo(f, icol); Move; ScoreMove; if (IsWinner) Winner; return TRUE; }
        return FALSE;
```

- Only the **top card** of a tableau column or of the waste is sent, and only **to a foundation**. A double-click never moves a card to the tableau.
- **[Wine-verified]**: tableau 3D → ♦ foundation +10; waste 4D → foundation +10.
- If nothing can move, the second click becomes an ordinary press. It starts a drag that snaps back, or on the stock it draws again.
- **Double-click on a face-down top card** turns it over with the first click (+5) and sends it to a foundation with the second if possible. **[Wine-verified]**: #AS → +5 +10.

### 7.2 Right-click / Ctrl+A: autoplay everything (msggm 19, 0x1002A45)
- **Triggers:**
  - **WM_RBUTTONDOWN anywhere** in the window (position ignored, button-down, only if no mouse capture);
  - **Ctrl+A** (KeyHit, `GetKeyState(VK_CONTROL) < 0`).

```c
do { moved = 0;
     for (c = 0; c < 13; c++) {
         if (c >= 2 && c <= 5) continue;                        // skip foundations
         if (col[c] empty || top card face down) continue;       // stock top is face down, so it is skipped
         select top card;
         for (f = 2; f <= 5; f++) if (CanDrop(col[f], col[c])) {
             SaveUndo(f, c); Move; ScoreMove; moved++;
             if (IsWinner) Winner(); break; }
     }
} while (moved);
```

- There is **no safety rule**: every card that can legally go to a foundation goes. That includes 2s and 3s that might still be needed in the tableau.
- One card per source column per pass; passes repeat until nothing moves, so cascades complete.
- Aces go to the **leftmost empty** foundation.
- Face-down cards are **not** turned over.
- **[Wine-verified]**: 2C 2S 3C 3S went home in one right-click (+40); later 2D, AH, 2H cascaded (+30).

### 7.3 What is NOT automatic
- No automatic move after the deal or after any move.
- No automatic turning of exposed cards.
- **No end-game auto-complete** (there is no "all cards face up" check). The player right-clicks (or presses Ctrl+A) to finish.

---------------------------------------------------------------------------------------------------

## 8. Win

- **Detection:** IsWinner (0x1004D1E) is true when **all four foundations hold 13 cards**. It is checked after each drag-drop, each double-click to a foundation and each autoplayed card, and never after stock clicks or Undo.

KlondWinner (0x1004DF0):
1. `gmmsg(ChangeScore, EV_WIN)`. The Standard time bonus is added to the score.
2. Undo is cleared, `fDealt = 0` (the clock stops, mouse input is ignored), and the win-animation flag is set.
3. **Status bar left text:**
   - Standard: **`"Bonus: <n>  Press Esc or a mouse button to stop..."`** (two spaces);
   - Vegas or None: `"Press Esc or a mouse button to stop..."`.
   - **[Wine-verified]** both.
4. **Bouncing-card animation:**
   - Order: for each rank **K down to A**, and for each foundation in index order 2..5, the foundation's card at that index is launched from where it lies.
   - Velocity: `dx = rand()%110 − 65`, forced to −20 if `|dx| < 15`; `dy = rand()%110 − 75`. Each frame `x += dx/10; y += dy/10; dy += 3`. On reaching the bottom (`y > clientHeight − cardH`) with `dy > 0`: `dy = −dy·8/10`.
   - A card runs until it leaves the client area left or right. The card image is drawn every frame and never erased, which leaves the trail.
   - Each frame waits in `MsgWaitForMultipleObjects(…, 5 ms, QS_ALLINPUT)`.
   - These messages **abort the whole animation** and are left in the queue: WM_KEYDOWN, WM_SYSKEYDOWN, WM_L/R/MBUTTONDOWN, WM_NCL/R/MBUTTONDOWN and WM_MENUSELECT. Other messages are dispatched. **[Wine-verified]** abort by Esc and by click.
5. The status text is cleared and the **client area is erased** to the table colour.
6. **MessageBox "Deal Again?"**, caption "Solitaire", MB_YESNO|MB_ICONEXCLAMATION (0x34), Yes default (0x10030C8). **[Wine-verified]**
   - **Yes** posts WM_COMMAND 1000: a new deal, with the score kept only if Vegas + Cumulative.
   - **No** leaves the finished foundations on a frozen table: clicks, drags and right-clicks do nothing until Deal. **[Wine-verified]**

There are **no statistics**: no won/lost counts, streaks or high scores, in memory or in the registry. There is no "resign" prompt on Deal or Exit either.

**ForceWin cheat** (§9.3) fills the foundations ♣, ♦, ♥, ♠ in slots 2..5 directly and runs Winner. In Vegas it therefore does not credit the +5 per card. **[Wine-verified]**: foundations AC..KC, AD..KD, AH..KH, AS..KS; the bonus at 48 s was 14,560.

---------------------------------------------------------------------------------------------------

## 9. Keyboard, menu and cheats

### 9.1 Keyboard (WM_KEYDOWN → KeyHit 0x1002EB8)
The model is a **cursor on a column and on a card in it**, `pgm+0x50/+0x54`. Every key also calls **SetCursorPos**, which moves the real mouse pointer onto that card. When something is selected, the pointer is moved one `dyUp` lower on non-empty columns. A selected group therefore travels with the pointer through ordinary WM_MOUSEMOVE dragging.

| Key | Action |
|---|---|
| ← / → | Previous / next column, wrapping. With **Shift**, skip columns of the same class. While dragging, the stock and waste are skipped (CanFocus). |
| Tab / Shift+Tab | Next / previous column **of a different class**: stock → waste → first foundation → first tableau → stock … **[Wine-verified]**: 7 → 0 → 1 → 2 → 6, then Shift+Tab 6 → 5. |
| Home / End | Column 0 (stock) / column 12 (last tableau column). |
| ↑ / ↓ | Move the card cursor within the column's **face-up run**, clamped. A column with no face-up card keeps the cursor on the top card. Stock and waste allow only the top card. **[Wine-verified]**: 3 → 2 → 1 → 1 → 2. |
| Enter / Space | With nothing selected: a mouse press at the cursor card. This picks up that card plus the run on it, or draws, recycles or turns over. With a selection: drop on the current target (MouseUp). **[Wine-verified]**: 5S picked up, moved and dropped on 6H; a 2-card run 5S 4D moved with ↑ and Space. |
| Esc | Cancel the drag (MouseUp with cancel). |
| Ctrl+A | Autoplay, as right-click (§7.2). **[Wine-verified]** |
| F2 | Deal (HIDDENACCEL). |
| F1 | Help contents. WM_HELP opens `sol.chm` through HtmlHelp; strings 65506–65508. |

### 9.2 Menu (resource 1) and accelerators (resource "HIDDENACCEL")
- **Game:** &Deal F2 (1000), &Undo (1001, **no accelerator**), De&ck... (1002), &Options... (1003), E&xit (1004).
- **Help:** &Contents F1 (65506), &Search for Help on... (65507), &How to Use Help (65508), &About Solitaire (2000).
- About is `ShellAbout(hwnd, "Solitaire", "Developed for Microsoft by Wes Cherry", icon)`.
- Accelerator table: VK_F2 → 1000, and **Alt+Shift+2 → 1010** (flags 0x17).
- **Exit** posts WM_SYSCOMMAND SC_CLOSE. There is no confirmation and nothing is saved at exit. **[Wine-verified]**
- **Deal** never asks for confirmation.

### 9.3 Cheats
- **Alt+Shift+2** (WM_COMMAND 1010 "Force a win"): instant win with animation and "Deal Again?" (§8). It works at any time a game is on the table. **[Wine-verified]** via the accelerator.
- **Ctrl+Alt+Shift + click on the stock:** draw one card in draw-three mode (§3.1). **[Wine-verified]**
- There are no others: the debug commands 1005–1009 are unreachable (§1.1).

### 9.4 Odds and ends
- **Registered message "CardDraw"** (string 103, 0x1007160):
  - `wParam` 1 returns `MAKELONG(cardW, cardH)`;
  - 2 calls `cdtDraw` with six DWORD arguments from `lParam`;
  - 3 closes the window.
  - This is an automation hook, not needed for the clone.
- **Command line `/I`** creates the main window with WS_MINIMIZE (0x1001C7C).
- **Losing focus during a drag** (WM_KILLFOCUS) cancels it, as Esc does.

---------------------------------------------------------------------------------------------------

## 10. Options and persistence

**Registry key:** **HKCU\Software\Microsoft\Solitaire**, opened with RegCreateKeyExW for every access.

**Values:**
- **`Options`** (REG_DWORD). It is read only if its type is REG_DWORD; otherwise the default is used.
- **`Back`** (REG_DWORD).
- Optional, read but never written: `iCurrency` (DWORD) and `sCurrency` (REG_SZ).

**Nothing else is stored:** no statistics and no Vegas total. **[Wine-verified]**: dumped after play and after exit.

| `Options` bit | Meaning | Default |
|---|---|---|
| 0 (0x01) | Status bar | 1 |
| 1 (0x02) | Timed game | 1 |
| 2 (0x04) | Outline dragging | 0 |
| 3 (0x08) | Draw three (clear = draw one) | 1 |
| 4–5 (0x30) | Scoring: 0 Standard, 1 (0x10) Vegas, 2 (0x20) None, 3 → Standard | 0 |
| 6 (0x40) | Cumulative score (Vegas) | 0 |

- **Missing value:** the default is 0x0B, from the initial globals (0x1001504).
- **Observed values [Wine-verified]:**
  - 0x03 after choosing Draw One;
  - 0x13 Vegas + draw one;
  - 0x5B Vegas + draw three + Cumulative;
  - 0x2D (written by hand) loaded as None + untimed + outline + draw three.
- **Back:**
  - The registry stores `back − 53`.
  - Load: `clamp(value + 53, 54, 65)`.
  - If the value is missing, the default is `rand() % 12`. That gives 53–64 after +53, clamped to 54–64: back 54 is twice as likely and 65 is never picked.
  - The default is not written until the player confirms the Deck dialog. A fresh install therefore shows a **random back each launch**. **[Wine-verified]**: 58, 62, then the chosen 60 stored as 7.

**Options dialog** (resource 103 "Options", proc 0x100575F):
- Controls: Draw &One/Draw &Three (300/301), St&andard/&Vegas/&None (302–304), T&imed game (305), Status &bar (306), Out&line dragging (307), &Cumulative (308) and its "Score" label (310).
- Cumulative is enabled only while Vegas is selected.
- **OK** applies the options and writes `Options` immediately with `SaveOptions(3)`, not at exit. Mask bit 0 writes `Options` and bit 2 writes `Back`, so only `Options` is written here.
- **What it changes:**
  - Status bar shows or hides at once.
  - **Changing Draw, Timed or Scoring rebuilds the game object if Draw changed, and deals a new game with a fresh seed and a reset score** (`NewDeal(1, 1)`).
  - Changing only Status bar, Outline or Cumulative keeps the current game.
  - **[Wine-verified]**: Draw One → new deal (seed 28172), Vegas → −52.
- **Cancel** discards the changes.

**Deck dialog** (resource 101 "Select Card Back"):
- Twelve owner-drawn buttons, ids 54–65.
- A click selects, OK applies; a double-click applies at once.
- Writes `Back` only (`SaveOptions(4)`).
- It never redeals.

---------------------------------------------------------------------------------------------------

## 11. Quirks and recommendations for the clone

**Replicate:**
- the deal: time-seeded LCG, 5 naive shuffle passes, row-wise deal, top of the array = top of the stock;
- that there is no game number;
- the move legality of §2: kings only to empty columns, single cards to foundations, foundation → tableau allowed, any ace to any empty foundation;
- the drop target = the first column in index order that overlaps the dragged card's rectangle and accepts it;
- draw one / draw three with the packet reversed; the waste fanning rules (§3.2); only the waste top playable;
- recycling and the Vegas pass limits (1 / 3 passes); the X/O empty-stock symbol;
- the exact score tables, the Standard floor at 0, −100 per draw-one recycle, −20 from the 4th draw-three recycle;
- −2 per 10 s; the 30-second minimum for the bonus and `35 × (20000 / s)` integer arithmetic;
- the −2 Undo charge in Standard;
- Vegas −52 per deal and the Cumulative semantics (including the reset on an Options redeal, and no persistence);
- the clock starting at the first press and pausing while minimized;
- single-level undo of two columns; a turned-over card killing Undo;
- double-click to foundations only; right-click / Ctrl+A autoplay with no safety rule; no auto-flip and no auto-complete;
- the bouncing-card win, abortable by input; "Deal Again?";
- the status-bar formats; the registry layout of `Options` and `Back`;
- both cheats; the keyboard model.

**Fix rather than replicate (XP bugs):**
- **Multi-card pick-up from a foundation** (§2.2). Pressing the visible 3-D edge of a foundation pile drags a lower card with everything above it, and the group can land on the tableau as a non-alternating run. **[Wine-verified]** In the clone, foundations should hit-test the top card only.
- **Stale drop target after Esc.** MouseUp with cancel saves an undo record and **does not clear `icolHilight`** (0x100299E skips the reset at 0x10029CF). Pressing Esc while a dragged card is over a valid target therefore:
  - overwrites the undo record with an unchanged position;
  - leaves the target set, so the **next press-and-release without mouse movement moves the clicked card onto that stale column without any legality check**. Move does not call CanDrop.
  - **[Wine-verified]**: after Esc over 6D, clicking KD in another column put KD on 6D.
  - In the clone, clear the target on cancel and validate the move again at drop time.
- **Autoplay during a keyboard drag.** Right-click autoplay is blocked only by mouse capture, so it can run during a keyboard drag. Block it while anything is selected.
- **Undo charges −2 in Standard.** This is probably a side effect of reusing the clock event for "redraw score". It is authentic, so keep it, but note it in Help.
- **Debug commands 1005–1009 and dialogs 102/999** are dead and should not be ported.

**Possibly keep:**
- the Back default bias (54 twice as likely, 65 never) — the clone could use a uniform random back instead;
- `iCurrency`/`sCurrency` being read from the Solitaire key (an artifact of the old WIN.INI `[intl]` port) — the clone can use `$`.
