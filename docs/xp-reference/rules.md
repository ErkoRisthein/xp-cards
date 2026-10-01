# XP FreeCell: game rules and state logic (reverse-engineered)

Target: `freecell.exe`, 32-bit x86, PE timestamp 0x3B7D8476, ImageBase 0x01000000. No symbols were available, so everything below comes from disassembly with capstone and is cited by virtual address (VA).

**Two kinds of evidence:**
- **Static.** The full linear disassembly is in `research/rules/fc.asm`, produced by `research/rules/fcdis.py`. It covers 0x10015F0 to 0x1005780, and the jump table at 0x1002108 is excluded.
- **Empirical.** I ran a private copy of the real game under Wine and drove it with my own helper (`research/rules/rdrv.c` → `research/rules/game/rdrv.exe`, wrapper `research/rules/run.sh`).
  - The copy's window class was patched from `FreeWClass` to `FreeRClass`, so it cannot collide with other agents' FreeCell instances. Nothing else was changed.
  - The helper reads and writes the game's memory (board array, globals), posts clicks, keys and WM_COMMANDs, reads dialog and MessageBox texts, and dumps the registry.
  - Items checked this way are marked **[Wine-verified]**.
  - At the end the registry was restored to its pre-test state.

Card encoding, used throughout and identical to cards.dll `cdtDraw` ordinals:
`card = rank*4 + suit`, where `rank` 0=A … 12=K and `suit` 0=♣ Clubs, 1=♦ Diamonds, 2=♥ Hearts, 3=♠ Spades. Red means suit 1 or 2 (see `CanStack` at 0x1003D8E). `-1` is an empty slot.

---------------------------------------------------------------------------------------------------

## 0. Data model and global map

| VA | Meaning |
|---|---|
| 0x1007500 | `int board[9][21]` (0x54 bytes per column). **Column 0 is the top row**: slots 0–3 are free cells and slots 4–7 are home cells. A home slot holds only its current top card. Columns 1–8 are the tableau, index 0 is the card furthest from the player and the last non-(-1) index is the exposed card. |
| 0x1007140 | Snapshot copy of `board`, taken at the start of every move click (0x1003AFF, 0x1003D31). Used by the "queue then replay" commit. |
| 0x1008360 | `homeRank[4]`, indexed by suit: rank of the top home card, or -1. |
| 0x1008330 | `suitHomeSlot[4]`: which top-row slot (4–7) belongs to each suit, or -1 until that suit's ace lands. |
| 0x100834C | Current game number. 0 means no game, and all mouse and keyboard input is ignored. 1000001 is the "Select Game cancelled" sentinel. |
| 0x10074E0 | Game number whose result (win or loss) was recorded last. Prevents double counting (see §8). |
| 0x100743C | Game in progress. Set to 1 right after a successful deal. Cleared on win (0x10050DF), in the "lose" dialog (0x1002641), and before every re-deal (0x1001FDA). |
| 0x1007800 | Cards Left (52 at deal, −1 per card arriving home during replay, +1 per card undone from home). |
| 0x1007988 / 0x1007128 / 0x1008344 | Selection state (0 or 1), selected column, selected position. |
| 0x10079C0 | Move log, 16 bytes per entry: `{srcCol, srcPos, dstCol, dstPos}`. 0x1007864 is the number of queued entries and 0x1007984 is the number of undoable entries. |
| 0x1007440 / 0x100712C / 0x1007134 | Options: messages / quick / dblclick. |
| 0x1007808 / 0x1008350 | Session won / session lost. 0x1007434 is "session played" and is written but never read. |
| 0x1007438 | "Select-game" flag (§6). |
| 0x1007130 | Cheat state: 0 none, 1 = lose, 2 = win (§10). |
| 0x1008320 | Win display: big smiling king is painted. |
| 0x10077F8 | Result of the MoveCol dialog. |
| 0x1007060 | "Swallow next left click" (set by WM_MOUSEACTIVATE). |

Key functions (VA → my name): 0x1001AF9 MainWndProc · 0x100317F Deal · 0x1003153 RandomGameNumber · 0x1002CFC HitTest · 0x1003777 ClickSelect · 0x1003AD6 ClickMove · 0x1003850 CheckTopRowOrEmptyColMove · 0x1004168 CardsToMove · 0x1004131 MaxMovable · 0x1003D6A Capacity · 0x1003D8E CanStack · 0x1003DDD LastIndex · 0x1004EDF QueueMove · 0x1004FC7 CommitMoves · 0x1004DB2 ReplayMove · 0x1004A57 AnimateCard · 0x100530A MoveRunViaFreeCells · 0x1005397 SuperMove · 0x1003973 SafeToAutoplay · 0x10039F1 Autoplay · 0x100423C CheckNoMoves · 0x1003FCD Undo · 0x1003CFB DblClickToFreeCell · 0x10043C9 OnChar · 0x10033C1/0x100344C right-button peek · 0x1003E12 RecordLoss · 0x1002330 Percent · 0x100239B RegRead · 0x10023E2 RegWrite · 0x1002B37 LoadOptions · 0x1002B90 SaveOptions · dialog procs 0x10024D4 GameNum, 0x100248A MoveCol, 0x1002671 Stats, 0x10029F1 Options, 0x10022AF YouWin, 0x10025A5 YouLose.

**Startup state [Wine-verified].** No game is dealt at launch. The board is empty, the title is "FreeCell" (string 301), the menu bar shows "Cards Left: 0", and clicks are ignored because the game number is 0. Restart (107) and Undo (115) start grayed (MF_GRAYED in the FREEMENU resource). Restart is enabled after the first deal at 0x1002025 and never grayed again.

---------------------------------------------------------------------------------------------------

## 1. Deal algorithm

### 1.1 RNG
The game uses the msvcrt `rand`/`srand` imports at IAT 0x10011EC/0x10011E8, so it is the Microsoft LCG: `seed = seed*214013 + 2531011; return (seed>>16) & 0x7FFF`. The seed is the full 32-bit game number, `srand(gamenum)` at 0x10032D4. No folding is applied to numbers above 32767; they are simply passed to `srand`.

### 1.2 Dealing loop (Deal, 0x100317F, `stdcall(hwnd, gamenum)`)
```c
void Deal(HWND hwnd, int n /*0 = ask*/) {
    if (n == 0) {                                   // Select Game
        gameNum = RandomGameNumber();               // default shown in the dialog
        while (!DialogBoxParam("GameNum", ...));    // re-shows while it returns 0 (invalid input)
        if (gameNum == 1000001) return;             // Cancel: board, title untouched
    } else gameNum = n;
    SetWindowText(hwnd, Format(LoadString(303 /*"FreeCell Game #%d"*/), gameNum));
    memset(board, -1, sizeof board);                // 0xBD dwords
    int deck[52]; for (i = 0; i < 52; i++) deck[i] = i;
    if (gameNum == -1) { ... }                      // fixed layouts, §1.4
    else if (gameNum == -2) { ... }
    else {
        srand(gameNum);
        int left = 52;
        for (i = 0; i < 52; i++) {
            int j = rand() % left;                  // div ebx → edx
            board[1 + (i & 7)][i >> 3] = deck[j];   // ecx = (i&7)*21 + (i>>3), base column 1
            deck[j] = deck[--left];
        }
    }
}
```
Cards go row by row across columns 1–8. Columns 1–4 get 7 cards and columns 5–8 get 6. This is exactly the Rosetta Code algorithm. Evidence: 0x10032D3–0x100330E.

### 1.3 Deals for #1 and #617
These are the outputs of my Python reimplementation (`research/rules/deal.py`). They are byte-identical to Rosetta Code "Deal cards for FreeCell". **[Wine-verified]**: I dumped the real program's `board` via ReadProcessMemory after choosing Select Game 1 and 617, and the columns are identical.
```
Game #1                      Game #617
 JD 2D 9H JC 5D 7H 7C 5H      7D AD 5C 3S 5S 8C 2D AH
 KD KC 9S 5S AD QC KH 3H      TD 7S QD AC 6D 8H AS KH
 2S KS 9D QD JS AS AH 3C      TH QC 3H 9D 6S 8D 3D TC
 4C 5C TS QH 4H AC 4D 7S      KD 5H 9S 3C 8S 7H 4D JS
 3S TD 4S TH 8H 2C JH 7D      4C QS 9C 9H 7C 6H 2C 2S
 6D 8S 8D QS 6C 3D 8C TC      4S TS 2H 5D JC 6C JH QH
 6S 9C 2H 6H                  JD KS KC 4H
```
The real program also matched for #5, #9, #11, #13, #14 and #24782. The layout agent's screenshot of game 1 shows the same cards.

### 1.4 Special games −1 and −2
These are not shuffled. They are hard-coded layouts at 0x1003233–0x10032D1 **[Wine-verified]**:
```
Game #-1                      Game #-2
 AC AD AH AS QC QD QH QS      AS AH AD AC 7S 7H 7D 7C
 3C 3D 3H 3S TC TD TH TS      KS KH KD KC 6S 6H 6D 6C
 5C 5D 5H 5S 8C 8D 8H 8S      QS QH QD QC 5S 5H 5D 5C
 7C 7D 7H 7S 6C 6D 6H 6S      JS JH JD JC 4S 4H 4D 4C
 9C 9D 9H 9S 4C 4D 4H 4S      TS TH TD TC 3S 3H 3D 3C
 JC JD JH JS 2C 2D 2H 2S      9S 9H 9D 9C 2S 2H 2D 2C
 KC KD KH KS                  8S 8H 8D 8C
```
- **−1:** columns 1–4 get `card = row*8 + (col-1)` for rows 0–6. Columns 5–8 get `v-=12; card = v + (col-5)` for rows 0–5, starting from v=56.
- **−2:** row 0 of columns 1–4 is 3,2,1,0 (AS AH AD AC). After that a counter runs down from 51, first through rows 1–6 of columns 1–4 and then through rows 0–5 of columns 5–8.
- **How they are entered:** type −1 or −2 in Select Game. The edit box is read with `GetDlgItemInt(..., bSigned=TRUE)` and has no ES_NUMBER style. The title becomes "FreeCell Game #-1".
- **Stats:** losses on −1/−2 are never recorded, because RecordLoss requires `gamenum > 0` (0x1003E19). **Wins are recorded**: the win path at 0x10050F0 only checks `gamenum != lastRecorded`.
- **Solvability:** widely reported as unwinnable. UNVERIFIED; I did not run a solver.

### 1.5 Accepted range (GameNum dialog proc, 0x10024D4) [Wine-verified]
- **WM_INITDIALOG** centers the dialog over the main window and calls `SetDlgItemInt(203, gameNum, FALSE)`, which shows the random default.
- **OK (id 1):** `v = GetDlgItemInt(203, NULL, TRUE)`. If `v < -2 || v > 1000000`, then `v = 0`. Garbage text also yields 0. The dialog ends with `EndDialog(v != 0)`.
- Deal loops while the result is 0, so the dialog **reappears** for 0, −3, 1000001, "abc", and so on. On re-show the field contains "0" (static: `SetDlgItemInt` with the stored 0; reading another process's edit text cross-process is unreliable under Wine).
- **Valid numbers:** −2, −1 and 1…1,000,000.
- **Cancel (id 2):** sets `gameNum = 1000001`, then `EndDialog(1)`. Deal returns early and the caller skips the redraw (0x1001FF1).
- Static texts: "Select a game number" / "from 1 to 1000000".

### 1.6 Random game number (RandomGameNumber, 0x1003153)
```c
srand(time(NULL)); rand(); rand();
do r = rand(); while (r < 1 || r > 1000000);   // rand() ≤ 32767, so effectively 1..32767
```
- Used for **New Game (F2)** and as the **default value in Select Game**.
- It reseeds from the clock every call, so two New Games in the same second give the same number.
- Observed values: 9369, 27233, 24782, 2938 and others, all ≤ 32767 **[Wine-verified]**.

---------------------------------------------------------------------------------------------------

## 2. Legal moves

### 2.1 Basic predicates
```c
bool CanStack(src, dst)  /*0x1003D8E*/ { return rank(dst) - rank(src) == 1 && isRed(src) != isRed(dst); }
int  LastIndex(col)      /*0x1003DDD*/ ; // -1 if empty
```
- **Free cell destination** (top row slot 0–3): legal only if empty (0x100391E).
- **Home destination** (slot 4–7), 0x100392D:
  - legal if `rank(src)==A` and the clicked home slot is empty, so **any** empty home cell accepts any ace and the player chooses the slot;
  - otherwise legal if the slot's top card has the same suit and rank exactly one lower.
  - When an ace lands, `suitHomeSlot[suit] = slot` (0x1004FB9).
- **Tableau destination:** a non-empty column needs `CanStack(src, bottom)`. An empty column accepts anything.
- **Sources:**
  - an exposed tableau card;
  - a free-cell card;
  - **never a home card** (0x10037D4: top-row pos > 3 cannot be selected).
- Free cell → free cell is legal if the target is empty **[Wine-verified]**.
- Free cell → empty column is a single card with no dialog (0x100395A).

### 2.2 Multi-card ("supermove") capacity: XP uses **(free+1)·(emptyCols+1)**
```c
int Capacity(int f, int e) /*0x1003D6A, recursive*/ { return e == 0 ? f + 1 : Capacity(f, e - 1) + f + 1; } // = (f+1)*(e+1)
int MaxMovable() /*0x1004131*/ { f = #empty top-row slots 0..3; e = #empty columns 1..8; return Capacity(f, e); }
```
It is not `(f+1)·2^e`. The destination column is non-empty in this path, so it is never counted.

**[Wine-verified]** with a 7-card run 9H..3H onto TC, reading the MessageBox text:

| f | e | Message |
|---|---|---|
| 1 | 2 | "That move requires moving 7 cards.You only have enough free space to move 6." (2^e would give 8) |
| 1 | 1 | "...move 4." |
| 4 | 0 | "...move 5." |
| 3 | 1 | Capacity 8, the move succeeded, and it was logged as **19** single-card steps |

### 2.3 How many cards a tableau→tableau move needs (CardsToMove, 0x1004168)
```c
int CardsToMove(int s, int d) {
    if (s == d) return 1;                              // clicking own column → "move to self" = deselect
    int i = LastIndex(s);
    if (board[d][0] == -1) {                           // empty destination: length of ordered run at bottom
        int n = 0; while (i > 0 && CanStack(board[s][i], board[s][i-1])) { i--; n++; } return n + 1;
    }
    int dst = board[d][LastIndex(d)], n = 1, cur = board[s][i];
    if (CanStack(cur, dst)) return 1;
    while (i != 0) {                                   // walk up the ordered run
        if (!CanStack(cur, board[s][i-1])) return 0;   // run broken before a fitting card: illegal
        cur = board[s][--i]; n++;
        if (CanStack(cur, dst)) return n;
    }
    return 0;
}
```
The number of cards moved is forced by the destination card: it is the unique card in the source's ordered run that fits. **XP never moves a shorter sub-run onto a non-empty column.**

### 2.4 Move click flow (ClickMove, 0x1003AD6), when a card is already selected
```c
snapshot = board;                                     // for CommitMoves
HitTest(x, y, &col, &pos);                            // return value IGNORED here (see note)
if (col >= 9) { col = selCol; pos = selPos; }         // miss → treat as clicking the selection (deselect)
if (col == 0) { if (pos > 7) { col = selCol; pos = selPos; } }
else { pos = LastIndex(col); if (pos == -1) pos = 0; }

if (selCol != 0 && col != 0 && board[col][0] != -1) {         // tableau → non-empty tableau
    n = CardsToMove(selCol, col);
    if (n == 0)                 { if (!optMessages) return;  /* selection kept, silent */
                                  MessageBeep(MB_ICONINFORMATION); MessageBox(306 "That move is not allowed.");
                                  QueueMove(selCol, selPos, selCol, selPos); }          // deselect
    else if (n <= MaxMovable()) SuperMove(selCol, col);
    else                        { if (!optMessages) return;
                                  MessageBox(Format(307, n, MaxMovable()));  QueueMove(self→self); }
} else {
    moveColResult = 0;
    if (CheckTopRowOrEmptyColMove(col, pos)) {
        if (moveColResult) MoveRunViaFreeCells(selCol, col);   // "Move column"
        else QueueMove(selCol, selPos, col, pos);              // single card
    } else {
        if (moveColResult == -1) QueueMove(self→self);         // dialog cancelled: deselect, no msg
        else if (!optMessages) return;                         // selection kept, silent
        else { MessageBox(306); QueueMove(self→self); }
    }
}
Autoplay(); CommitMoves(hwnd); selState = 0;
```
Notes:
- **Messages off:** illegal clicks are silent and **keep the selection** [Wine-verified]. Messages on: an info box with a beep, then deselect [Wine-verified].
- **Destination hit-zones are wider than source hit-zones.** HitTest (0x1002CFC) writes `col` before it range-checks x and y. A destination click anywhere in a tableau column's x band therefore targets that column, including:
  - below the last card;
  - over an empty column;
  - in the gap to the right of the column.
  Clicking empty felt elsewhere, such as the margin or the band between the rows, deselects.
- **Quirk, do not replicate:** a miss in the king gap of the top row with y in 1..8 is interpreted as column `y`.

### 2.5 Moves into an empty column, and the "Move to Empty Column..." dialog
CheckTopRowOrEmptyColMove (0x1003850) handles the case where the source is the tableau and the destination is an empty column:
```c
n = CardsToMove(src, dst);              // = length of ordered run at the bottom of src
if (freeCellsEmpty == 0 && n > 1) n = 1;
if (n == 1) return 1;                   // single card, NO dialog
moveColResult = DialogBox("MoveCol");   // 1 = "Move column", 0 = "Move single card", -1 = Cancel
return moveColResult != -1;
```
- The dialog appears only when the run at the bottom is ≥ 2 cards **and** at least one free cell is empty.
- **"Move column" moves `min(run, freeCells+1)` cards** through free cells only, using MoveRunViaFreeCells (0x100530A). Other empty columns are **not** used, and a longer run is moved partially (its bottom part) without any message.
- **"Move single card"** moves 1 card. **Cancel** deselects without a message.

[Wine-verified]:

| Setup | Result |
|---|---|
| Run 7, f=3, e=1 | Dialog appeared; "Move column" moved 4 cards (6S 5H 4S 3H) |
| f=1, e=2 | "Move column" moved 2 |
| Single | Moved 1 |
| Cancel | Nothing moved, deselected |
| f=0 | No dialog, 1 card moved |

The dialog title is "Move to Empty Column..." and the buttons are "Move &column" (201, default), "Move &single card" (202) and "Cancel" (2). It is centered on the main window.

### 2.6 How a supermove is executed (affects Undo and animation)
```c
void MoveRunViaFreeCells(s, d) /*0x100530A*/ {      // uses only free cells
    fc[] = empty free cells, left to right;  k = (s && d) ? CardsToMove(s, d) : 1;
    if (k > fc.count + 1) k = fc.count + 1;
    for (i = 0; i < k-1; i++) QueueMove(s, 0, 0, fc[i]);       // park
    QueueMove(s, 0, d, 0);                                     // base card
    for (i = k-2; i >= 0; i--) QueueMove(0, fc[i], d, 0);      // unpark (reverse)
}
void SuperMove(s, d) /*0x1005397*/ {
    f = #free cells; n = CardsToMove(s, d);
    if (n <= f+1) { MoveRunViaFreeCells(s, d); return; }
    ec[] = empty columns 1..8 in order; j = 0;
    do { MoveRunViaFreeCells(s, ec[j++]); n -= f+1; } while (n > f+1);  // stack f+1 per empty col
    MoveRunViaFreeCells(s, d);
    while (--j >= 0) MoveRunViaFreeCells(ec[j], d);
}
```
Each empty column is used once, as a single level. That is why capacity is (f+1)(e+1).

---------------------------------------------------------------------------------------------------

## 3. Auto-move to home cells ("autoplay")

```c
bool SafeToAutoplay(int c) /*0x1003973*/ {
    if (c == -1) return false;
    if (cheat == 2) return true;                       // Ctrl+Shift+F10 "Abort" (§10)
    r = c/4; s = c%4;
    if (r == 0) return true;                           // ace: always
    if (r == 1) return homeRank[s] == 0;               // two: when its own ace is home
    if (homeRank[s] != r-1) return false;
    if (s is red) { a = homeRank[CLUBS];    b = homeRank[SPADES]; }
    else          { a = homeRank[DIAMONDS]; b = homeRank[HEARTS]; }
    return a != -1 && b != -1 && a >= r-1 && b >= r-1; // both opposite-colour foundations ≥ r-1
}
void Autoplay() /*0x10039F1*/ {
    do { moved = false;
        for (i = 0; i < 4; i++)  if (SafeToAutoplay(board[0][i]))             { QueueMove(0, i, 0, HomeSlot(suit)); moved = true; }
        for (c = 1; c <= 8; c++) if (SafeToAutoplay(board[c][LastIndex(c)])) { QueueMove(c, last, 0, HomeSlot(suit)); moved = true; }
    } while (moved);                                   // cascades until nothing moves
}
int HomeSlot(s) { if (suitHomeSlot[s] == -1) suitHomeSlot[s] = leftmost empty of slots 4..7; return suitHomeSlot[s]; }
```
**The rule, with 1-based ranks:**
- an Ace always goes home;
- a 2 goes home when its Ace is home;
- a card of rank R ≥ 3 goes home when its own foundation is at R−1 **and both opposite-colour foundations are ≥ R−1**.

There is no same-colour condition. Each pass checks the free cells first, then columns 1–8, and passes repeat until nothing moves.

[Wine-verified]:
- With homes at ♣3 ♦2 ♥4 ♠3 and bottoms 3D, 4C, 5H, 4S, one dummy move cascaded 3D→4C→4S→5H home. A 5D in a free cell stayed.
- Aces went to the **leftmost empty** home slot.
- A manually placed AH in slot 7 made 2H follow to slot 7.

**When autoplay runs:**
- After every left-click move action in ClickMove, including a plain deselect, an illegal move that shows a message, and a MoveCol cancel. The "messages off" illegal paths return early and do **not** autoplay.
- After a double-click to a free cell.

**When it does not run:**
- **Not after dealing.** Aces dealt to the bottom stay until the next click [Wine-verified with game 14, where AC stayed at the bottom of column 8].
- **Not after Undo.**

**Animation:** autoplay moves are queued and then replayed with the normal card animation (§4.3), unless Quick play is on.

---------------------------------------------------------------------------------------------------

## 4. Selection model, clicks, double-click, right-click, keyboard

### 4.1 Selecting (ClickSelect, 0x1003777), when nothing is selected
- **Undo is cancelled first.** It sets `undoCount = 0` and **grays Undo** (`EnableMenuItem(115, MF_GRAYED)`), then resets the selection. This happens on every such click, even a miss [Wine-verified: after selecting a card, Undo did nothing].
- Next it runs the hit-test, and the return value must be true (unlike the destination hit-test).
  - **Top row:** an empty slot does nothing, and home slots 4–7 are never selectable. An occupied free cell is selected, and the king turns to look left.
  - **Tableau:** clicking **any** card of a column selects that column's **bottom** card.
- The selected card is redrawn **inverted** (cdtDrawExt mode 2). Only that one card is inverted, never the run above it.
- **Clicking the same card or column again deselects.** Internally this is a "move to self". It triggers autoplay and the no-moves check.

### 4.2 Commit model ("queue then replay")
- QueueMove (0x1004EDF) applies each single-card move to `board` logically and appends it to the move log.
- CommitMoves (0x1004FC7) then:
  - restores `board` from the pre-click snapshot;
  - replays every logged move with animation (ReplayMove, 0x1004DB2). During the replay `cardsLeft` is decremented, and the king bitmap switches to `KINGLEFT` after a move into a free cell and to `KINGBITMAP` after a move home (0x1004EB1);
  - sets `undoCount = queued` and enables Undo, except when exactly one move was logged and `srcCol == dstCol` (a deselect, or **free cell→free cell / free cell→home with no follow-up autoplay**). In that case Undo is grayed [Wine-verified: fc1→fc2 left undo=0];
  - checks for a win (`cardsLeft == 0`), otherwise for no moves (§6).

### 4.3 Animation
- AnimateCard (0x1004A57) glides the card in a straight line. `steps = isqrt(dx²+dy²)/37` (0x1004BD4–0x1004BE5); with Quick play, `steps = 1`, so there is no glide.
- There is no timer or Sleep, so the speed depends on the CPU. The 37 px step is based on 71×96 cards; a scaled clone should scale it.
- Multi-card moves animate as their individual single-card steps through free and empty cells.

### 4.4 Double-click (WM_LBUTTONDBLCLK, 0x1001D79; DblClickToFreeCell, 0x1003CFB)
- **Option on (default):** if a tableau card is selected and the double-click hits the same column, its bottom card moves to the **leftmost empty free cell**. Autoplay and commit follow, and the move is undoable.
- If there is no empty free cell, the second click acts as a normal click, which deselects. No message is shown.
- Double-clicking a free-cell card is just select plus deselect.
- **Option off:** a double-click is two ordinary clicks, which select and then deselect.
- [Wine-verified for all of the above.]
- **Quirk:** if the first click of a double-click performed a move from a tableau column, the DBLCLK is processed as a click **and** re-posted. If that click selects a column, the re-posted DBLCLK then sends its bottom card to a free cell.

### 4.5 Right button (0x10033C1 down / 0x100344C up)
- Right-button down on a **covered** tableau card redraws that card fully on top, which lets you peek at it. Right-button up redraws the column and restores the inverted selection.
- No selection or state change. It does not work on the top row or on the bottom card.

### 4.6 Swallowed activation click
WM_MOUSEACTIVATE with HTCLIENT sets a flag (0x1001C49). The next WM_LBUTTONDOWN or DBLCLK is then ignored (0x1001DFE), so the click that activates the window does nothing.

### 4.7 Keyboard (WM_CHAR → OnChar, 0x10043C9)
Only digits are handled (`isdigit`). Each key is translated into a synthetic WM_LBUTTONDOWN at the target card's position. [Wine-verified for all.]
- **'1'…'8':** click on that column, which selects it or moves to it.
  - If that column is already selected **and has ≥ 2 cards**, timer 3 (300 ms) "peeks" every card of the column from top to bottom, with a wait cursor and input blocked. Afterwards it deselects.
- **'0':**
  - Nothing selected: selects the first occupied free cell.
  - A free cell selected: deselects it (with messages temporarily off) and selects the next occupied free cell to the right, if any.
  - A tableau card selected: moves it to the first empty free cell. If none is empty it targets free cell 0, which is an illegal move.
- **'9':** with a card selected, moves it to its suit's home slot. If the suit has no slot yet, it targets home slot 4 (the leftmost).

### 4.8 Cursor feedback (WM_MOUSEMOVE, 0x10051E7)
While a card is selected:
- the custom "DownArrow" cursor shows over a legal tableau destination;
- IDC_UPARROW shows over a legal free or home cell, and over any **empty** column (shown without a legality check);
- the arrow shows otherwise.

The king looks left while the mouse is over the free cells and right over the home cells.

---------------------------------------------------------------------------------------------------

## 5. Messages (string table ids; exact text)

| When | Id / resource | Text | Style |
|---|---|---|---|
| Illegal destination (messages on) | 306 | `That move is not allowed.` | MessageBeep(0x40) + MB_OK\|MB_ICONINFORMATION, caption 301 "FreeCell" |
| Run longer than capacity (messages on) | 307 | `That move requires moving %u cards.You only have enough free space to move %u.` (**no space after "cards."** in the binary) | same |
| New / Select / Restart / Exit while game in progress | 305 | `Do you want to resign this game?` | MessageBeep(0x20) + MB_YESNO\|MB_ICONQUESTION |
| Stats "Clear" | 309 | `Are you sure you want to delete all statistics?` | same |
| Bitmap creation failure | 304 | `Out of memory.  Close other applications and try again.` | MB_ICONHAND, then quit |
| Run of ≥ 2 to an empty column with ≥ 1 empty free cell | MOVECOL dialog | "Move to Empty Column..." | modal |
| No legal moves | YOULOSE dialog | caption "Game Over", text `Sorry, you lose.There are no more legal moves.\nDo you want to play again?` (no space after "lose."), buttons &Yes(6, default) &No(7), checkbox **&Same game (205, checked by default)** | modal, centered |
| Win | YOUWIN dialog | caption "Game Over", text `Congratulations, you win!\n \nDo you want to play again?`, &Yes(6) &No(7), checkbox **&Select game (208)** | modal, not centered (template x=172, y=85 DLU) |
| Ctrl+Shift+F10 | inline strings | caption `User-Friendly User Interface`, text `Choose Abort to Win,\nRetry to Lose,\nor Ignore to Cancel.` | MB_ABORTRETRYIGNORE\|MB_ICONQUESTION |

The only option that suppresses anything is **"Display messages on illegal moves"**. It suppresses 306 and 307, which are silent with no beep while the selection is kept.

Other strings:
- 301/318 "FreeCell", 302 "by Jim Horne" (ShellAbout: `ShellAbout(hwnd, "FreeCell", "by Jim Horne", icon)`), 303 "FreeCell Game #%d", 308 "Cards Left: %u", 310–313 streak words, 319–321 stats formats (§8).
- 314/315/316/317 ("How to Play", "Commands", "streak", "stype") are unused.

All dialog texts were read from the running game [Wine-verified].

---------------------------------------------------------------------------------------------------

## 6. Game over: win and "no more legal moves"

### 6.1 No-moves detection (CheckNoMoves, 0x100423C)
CommitMoves calls this after every committed action, including a deselect, when `cardsLeft != 0`.
```c
void CheckNoMoves(HWND h) {
    if (cheat != 1) {
        for (i = 0; i < 4; i++)  if (board[0][i] == -1) return;      // any empty free cell → OK
        for (c = 1; c <= 8; c++) if (board[c][0] == -1) return;      // any empty column → OK
        int m = 0, b[9];
        for (c = 1; c <= 8; c++) { b[c] = bottom(c);
            if (rank(b[c]) == 0) m++;                                  // ace (double-counted, harmless)
            if (homeRank[suit(b[c])] == rank(b[c]) - 1) m++; }         // can go home
        for (i = 0; i < 4; i++) if (homeRank[suit(fc[i])] == rank(fc[i]) - 1) m++;
        for (i = 0; i < 4; i++) for (c = 1; c <= 8; c++) if (CanStack(fc[i], b[c])) m++;
        for (d = 1; d <= 8; d++) for (s = 1; s <= 8; s++) if (s != d && CanStack(b[s], b[d])) m++;
        if (m > 1) return;
        if (m == 1) { flashCount = 4; SetTimer(h, 2, 400, 0); return; }  // FlashWindow ×4 "last move" warning
    }
    undoCount = 0; gray Undo;
    DialogBox("YouLose");                                             // its WM_INITDIALOG records the loss
    gameNum = 0; cheat = 0;
}
```
- The no-moves check only counts single-card moves (bottom→bottom, free cell→bottom, →home). That is exact here, because with no free cells and no empty columns the capacity is 1.
- **[Wine-verified]:**
  - A full-free-cell, no-empty-column position with one legal move flashed the title bar (timer 2, counter 4→0).
  - A position with zero moves opened "Game Over" with "Same game" checked, and the loss was written immediately.

### 6.2 Lose dialog (YouLose proc, 0x10025A5)
- **WM_INITDIALOG:** center the dialog, set `inProgress = 0`, **RecordLoss()**, and check "Same game".
- **Yes (or Enter/IDOK):**
  - if "Same game" is checked, post `WM_COMMAND 107` (Restart): the same number, and no resign prompt because the game is not in progress;
  - otherwise post `103` (Select Game) if the select-game flag is set, else `102` (New Game).
- **No / Esc:** `gameNum = 0`. The board stays visible but frozen and further clicks are ignored [Wine-verified].
- [Wine-verified: Yes with "Same game" re-dealt #5.]

### 6.3 Win (inside CommitMoves, 0x1005097)
The win is detected when `cardsLeft == 0` after replay. Then:
- Undo is cleared and grayed, `inProgress = 0`, `cheat = 0`.
- The win is recorded if `gameNum != lastRecorded` (§8).
- The big smiling king is drawn: `KingSmile` StretchBlt to 320×320 at (10, cardH+10), or 256×192 when the screen height is ≤ 350. The window-level flag 0x1008320 keeps repainting it.
- The "YouWin" dialog opens:
  - "Select game" checkbox initial state = the select-game flag;
  - **Yes** saves the checkbox to the flag and posts 103 (checked) or 102 (unchecked);
  - **No / Esc** does nothing more.
- Afterwards `lastRecorded = gameNum; gameNum = 0`, so the table is frozen until the next game.

[Wine-verified: the dialog text, the checkbox defaulting to checked after Select Game and unchecked after New Game, and frozen input after No.]

**Select-game flag (0x1007438):**
- New Game sets it to 0 and Select Game sets it to 1. Restart leaves it unchanged.
- YouWin "Yes" overwrites it with the checkbox state.

---------------------------------------------------------------------------------------------------

## 7. Undo, Restart, New/Select during a game, Exit

### 7.1 Undo (F10, id 115; Undo, 0x1003FCD)
- **Single level.** The unit undone is the whole last action: the user's move (all its supermove steps) **plus every autoplay move that followed it**. The log is replayed backwards with animation, and `cardsLeft`, `homeRank` and `suitHomeSlot` are restored. Undone home aces free their slot.
- [Wine-verified: one Undo restored a free-cell move plus 4 autoplayed aces and twos. A second Undo did nothing.]
- **Enabled** after any committed action that logged a real move (§4.2).
- **Disabled** by:
  - the next selecting click;
  - Undo itself;
  - win;
  - lose;
  - a lone free cell→free cell or free cell→home move.
- **No autoplay after Undo.** Autoplay is retriggered on the next click, so the undone cards fly home again as soon as you make any move or deselect.
- **Quirk:** `undoCount` and the Undo menu state are **not reset when a new game is dealt**. Undo after F2 replays the previous game's last action onto the new board. In the test it did nothing visible because the record moved a free-cell card and the new board's free cell was empty, but it can corrupt a new deal. **Do not replicate; reset Undo on deal.**

### 7.2 New Game (F2/102), Select Game (F3/103), Restart (107) — common path 0x1001F0D
```c
if (cmd == NewGame) n = RandomGameNumber();
if (inProgress) { if (MessageBox(305, YESNO) == IDNO) return; RecordLoss(); }
n = (cmd == Restart) ? (inProgress ? gameNum : lastRecorded) : (cmd == Select ? 0 : n);
selectFlag = (cmd == NewGame) ? 0 : (cmd == Select) ? 1 : selectFlag;
homeRank[] = suitHomeSlot[] = -1; selCol = -1; inProgress = 0; selState = 0; queued = 0;
Deal(hwnd, n);
if (gameNum == 1000001) return;                        // Select cancelled
InvalidateRect; cardsLeft = 52; inProgress = 1; enable Restart; redraw "Cards Left"; king = facing right; winDisplay = 0;
```
- **"In progress" starts at the deal**, so resigning right after a deal with zero moves still asks for confirmation and counts a loss.
- **Restart** re-deals the same number. When in progress it is a resign plus a loss.
- **Select Game:** the resign prompt comes **before** the number dialog. If the player then cancels, the loss stays counted, `inProgress = 0`, the old board stays playable, `gameNum = 1000001` and the title is unchanged.
  - **Quirk:** `homeRank` and `suitHomeSlot` were already reset, which breaks home moves on that board. [Wine-verified: lost 4→5, gameNum = 1000001.] Recommend not replicating; restore the old game on cancel.
- **New Game when not in progress:** no prompt [Wine-verified].
- **Exit (108):** sends WM_CLOSE. WM_CLOSE (0x1001C54) asks 305 if a game is in progress (No aborts the close), records the loss, **saves options** (0x1002B90), then destroys the window [Wine-verified: loss counted on close].

---------------------------------------------------------------------------------------------------

## 8. Statistics

**Registry key:** `HKCU\Software\Microsoft\Windows\CurrentVersion\Applets\FreeCell`, opened with RegCreateKeyW per operation. **All values are REG_BINARY (type 3), 4 bytes, little-endian DWORD** (RegSetValueExW at 0x1002402, RegQueryValueExW at 0x10023CE) [Wine-verified]. A missing value reads as the caller's default.

| Value | Meaning |
|---|---|
| `won` | Total games won |
| `lost` | Total games lost |
| `wins` | Longest winning streak |
| `losses` | Longest losing streak |
| `streak` | Current streak length |
| `stype` | Current streak type: **1 = wins, 0 = losses** |
| `AlreadyPlayed` | 1 after the one-time migration |
| `messages`, `quick`, `dblclick` | Options (§9) |

Session counters live only in memory (0x1007808 won, 0x1008350 lost).

**First run (InitInstance, 0x1001732):** if `AlreadyPlayed` is absent, the game reads `lost, won, losses, wins, streak, stype` with `GetPrivateProfileIntW("FreeCell", key, 0, "entpack.ini")` (the old Entertainment Pack ini), writes them to the registry, and sets `AlreadyPlayed = 1`.

**RecordLoss (0x1003E12):**
```c
if (gameNum > 0 && gameNum != lastRecorded) {
    lost++; sessionLost++;
    if (RegRead("stype", 1) == 1) { stype = 0; streak = 1; } else streak = RegRead("streak", 0) + 1;
    if (RegRead("losses", 0) < streak) losses = streak;
}
lastRecorded = gameNum;
```
**RecordWin (0x10050D8):**
```c
if (gameNum != lastRecorded) {            // NOTE: no gameNum>0 test → −1/−2 wins count
    won++; sessionWon++;
    if (RegRead("stype", 0) == 0) { stype = 1; streak = 1; } else streak = RegRead("streak", 0) + 1;
    if (RegRead("wins", 0) < streak) wins = streak;
}
lastRecorded = gameNum;
```
**When results are recorded:**
- a loss: resign via New, Select, Restart or Exit (if confirmed), and when the YouLose dialog opens;
- a win: when the last card reaches home.
- **The same game number is never recorded twice in a row.** Restarting and resigning #9 again did not add a loss, and winning #5 right after losing #5 did not add a win [Wine-verified].

**Percentage (Percent(won, lost), 0x1002330):**
```c
t = won + lost; if (t == 0) return 0;
p = (won*200 + t) / (2*t);           // round half up
if (p >= 100 && lost != 0) p = 99;   // 100% only with zero losses
```
[Wine-verified: 1/3 → 25%, 199/1 → 99%, 2/1 → 67%, 1/7 → 13%.]

**Stats dialog ("FreeCell Statistics", 0x1002671):**
- Three static boxes, 212/213/214, are filled with wsprintf of strings 319/320/321:
  - `This session\t\t\t%u%%\n\twon:\t\t%u \n\tlost:\t\t%u%\n\n`
  - `Total\t\t\t\t%u%%\n\twon:\t\t%u \n\tlost:\t\t%u%\n\n`
  - `Streaks\n\twins:\t\t%u \n\tlosses:\t\t%u% \n\tcurrent:\t\t%s`
- The stray `%` before `\n` and before the space disappears in output.
- "current" is `"0"` if streak = 0, `"1 win"`/`"1 loss"` (310/311) if 1, otherwise `"%u wins"`/`"%u losses"` (312/313), chosen by `stype == 1`.
- Rendered examples [Wine-verified]: "This session 25% / won: 1 / lost: 3", "current: 5 losses".
- **Clear (207):** asks 309. Yes deletes `won, lost, wins, losses, streak, stype` (not AlreadyPlayed), zeroes the session counters, and **closes the dialog** with EndDialog(0). No leaves the dialog open. OK/Cancel close it.

---------------------------------------------------------------------------------------------------

## 9. Options (dialog 505 "FreeCell Options", proc 0x10029F1)

| Checkbox | Default | Registry (only non-default values are stored) |
|---|---|---|
| 209 "Display &messages on illegal moves" | **on** | `messages`=0 when off; the value is deleted when on |
| 210 "&Quick play (no animation)" | **off** | `quick`=1 when on; deleted when off |
| 211 "&Double click moves card to free cell" | **on** | `dblclick`=0 when off; deleted when on |

- Loaded at WM_CREATE (0x1002B37) with defaults messages=1, quick=0, dblclick=1.
- OK copies the checkboxes into memory immediately. **They are written to the registry only on WM_CLOSE** (0x1002B90, followed by RegFlushKey).
- [Wine-verified: after unchecking, checking and unchecking the three boxes and exiting, the registry held messages=0, quick=1, dblclick=0. A relaunch loaded them. Exiting with defaults deleted them.]
- Quick play affects only the animation step count.

---------------------------------------------------------------------------------------------------

## 10. Other behaviour

**Cards Left (0x1002E86).** "Cards Left: %u" is drawn right-aligned in the **menu bar** (non-client area) in the menu font, in COLOR_MENUTEXT. The previous text is erased in COLOR_MENU.
- It shows the number of cards **not on the home cells**: 52 after a deal, 0 at startup and after a win.
- It changes card by card during the replay animation, and goes back up on Undo.

**Accelerators (FREEMENU):** F1 and Shift+F1 → Help Contents (106); F2 New (102); F3 Select (103); F4 Statistics (105); F5 Options (109); F10 Undo (115); **Ctrl+Shift+F10 → 114 (cheat)**.

**Menu:**
- Game: &New Game F2, &Select Game F3, &Restart Game (initially grayed), —, S&tatistics... F4, &Options... F5, —, &Undo F10 (initially grayed), —, E&xit.
- Help: &Contents F1, &Search for Help on..., &How to Use Help, —, &About FreeCell...
- Help uses HtmlHelp from hhctrl.ocx ordinal 14:
  - Contents opens `freecell.chm` (exe path with .chm) with HH_DISPLAY_TOPIC;
  - Search uses HH_DISPLAY_INDEX;
  - How to Use Help opens `NTHelp.chm`.

**Cheat Ctrl+Shift+F10 (id 114, 0x10020B9).** A MessageBox (§5) sets `cheat`: Abort = 2 (win), Retry = 1 (lose), Ignore = 0. Nothing happens until the next committed click.
- **Win (2):** SafeToAutoplay returns true for every card, so the whole deck flies "home" and a counted win follows [Wine-verified: the win dialog appeared and won+1]. Cards are dropped onto their suit slots regardless of order, so the home cells show odd top cards. That is cosmetic.
- **Lose (1):** CheckNoMoves goes straight to the YouLose dialog, and the loss is counted [Wine-verified].
- Reset on win and lose.

**Window size limit.** WM_GETMINMAXINFO (0x1001C0A) limits the tracking width to 640 px when the screen is wider than 640. Maximize still fills the screen. This is layout, noted for completeness.

**Timers:**
- 2 = flash the "one move left" warning, 400 ms × 4;
- 3 = keyboard column peek, 300 ms per card.

**"Out of memory" (304)** appears only if the off-screen bitmaps fail at WM_CREATE.

---------------------------------------------------------------------------------------------------

## 11. Recommendations for the clone (where XP behaviour is buggy)

**Replicate:**
- the deal, including −1/−2;
- the capacity formula (f+1)(e+1);
- the forced run length, with no partial moves onto non-empty columns;
- the MoveCol dialog semantics and the f+1 limit;
- the autoplay rule and its triggers, including no autoplay after the deal or Undo;
- single-level "action" undo, including autoplay;
- messages-off keeping the selection;
- one-move flash;
- the stats formulas, the REG_BINARY encoding and the same-number rule;
- option defaults and persistence;
- keyboard digits;
- the Ctrl+Shift+F10 cheat.

**Fix rather than replicate:**
- stale Undo after a new deal (§7.1);
- the Select-cancel corrupting the home arrays (§7.2);
- the top-row y < 9 miss quirk (§2.4);
- the DBLCLK repost oddity (§4.4);
- the cheat's cosmetic home-cell garbage.

**Possibly keep:** the texts with missing spaces ("lose.There", "cards.You") are what the XP binary actually contains.
