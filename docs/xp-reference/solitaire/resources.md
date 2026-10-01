# sol.exe / cards.dll (XP Solitaire): resources and strings, extracted in full

Sources: `/Users/erko/IdeaProjects/xp-cards/sol.exe` (56,832 bytes, SHA-1 `849abaa9c524ff6da8891c3da01350c18ea1a1d4`, PE32 i386, TimeDateStamp 0x3B7D8480 = 2001-08-17 20:54:24 UTC, ImageBase 0x01000000, entry 0x01005F85, linker 7.0, subsystem GUI 4.0, OS version 5.1, Characteristics 0x010F, DllCharacteristics 0x8000 (TERMINAL_SERVER_AWARE), CodeView NB10 `sol.pdb` signature 0x3B7D8480 age 1; no public symbols were available) and `cards.dll` (359,936 bytes, SHA-1 `0a19ac47361a87029a3df157d23204a4461f4874`, TimeDateStamp 0x3B7DFE43, ImageBase 0x6FC10000; same file as the one documented in `../resources.md` section 10).
Sections of sol.exe: `.text` VA 0x1001000 (vsize 0x5D2E; the IAT sits at its start), `.data` VA 0x1007000 (vsize 0x38C, only 0x200 bytes initialised), `.rsrc` VA 0x1008000 (vsize 0x7880). No `.reloc`.
Tools: pefile with custom decoders for menus, dialogs, accelerators and string blocks; capstone for a linear disassembly annotated with imports and strings; Wine 11.18 (macOS, private prefix, Windows version set to XP, `WINEDLLOVERRIDES=cards=n,b` so that the **native XP cards.dll** is used, not Wine's builtin one) driven by a local copy of `tools/wine/fcdrive` for live captures.
Scratch (scripts, captures, prefix): `/private/tmp/claude-502/-Users-erko-IdeaProjects-xp-cards/4b6e3af5-e647-41ff-a37b-eccdd0578cdc/scratchpad/sol-research/` (below: `<sr>`). Scripts: `<sr>/rsx/scripts/{sdis.py, rng.sh, sheets.py, mock.py}`; Wine scripts `<sr>/rsx/t1.txt`..`t13.txt`; the driver copy is `<sr>/rsx/drv/` with four local-only commands added (`capture_dialog_client`, `dialog_geom`, `child_geom`, `sendmsg <msg> <wparam|menu> <lparam>`). Images: `<sr>/res/` (called `res/` below).
All VAs are absolute (ImageBase 0x01000000 for sol.exe, 0x6FC10000 for cards.dll). "RVA" means resource data RVA.
Strings are written in **C-literal notation**: `\t` = TAB (0x09), `\n` = LF (0x0A), `\\` = one backslash. Nothing else is escaped. Every resource is LANG 0x0409 (en-US).
Marks: **[Wine-verified]** = observed in the Wine run with the original binaries; **[code]** = read from the disassembly; **UNVERIFIED** = inferred, not proven.

---------------------------------------------------------------------------------------------------

## 0. Key takeaways for the clone

* **Main menu (RT_MENU 1)**: `&Game` = `"&Deal\t F2"` (1000), separator, `"&Undo"` (1001, no shortcut text), `"De&ck..."` (1002), `"&Options..."` (1003), separator, `"E&xit"` (1004); `&Help` = `"&Contents\t F1"` (0xFFE2), `"&Search for Help on..."` (0xFFE3), `"&How to Use Help"` (0xFFE4), separator, `"&About Solitaire"` (2000). **No item is grayed in the template**; graying is done at `WM_INITMENU` (section 2.3). Both shortcut texts have a **space after the TAB**. There is no Statistics item and no MF_HELP right-justification.
* **Accelerators "HIDDENACCEL"** (2 entries): **F2** -> 1000 (Deal), and **Alt+Shift+2** -> 1010 ("Force a win", no menu item: runs the win cascade immediately) [Wine-verified]. **F1 is not an accelerator**: it reaches the program as `WM_HELP` and opens Contents.
* **Window**: class `"Solitaire"` (CS_DBLCLKS|CS_BYTEALIGNWINDOW), background brush **RGB(0,128,0)** (white on a 2-colour display), title always `"Solitaire"` (no game number), style WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN, initial **client 585 x 384** (= 7*71 + 8*11 wide, 4*96 high) [Wine-verified]. Menu bar text is not drawn by the program.
* **Status bar**: a private child window of class `"Stat"` (white, WS_BORDER, height = system-font tmHeight + 2 = **18 px**, placed 1 px outside the left/right/bottom client edges) [Wine-verified]. Left: menu-help text at x=4 (the string whose id equals the highlighted menu command). Right, right-aligned 4 px from the edge, font **MS Shell Dlg 9 pt bold**: `"Score: "` + score + `" "` then `"Time: "` + seconds; e.g. `"Score: 0 Time: 0"`. Negative scores are **red RGB(255,0,0)**; Vegas scores carry a `$` (e.g. `"Score: -$52 Time: 0"`) [Wine-verified].
* **Three dialogs are used**: 103 **"Options"** (Draw One/Three, Scoring Standard/Vegas/None, Timed game, Status bar, Outline dragging, Cumulative Score), 101 **"Select Card Back"** (12 owner-drawn back buttons in a 6x2 grid), and Windows' `ShellAboutW` box. Dialogs 102 (`Set Next Game Number`) and 999 (`Assertion Failure`) exist in the resources but are **unreachable** in this retail build. All use `MS Shell Dlg` 8 pt; dialogs are **not centred** (template position relative to the main client area) [Wine-verified].
* **Message boxes** (caption always `"Solitaire"`): `"Deal Again?"` (MB_YESNO|MB_ICONEXCLAMATION, after the win cascade; Yes = Deal), `"Unable load Windows Help application"` (sic, no "to"), `"Out of memory"`, and the full-drag memory warning (string 303). All others use MB_ICONEXCLAMATION|MB_OK.
* **Settings**: `HKCU\Software\Microsoft\Solitaire`, values **REG_DWORD** `Options` (bitfield, section 9.2; default 0x0B = Status bar + Timed + Draw Three + Standard) and `Back` (back id - 53, i.e. 1..12; default random). Written only when Options/Deck are confirmed with OK. No statistics are stored. `iCurrency` (REG_DWORD 0-3) is honoured for the Vegas `$` placement; **`sCurrency` crashes the program** if present with <= 10 bytes (bug, section 9.3) [Wine-verified].
* **Help**: `HtmlHelpA` (dynamically loaded, ordinal 14 of hhctrl.ocx) with the bare file name **`sol.chm`** (Contents = HH_DISPLAY_TOPIC, Search = HH_DISPLAY_INDEX) and `NTHelp.chm` (How to Use Help). Dialog "?" help uses `WinHelpW("sol.hlp")`.
* **cards.dll**: sol.exe imports **cdtInit, cdtDraw, cdtDrawExt, cdtTerm only. cdtAnimate is not imported**, and in this cards.dll it is a stub (`xor eax,eax / inc eax / ret 14h`). **No card back is animated in XP Solitaire.** The 12 backs (ids 54-65) are the new 24-bpp photo backs; the sprite bitmaps 678-684 (robot, castle bats, beach sun, ace-up-the-sleeve hand) are dead leftovers of the Windows 3.x-2000 animated backs (section 10.4).
* **Back dialog order** (row-major): `54 55 60 61 58 59 / 56 57 62 63 64 65`, i.e. sky, aqua glass, mosaic, purple, red swirl, palms / fish, frog, dunes+moon, astronaut, stripes, race cars.

---------------------------------------------------------------------------------------------------

## 1. Resource directory of sol.exe

| Type | Name/ID | Lang | Size (bytes) | Data RVA |
|---|---|---|---|---|
| RT_ICON (3) | 1..8 | 0x409 | 744, 296, 3752, 2216, 1384, 9640, 4264, 1128 | 0x9608, 0x98F0, 0x9A18, 0xA8C0, 0xB168, 0xB6D0, 0xDC78, 0xED20 |
| RT_MENU (4) | 1 | 0x409 | 314 | 0x8B78 |
| RT_DIALOG (5) | 101 (0x65) | 0x409 | 490 | 0x8CC8 |
| RT_DIALOG (5) | 102 (0x66) | 0x409 | 262 | 0x9158 |
| RT_DIALOG (5) | 103 (0x67) | 0x409 | 670 | 0x8EB8 |
| RT_DIALOG (5) | 999 (0x3E7) | 0x409 | 934 | 0x9260 |
| RT_STRING (6) | blocks 7, 13, 19, 63, 64, 126, 4095 (ids 100-108, 200-205, 300-303, 1000-1010, 2000, 65506-65508) | 0x409 | 292, 110, 480, 330, 162, 62, 208 | 0xF200, 0xF328, 0xF398, 0xF578, 0xF6C8, 0xF840, 0xF770 |
| RT_ACCELERATOR (9) | "HIDDENACCEL" | 0x409 | 16 | 0x8CB8 |
| RT_GROUP_ICON (14) | 500 (0x1F4) | 0x409 | 118 | 0xF188 |
| RT_VERSION (16) | 1 | 0x409 | 880 | 0x8808 |
| RT_MANIFEST (24) | 1 | 0x409 | 661 | 0x8570 |

There are **no RT_BITMAP and no RT_CURSOR resources** in sol.exe: every image comes from cards.dll, and the cursor is the system IDC_ARROW. The accelerator name is stored upper-case and loaded with the UTF-16 literal `"HiddenAccel"` (VA 0x100124C); the menu, the dialogs and the icon are loaded by number (`lpszMenuName = MAKEINTRESOURCE(1)`, dialogs 101/103, icon 500).

---------------------------------------------------------------------------------------------------

## 2. MENU 1 (standard MENU template: version 0, header size 0)

| # | Kind | Text (verbatim) | Cmd ID | Flags (raw) |
|---|---|---|---|---|
| 0 | **POPUP** | `"&Game"` | - | 0x0010 MF_POPUP |
| 1 | MENUITEM | `"&Deal\t F2"` | 1000 (0x3E8) | 0x0000 |
| 2 | SEPARATOR | | 0 | 0x0000 |
| 3 | MENUITEM | `"&Undo"` | 1001 (0x3E9) | 0x0000 |
| 4 | MENUITEM | `"De&ck..."` | 1002 (0x3EA) | 0x0000 |
| 5 | MENUITEM | `"&Options..."` | 1003 (0x3EB) | 0x0000 |
| 6 | SEPARATOR | | 0 | 0x0000 |
| 7 | MENUITEM | `"E&xit"` | 1004 (0x3EC) | 0x0080 MF_END |
| 8 | **POPUP** | `"&Help"` | - | 0x0090 MF_POPUP\|MF_END |
| 9 | MENUITEM | `"&Contents\t F1"` | 65506 (0xFFE2) | 0x0000 |
| 10 | MENUITEM | `"&Search for Help on..."` | 65507 (0xFFE3) | 0x0000 |
| 11 | MENUITEM | `"&How to Use Help"` | 65508 (0xFFE4) | 0x0000 |
| 12 | SEPARATOR | | 0 | 0x0000 |
| 13 | MENUITEM | `"&About Solitaire"` | 2000 (0x7D0) | 0x0080 MF_END |

No MF_GRAYED, MF_CHECKED, MF_HELP or MF_RIGHTJUSTIFY anywhere: `Help` sits right after `Game`. Note `"&About Solitaire"` has **no** trailing `"..."`, `"&Undo"` has no shortcut, and `"&Deal\t F2"` / `"&Contents\t F1"` contain TAB + space. [Wine-verified: GetMenuString returned exactly these texts.]

Equivalent RC:
```
1 MENU
POPUP "&Game"
    MENUITEM "&Deal\t F2", 1000
    MENUITEM SEPARATOR
    MENUITEM "&Undo", 1001
    MENUITEM "De&ck...", 1002
    MENUITEM "&Options...", 1003
    MENUITEM SEPARATOR
    MENUITEM "E&xit", 1004
POPUP "&Help"
    MENUITEM "&Contents\t F1", 0xFFE2
    MENUITEM "&Search for Help on...", 0xFFE3
    MENUITEM "&How to Use Help", 0xFFE4
    MENUITEM SEPARATOR
    MENUITEM "&About Solitaire", 2000
```

### 2.1 Command ID map (WM_COMMAND handling in the main window procedure 0x10016BD, switch at 0x1001968)

| ID | Menu item | Handler VA | What it does [code] |
|---|---|---|---|
| 1000 | Deal (F2) | 0x10019AA | `fn 0x1001468(TRUE, 0)`: `seed = time(NULL) & 0x7FFF` (kept in 0x1007344), `srand(seed)`, then game message 8 (deal). No confirmation, even mid-game. |
| 1001 | Undo | 0x100199F | game message 9 (undo). One level: after a stock click Undo is enabled, after one Undo it is grayed again [Wine-verified]. |
| 1002 | Deck... | 0x1001995 -> 0x1005C5D | `DialogBoxParamW(hInst, MAKEINTRESOURCE(101), hwnd, 0x1005AB4, 0)` |
| 1003 | Options... | 0x100198B -> 0x1005A1F | `DialogBoxParamW(hInst, MAKEINTRESOURCE(103), hwnd, 0x100575F, 0)`; if it returns TRUE (Draw, Timed or Scoring changed) -> `fn 0x1001468(TRUE, 1)` = **new random deal without asking** |
| 1004 | Exit | 0x10019B8 | `PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0)`. There is no "resign?" confirmation anywhere (WM_CLOSE goes to DefWindowProc) [Wine-verified: closes at once]. |
| 1005-1009 | (none) | - | Not in the menu or accelerators; only their status-bar strings survive (debug-build commands: set game number, print column counts, assertion, "Heck, I don't know", screen-shot mode). Unhandled -> DefWindowProc. |
| 1010 | (accelerator Alt+Shift+2 only) | 0x1001A35 | game message 0x12 = force a win (Klondike handler 0x1004FB6): the cards fly to the foundations and the win cascade starts [Wine-verified]. |
| 2000 | About Solitaire | 0x1001A05 | `ShellAboutW(hwnd, L"Solitaire" (string 100), LoadString(108) = L"Developed for Microsoft by Wes Cherry", hIcon 500)`. Windows builds the caption `"About Solitaire"` [Wine-verified]. |
| 0xFFE2 | Contents | 0x10019F6 -> 0x100149D | `HtmlHelpA(GetDesktopWindow(), "sol.chm", HH_DISPLAY_TOPIC (0), 0)` |
| 0xFFE3 | Search for Help on... | same | `HtmlHelpA(GetDesktopWindow(), "sol.chm", HH_DISPLAY_INDEX (2), 0)` |
| 0xFFE4 | How to Use Help | same | `HtmlHelpA(GetDesktopWindow(), "NTHelp.chm", HH_DISPLAY_TOPIC, 0)` |

The help function 0x100149D loads string 105 (`"sol.chm"`) with **LoadStringA** (100-byte buffer) because HtmlHelpA is ANSI; if HtmlHelpA returns NULL it shows string 301 (`"Unable load Windows Help application"`, MB_ICONEXCLAMATION) [Wine-verified: with no sol.chm present]. Ids 0xFFE1..0xFFE4 are routed there; 0xFFE1 has no item.

### 2.2 Other main-window messages relevant to resources [code]

| Message | Handling |
|---|---|
| WM_HELP (0x53) | same as Contents (help fn with 0xFFE2). This is how **F1** works. |
| WM_MENUSELECT (0x11F) | if the flags contain MF_POPUP (0x10), MF_SYSMENU (0x2000) or MF_SEPARATOR (0x800) -> clear the status text; else `LoadStringW(item id, buf, 60)` and draw it at the status bar's left (fn 0x1005E8E -> 0x1005DED). So the **status-bar menu help string id = the command id** (strings 1000-1004, 2000, 65506-65508). [Wine-verified: `"Deal a new game"`, `"Force a win"`, `"Search the Help Engine for a specific topic"`; `res/wine_statusbar_menuhelp_x2.png`] |
| WM_INITMENU (0x116) | see 2.3 |
| registered message `"CardDraw"` (string 103) | wParam 1: returns `MAKELONG(dxCard, dyCard)`; wParam 2: calls `cdtDraw(p[0], p[1], p[2], p[3], p[4], p[5])` with the six DWORDs at lParam (hdc, x, y, cd, md, rgbBk) and returns its result; wParam 3: `PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0)`, returns 1. An inter-module card-drawing hook (its client, presumably the old help/tutorial, is UNVERIFIED). |
| WM_DESTROY | `KillTimer(hwnd, 666)`, game message 1, frees the drag buffers, `cdtTerm()`, `DeleteObject(green brush)`, `PostQuitMessage(0)` |
| WM_TIMER | timer id **666** (0x29A), **250 ms**, TimerProc 0x100142B (game message 0x11). Created at start-up with `SetTimer(hwnd, 666, 250, 0x100142B)`. |

### 2.3 Menu graying (WM_INITMENU at 0x10018E6) [code, Wine-verified with `sendmsg 0x116`]

`EnableMenuItem` is called on every WM_INITMENU (and therefore also before an accelerator is executed):

| Item | Grayed (MF_GRAYED\|MF_DISABLED = 3) when |
|---|---|
| Undo (1001) | there is nothing to undo (`game+4 == 0`) **or** a card is being moved (`game+0x58 != -1`) |
| Deal (1000), Deck... (1002), About Solitaire (2000) | a card is being moved (`game+0x58 != -1`) |
| Options..., Exit, Help items | never |

Wine check: fresh game -> Undo grayed, others enabled; during a held left-button drag -> Deal, Undo, Deck, About grayed and **Options still enabled**; after one stock click -> Undo enabled.

---------------------------------------------------------------------------------------------------

## 3. ACCELERATORS "HIDDENACCEL" (2 entries, 8 bytes each: WORD fFlags, WORD key, WORD cmd, WORD pad)

Raw: `17 00 32 00 f2 03 00 00 81 00 71 00 e8 03 00 00`

| # | fFlags (raw) | Decoded | Key | Cmd ID |
|---|---|---|---|---|
| 0 | 0x17 | FVIRTKEY\|FNOINVERT\|FSHIFT\|FALT | 0x32 (VK `'2'`, main keyboard row) | 1010 (Force a win) |
| 1 | 0x81 | FVIRTKEY\|(last entry) | 0x71 VK_F2 | 1000 (Deal) |

Loaded at 0x1001E48 with `LoadAcceleratorsW(hInst, L"HiddenAccel")`, used with `TranslateAcceleratorW(hwnd, ...)` in the message loop (0x1001EE0). F2 has no FNOINVERT, so the `Game` title is highlighted briefly when F2 is used (standard TranslateAccelerator behaviour), and F2 is ignored while Deal is grayed. Alt+Shift+2 has no menu item and works at any time [Wine-verified: posting WM_SYSKEYDOWN `'2'` with Alt+Shift held started the win cascade]. **F1 is not in the table** (see WM_HELP). There is no accelerator for Undo, Deck, Options or Exit.

---------------------------------------------------------------------------------------------------

## 4. STRING TABLE (RT_STRING blocks 7, 13, 19, 63, 64, 126, 4095; 39 strings)

| ID | Hex | Text (verbatim) | Used at [code] |
|---|---|---|---|
| 100 | 0x64 | `"Solitaire"` | 0x1001C3A -> buffer 0x1007320 (10 chars): window title (CreateWindowExW 0x1001DCD), ShellAbout app name, caption of every message box. (Also pushed as the ignored "section" argument of the registry helpers, 0x100154D etc.) |
| 101 | 0x65 | `"Score: "` | 0x1001C48 -> buffer 0x10072A0 (50 chars), drawn at 0x1005402 |
| 102 | 0x66 | `"Time: "` | 0x100526B (status bar) |
| 103 | 0x67 | `"CardDraw"` | 0x1001C56 -> `RegisterWindowMessageW` (0x1001C65) |
| 104 | 0x68 | `"Deal Again?"` | 0x10030D0 (message box after a win) |
| 105 | 0x69 | `"sol.chm"` | 0x10014AA (LoadStringA, help) |
| 106 | 0x6A | `"Press Esc or a mouse button to stop..."` | 0x1004E74 (status bar during the win cascade) |
| 107 | 0x6B | `"Bonus: "` | 0x1004E33 (status bar during the win cascade, Standard scoring only) |
| 108 | 0x6C | `"Developed for Microsoft by Wes Cherry"` | 0x1001A07 (About box) |
| 200 | 0xC8 | `"Back"` | registry value name: read 0x10015D3, written 0x10016AE |
| 201 | 0xC9 | `"Options"` | registry value name: read 0x1001548, written 0x1001692 |
| 202 | 0xCA | `"Bitmap"` | **no reference** (legacy background-bitmap option) |
| 203 | 0xCB | `"iCurrency"` | registry value name, read 0x10015F4 |
| 204 | 0xCC | `"sCurrency"` | registry value name, read 0x1001610 (buggy, section 9.3) |
| 205 | 0xCD | `"intl"` | pushed as the "section" argument for 203/204 (0x10015F9), which the registry helpers **ignore** (a WIN.INI `[intl]` leftover) |
| 300 | 0x12C | `"Out of memory"` | 0x1001B3F -> buffer 0x1007220, shown by fn 0x10023E2 |
| 301 | 0x12D | `"Unable load Windows Help application"` | 0x10014F6 (help failed) |
| 302 | 0x12E | `"Unable to load bitmap; do you want to use a green background?"` | **no reference** (0x12E is only used as the Standard radio-button id) |
| 303 | 0x12F | `"Insufficient memory to display card faces when cards move;\n\nselect the Outline dragging box from the Options menu."` | 0x1002860 (allocation of the full-drag bitmaps failed) |
| 1000 | 0x3E8 | `"Deal a new game"` | status-bar menu help (WM_MENUSELECT) |
| 1001 | 0x3E9 | `"Undo last action"` | menu help |
| 1002 | 0x3EA | `"Choose new deck back"` | menu help |
| 1003 | 0x3EB | `"Change Solitaire options"` | menu help |
| 1004 | 0x3EC | `"Exit Solitaire"` | menu help |
| 1005 | 0x3ED | `"Set game number"` | orphan (debug command; see dialog 102) |
| 1006 | 0x3EE | `"Print # of cards in each col"` | orphan (debug) |
| 1007 | 0x3EF | `"Assertion failure"` | orphan (debug; see dialog 999) |
| 1008 | 0x3F0 | `"Heck, I don't know"` | orphan (debug) |
| 1009 | 0x3F1 | `"Configure Solitaire for screen shots"` | orphan (debug) |
| 1010 | 0x3F2 | `"Force a win"` | menu help id of the hidden command (only shown if a menu item 1010 were ever selected; [Wine-verified] via a synthetic WM_MENUSELECT) |
| 2000 | 0x7D0 | `"About Solitaire"` | menu help |
| 65506 | 0xFFE2 | `"Index of Solitaire help topics"` | menu help (Contents) |
| 65507 | 0xFFE3 | `"Search the Help Engine for a specific topic"` | menu help |
| 65508 | 0xFFE4 | `"Help using help"` | menu help |

Lengths (UTF-16 units) as stored: 100:9, 101:7, 102:6, 103:8, 104:11, 105:7, 106:38, 107:7, 108:37, 200:4, 201:7, 202:6, 203:9, 204:9, 205:4, 300:13, 301:36, 302:61, 303:114, 1000:15, 1001:16, 1002:20, 1003:24, 1004:14, 1005:15, 1006:28, 1007:17, 1008:18, 1009:36, 1010:11, 2000:15, 65506:30, 65507:43, 65508:15. Strings 101, 102 and 107 end with **one space**.

### 4.1 Message boxes

| Case | Text | Caption | Flags | Helper |
|---|---|---|---|---|
| Out of memory (start-up failure 0x1001E0F, pile allocation 0x10056E6) | 300 | 100 `"Solitaire"` | 0x30 MB_OK\|MB_ICONEXCLAMATION | 0x10023E2 -> 0x100238A |
| Help could not start | 301 | 100 | 0x30 | 0x100261C -> 0x100238A [Wine-verified, `res/wine_msgbox_help_error_client.png`] |
| Full-drag bitmaps could not be allocated (fn 0x10026F8) | 303 | 100 | 0x30 | 0x100261C |
| After the win cascade (base game message 0xD, fn 0x10030C8) | 104 `"Deal Again?"` | 100 | **0x34 MB_YESNO\|MB_ICONEXCLAMATION** | 0x10025D0 (returns TRUE for IDYES or IDOK). Yes -> `PostMessageW(hwnd, WM_COMMAND, 1000, 0)`; No -> the finished table stays. [Wine-verified, `res/wine_msgbox_dealagain_client.png`] |
| Base-class "force win" for a game type that does not implement it (0x10031BA -> 0x10023ED) | inline `"Not Yet Implemented"` (VA 0x1001264) | 100 | 0x30 | **unreachable** in Klondike (its handler implements message 0x12) |

### 4.2 Texts drawn in the status bar [Wine-verified unless noted]

* Left part, `TextOutW(hdc, 4, 0, text)` after a white `PatBlt` of the whole status client: menu-help strings (above), or during the win cascade `"Bonus: " + n + "  " + "Press Esc or a mouse button to stop..."` (two spaces after the number) in **Standard** scoring, only `"Press Esc or a mouse button to stop..."` otherwise. Observed: `"Bonus: 0  Press Esc or a mouse button to stop..."`. The left text uses the DC's default font (the system font), the right part uses the bold font below.
* Right part (fn 0x1005203, game message 0x10), font `CreateFontW(-MulDiv(9, LOGPIXELSY, 72), 0, 0, 0, FW_BOLD (700), 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"MS Shell Dlg")` (= -12 px at 96 DPI; created and deleted on each paint), each piece drawn with `DrawTextW(..., DT_RIGHT|DT_SINGLELINE|DT_NOCLIP)` into the status client rect with `right -= 4`, building from the right edge leftwards:
  1. if Timed game: `"Time: "` + `seconds` (`game+0x34 >> 2`; the counter ticks every 250 ms and saturates at 0x7FFE quarter-seconds);
  2. if Scoring != None: the score text `[-][cur]N[ ]` + `" "` (one trailing space) in black, or **red RGB(255,0,0) if negative** (black on a 2-colour display); then `"Score: "`;
  3. then a white `PatBlt` 4 x tmMaxCharWidth (of the system font, 0x1007310) wide to the left, erasing remnants.
* Captured examples: `"Score: 0 Time: 0"` (default), `"Time: 0"` only (None + Timed), `"Score: 0"` only (Standard, untimed), `"Score: -$52 Time: 0"` (Vegas) (`res/wine_statusbar_none_timed_vs_standard_untimed_x2.png`, `res/wine_statusbar_vegas_currency_x3.png`).
* Vegas currency placement, from `iCurrency` (REG_DWORD in the Solitaire key, default 0) and the symbol `"$"`: 0 -> `-$52`, 1 -> `-52$`, 2 -> `-$ 52`, 3 -> `-52 $` (all four [Wine-verified]). The minus sign always comes first.
* Measured (Wine, default window): status window at client (-1, 367), 587 x 18; its top border line is the only visible border; text glyphs (digits) occupy rows 371-379 of the main client, i.e. the text is top-aligned in the 16-px status client (no DT_VCENTER); the last glyph ends at x = 578. XP draws the WS_BORDER line in COLOR_WINDOWFRAME (black); Wine drew it RGB(158,158,158) (XP colour UNVERIFIED but expected black).

---------------------------------------------------------------------------------------------------

## 5. DIALOGS

All four templates are plain DLGTEMPLATE (not DIALOGEX) with DS_SETFONT, font **"MS Shell Dlg" 8 pt** (dialog base units 6 x 13 px at 96 DPI, so 1 DLU = 1.5 px horizontally and 1.625 px vertically; Wine measurements match exactly, rounding to nearest). None has DS_CENTER and sol.exe has no dialog-centring code (SetWindowPos is not imported; MoveWindow is only used for the status bar), so **each dialog appears at its template x/y, measured from the main window's client origin** (the template rect is the dialog's client rect) [Wine-verified: Options client at main client + (75, 50) px = (50, 31) DLU; Deck client at + (17, 20) px = (11, 12) DLU].

### Dialog 101 "Select Card Back" (RT_DIALOG 101, 490 bytes)

- Caption: `"Select Card Back"`
- Rect (DLU): x=11 y=12 cx=178 cy=112 -> client **267 x 182 px** [Wine-verified]
- Style: 0x80C820C0 = WS_POPUP|WS_BORDER|WS_DLGFRAME|WS_SYSMENU|DS_SETFONT|DS_MODALFRAME|DS_CONTEXTHELP
- ExStyle 0; menu none; class default (#32770); font "MS Shell Dlg" 8 pt; 14 controls

| # | Class | ID | Text | x | y | cx | cy | Style (raw / decoded) | Wine rect (px, client) |
|---|---|---|---|---|---|---|---|---|---|
| 0 | BUTTON | 54 | `""` | 8 | 4 | 26 | 42 | 0x5003000B WS_CHILD\|WS_VISIBLE\|WS_GROUP\|WS_TABSTOP\|BS_OWNERDRAW | (12,7) 39x68 |
| 1 | BUTTON | 55 | `""` | 36 | 4 | 26 | 42 | 0x5000000B WS_CHILD\|WS_VISIBLE\|BS_OWNERDRAW | (54,7) 39x68 |
| 2 | BUTTON | 60 | `""` | 64 | 4 | 26 | 42 | 0x5000000B | (96,7) 39x68 |
| 3 | BUTTON | 61 | `""` | 92 | 4 | 26 | 42 | 0x5000000B | (138,7) 39x68 |
| 4 | BUTTON | 58 | `""` | 120 | 4 | 26 | 42 | 0x5000000B | (180,7) 39x68 |
| 5 | BUTTON | 59 | `""` | 148 | 4 | 26 | 42 | 0x5000000B | (222,7) 39x68 |
| 6 | BUTTON | 56 | `""` | 8 | 48 | 26 | 42 | 0x5000000B | (12,78) 39x68 |
| 7 | BUTTON | 57 | `""` | 36 | 48 | 26 | 42 | 0x5000000B | (54,78) 39x68 |
| 8 | BUTTON | 62 | `""` | 64 | 48 | 26 | 42 | 0x5000000B | (96,78) 39x68 |
| 9 | BUTTON | 63 | `""` | 92 | 48 | 26 | 42 | 0x5000000B | (138,78) 39x68 |
| 10 | BUTTON | 64 | `""` | 120 | 48 | 26 | 42 | 0x5000000B | (180,78) 39x68 |
| 11 | BUTTON | 65 | `""` | 148 | 48 | 26 | 42 | 0x5000000B | (222,78) 39x68 |
| 12 | BUTTON | 1 | `"OK"` | 44 | 94 | 40 | 14 | 0x50030001 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|WS_TABSTOP\|BS_DEFPUSHBUTTON | (66,153) 60x23 |
| 13 | BUTTON | 2 | `"Cancel"` | 94 | 94 | 40 | 14 | 0x50030000 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|WS_TABSTOP\|BS_PUSHBUTTON | (141,153) 60x23 |

The **control id of each owner-drawn button is the cards.dll back bitmap id**. The 12 buttons form one group (only the first has WS_GROUP|WS_TABSTOP), so the arrow keys move between them.

### Dialog 103 "Options" (RT_DIALOG 103, 670 bytes)

- Caption: `"Options"`
- Rect (DLU): x=50 y=31 cx=134 cy=101 -> client **201 x 164 px** [Wine-verified]
- Style: 0x80C820C0 = WS_POPUP|WS_BORDER|WS_DLGFRAME|WS_SYSMENU|DS_SETFONT|DS_MODALFRAME|DS_CONTEXTHELP
- ExStyle 0; menu none; class default; font "MS Shell Dlg" 8 pt; 14 controls

| # | Class | ID | Text (verbatim) | x | y | cx | cy | Style (raw / decoded) | Wine rect (px) |
|---|---|---|---|---|---|---|---|---|---|
| 0 | BUTTON | -1 | `"&Draw"` | 4 | 4 | 60 | 36 | 0x50000007 WS_CHILD\|WS_VISIBLE\|BS_GROUPBOX | (6,7) 90x59 |
| 1 | BUTTON | 300 | `"Draw &One"` | 8 | 13 | 52 | 12 | 0x50030004 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|WS_TABSTOP\|BS_RADIOBUTTON | (12,21) 78x20 |
| 2 | BUTTON | 301 | `"Draw &Three"` | 8 | 25 | 52 | 12 | 0x50000004 WS_CHILD\|WS_VISIBLE\|BS_RADIOBUTTON | (12,41) 78x20 |
| 3 | BUTTON | -1 | `"&Scoring"` | 68 | 4 | 56 | 48 | 0x50020007 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|BS_GROUPBOX | (102,7) 84x78 |
| 4 | BUTTON | 302 | `"St&andard"` | 72 | 14 | 43 | 12 | 0x50030004 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|WS_TABSTOP\|BS_RADIOBUTTON | (108,23) 65x20 |
| 5 | BUTTON | 303 | `"&Vegas"` | 72 | 26 | 36 | 12 | 0x50000004 WS_CHILD\|WS_VISIBLE\|BS_RADIOBUTTON | (108,42) 54x20 |
| 6 | BUTTON | 304 | `"&None"` | 72 | 38 | 30 | 12 | 0x50000004 WS_CHILD\|WS_VISIBLE\|BS_RADIOBUTTON | (108,62) 45x20 |
| 7 | BUTTON | 305 | `"T&imed game"` | 8 | 44 | 52 | 12 | 0x50030003 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|WS_TABSTOP\|BS_AUTOCHECKBOX | (12,72) 78x20 |
| 8 | BUTTON | 306 | `"Status &bar"` | 8 | 56 | 52 | 12 | 0x50010003 WS_CHILD\|WS_VISIBLE\|WS_TABSTOP\|BS_AUTOCHECKBOX | (12,91) 78x20 |
| 9 | BUTTON | 307 | `"Out&line dragging"` | 8 | 68 | 76 | 12 | 0x50010003 WS_CHILD\|WS_VISIBLE\|WS_TABSTOP\|BS_AUTOCHECKBOX | (12,111) 114x20 |
| 10 | BUTTON | 308 | `"&Cumulative"` | 72 | 56 | 62 | 12 | 0x50010003 WS_CHILD\|WS_VISIBLE\|WS_TABSTOP\|BS_AUTOCHECKBOX | (108,91) 93x20 |
| 11 | STATIC | 310 | `"Score"` | 84 | 68 | 20 | 12 | 0x50020000 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|SS_LEFT | (126,111) 30x20 |
| 12 | BUTTON | 1 | `"OK"` | 36 | 84 | 36 | 14 | 0x50030001 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|WS_TABSTOP\|BS_DEFPUSHBUTTON | (54,137) 54x23 |
| 13 | BUTTON | 2 | `"Cancel"` | 76 | 84 | 40 | 14 | 0x50030000 WS_CHILD\|WS_VISIBLE\|WS_GROUP\|WS_TABSTOP\|BS_PUSHBUTTON | (114,137) 60x23 |

* The radio buttons are **BS_RADIOBUTTON (not AUTO)**: the dialog procedure checks them itself with CheckRadioButton.
* `"&Cumulative"` (308) and the static `"Score"` (310) are two controls that read "Cumulative / Score" on two lines below the Scoring box; the static is positioned under the checkbox text (x=84 = checkbox x 72 + 12 DLU). Both are **disabled unless Vegas is selected** [Wine-verified: disabled with Standard, enabled immediately after clicking Vegas].
* The Draw group box has no WS_GROUP flag; the Scoring group box has.
* The checkboxes overflow their box: `"Out&line dragging"` (76 DLU wide) crosses under the Scoring column, exactly as in the original.

### Dialog 102 (no caption; "Set Next Game Number") (RT_DIALOG 102, 262 bytes) - **unused**

- Caption: `""`; Rect (DLU): x=16 y=18 cx=107 cy=70 -> client approx 160 x 114 px
- Style: 0x80C800C0 = WS_POPUP|WS_BORDER|WS_DLGFRAME|WS_SYSMENU|DS_SETFONT|DS_MODALFRAME; 5 controls

| # | Class | ID | Text | x | y | cx | cy | Style |
|---|---|---|---|---|---|---|---|---|
| 0 | STATIC | 100 | `"Set Next Game Number"` | 10 | 6 | 88 | 8 | 0x50000000 SS_LEFT |
| 1 | STATIC | 101 | `"Game #"` | 8 | 26 | 30 | 8 | 0x50000000 SS_LEFT |
| 2 | EDIT | 200 | `""` | 37 | 25 | 56 | 12 | 0x50810000 WS_BORDER\|WS_TABSTOP\|ES_LEFT |
| 3 | BUTTON | 1 | `"Deal"` | 13 | 47 | 33 | 14 | 0x50010001 WS_TABSTOP\|BS_DEFPUSHBUTTON |
| 4 | BUTTON | 2 | `"Cancel"` | 58 | 47 | 32 | 14 | 0x50010000 WS_TABSTOP\|BS_PUSHBUTTON |

No `DialogBoxParamW` uses id 102 (the only calls are 101 at 0x1005C72 and 103 at 0x1005A34); it belonged to the debug command 1005 "Set game number". **XP Solitaire has no "select game number" feature.**

### Dialog 999 "Assertion Failure" (RT_DIALOG 999, 934 bytes) - **unused**

- Rect (DLU): x=12 y=15 cx=198 cy=113 -> approx 297 x 184 px; Style 0x80C80040 = WS_POPUP|WS_BORDER|WS_DLGFRAME|WS_SYSMENU|DS_SETFONT; 11 controls

| # | Class | ID | Text (verbatim) | x | y | cx | cy | Style |
|---|---|---|---|---|---|---|---|---|
| 0 | STATIC | 100 | `"Please send bug report to winbug.  Include file, line number, game number, version number and steps to reproduce."` | 4 | 40 | 191 | 25 | 0x50000000 SS_LEFT |
| 1 | STATIC | 101 | `"File:"` | 8 | 4 | 28 | 9 | 0x50000002 SS_RIGHT |
| 2 | STATIC | 102 | `"A file named sol.dbg will be created in the current directory.  Please attach this file to the bug report -- Thanks"` | 4 | 68 | 188 | 26 | 0x50000000 SS_LEFT |
| 3 | BUTTON | 1 | `"Continue"` | 36 | 96 | 44 | 14 | 0x50010001 BS_DEFPUSHBUTTON |
| 4 | BUTTON | 2 | `"Exit Windows"` | 100 | 96 | 59 | 14 | 0x50010000 BS_PUSHBUTTON |
| 5 | STATIC | 105 | `"Line:"` | 12 | 12 | 24 | 8 | 0x50000002 SS_RIGHT |
| 6 | STATIC | 202 | `"Version 3.0"` | 4 | 28 | 173 | 8 | 0x50000000 SS_LEFT |
| 7 | STATIC | 201 | `""` | 40 | 12 | 100 | 8 | 0x50000000 SS_LEFT |
| 8 | STATIC | 200 | `""` | 40 | 4 | 98 | 9 | 0x50000000 SS_LEFT |
| 9 | STATIC | 109 | `"Game #:"` | 0 | 20 | 36 | 8 | 0x50000002 SS_RIGHT |
| 10 | STATIC | 203 | `""` | 40 | 20 | 68 | 8 | 0x50000000 SS_LEFT |

(Two spaces after "winbug." and after "directory." are in the original.) Debug-build only; never created by the retail exe.

### 5.1 Dialog behaviour (dialog procedures) [code; Wine-verified where marked]

**Options (proc 0x100575F, invoked by cmd 1003):**

| Message | Behaviour |
|---|---|
| WM_INITDIALOG | `CheckRadioButton(302, 304, g_scoring)`; temp scoring = g_scoring; `CheckRadioButton(300, 301, g_draw == 1 ? 300 : 301)`; temp draw = g_draw; `CheckDlgButton(306, g_statusBar)`, `(305, g_timed)`, `(307, g_outline)`, `(308, g_cumulative)`; `EnableWindow(308 and 310, g_scoring == 303 Vegas)`. Returns TRUE (the dialog manager chooses the initial focus). |
| WM_COMMAND 300/301 | temp draw = (id == 300) ? 1 : 3; `CheckRadioButton(300, 301, id)` |
| WM_COMMAND 302-304 | temp scoring = id; `CheckRadioButton(302, 304, id)`; enable 308 and 310 only for 303 [Wine-verified] |
| IDOK | Status bar: if changed, toggle and create (fn 0x1005C79) or destroy (0x1005CFD) the status window. Draw: if changed, store it, rebuild the piles (0x100289B) and the layout (0x10040FE), mark "redeal". Timed: if changed, toggle, mark "redeal". Scoring: if changed, store, mark "redeal". Outline dragging: if changed, fn 0x10026F8 (allocates or frees the three full-drag bitmaps). Cumulative: stored as checked. Then **write `Options`** (fn 0x1001624(3)) and `EndDialog(hDlg, redeal)`. A TRUE result makes the caller deal a new random game immediately. Cancel: `EndDialog(hDlg, FALSE)`, nothing changes. [Wine-verified: after OK with Draw One the registry held Options = 3.] |
| WM_HELP / WM_CONTEXTMENU | `WinHelpW(hCtl, L"sol.hlp", HELP_WM_HELP (0x0C) / HELP_CONTEXTMENU (0x0A), table 0x1007078)` |

**Select Card Back (proc 0x1005AB4, invoked by cmd 1002):**

| Message | Behaviour |
|---|---|
| WM_INITDIALOG | temp back = g_back (0x1007008); `SetFocus(GetDlgItem(hDlg, g_back))`; returns FALSE. The **current back is pre-selected** (Wine: the random back 56 had the blue frame). |
| WM_DRAWITEM | `rc = rcItem`; inner = `InflateRect(rc, -3, -3)`. ODA_DRAWENTIRE: `cdtDrawExt(hDC, inner.left, inner.top, inner.w, inner.h, CtlID, 1 /*mdFaceDown*/, 0)` = the back **stretched to 33 x 62 px** (StretchBlt path of cards.dll) and then the frame. ODA_SELECT (button pressed): `InvertRect(hDC, inner)`. ODA_FOCUS: if ODS_FOCUS, temp back = CtlID (focus = selection), then the frame. **Frame** (fn 0x1005A48): two nested 1-px `FrameRect`s on `rc` in `GetSysColor(focused ? COLOR_HIGHLIGHT (13) : COLOR_BTNFACE (15))`, i.e. a 2-px highlight-colour frame around the selected back and an invisible (face-colour) one around the others, with a 1-px gap before the card. |
| WM_MEASUREITEM | sets CtlType 4, itemWidth 32, itemHeight 54 (dead code: buttons never receive it) |
| WM_COMMAND id 54-65, BN_CLICKED | temp back = id |
| WM_COMMAND id 54-65, BN_DOUBLECLICKED | treated as **OK** |
| IDOK | fn 0x1001444(temp): if different, g_back = temp and the main window is invalidated; then **write `Back`** (fn 0x1001624(4)); `EndDialog(hDlg, 0)` [Wine-verified: choosing 63 wrote Back = 10] |
| IDCANCEL | `EndDialog(hDlg, 0)` |
| WM_HELP / WM_CONTEXTMENU | `WinHelpW(..., L"sol.hlp", 0x0C / 0x0A, table 0x10070D0)` |

Changing the back never redeals.

### 5.2 Context-help ID tables (`.data`; pairs of DWORD control id, help context id; 0-terminated)

| Table VA | Dialog | Pairs |
|---|---|---|
| 0x1007078 | 103 Options | (300, 101), (301, 101), (302, 102), (303, 102), (304, 102), (305, 103), (306, 104), (307, 105), (308, 106), (310, 106) |
| 0x10070D0 | 101 Select Card Back | (54..65, 100) - all twelve backs share context 100 |

### 5.3 Images

* Wine 11.18 captures (client areas; Wine on macOS draws a native caption, so captions are not captured): `res/wine_dlg_103_options_client.png` (201x164, defaults), `res/wine_dlg_103_options_vegas_client.png` (after clicking Vegas), `res/wine_dlg_101_deck_client.png` (267x182; the stretched backs look noisy because Wine's StretchBlt of the 24-bpp backs differs from XP's), `res/wine_msgbox_dealagain_client.png`, `res/wine_msgbox_help_error_client.png`. Message-box layout is Wine's own, not XP's.
* Main window: `res/wine_main_window_default.png` (593x437 incl. menu bar), `res/wine_main_client_default.png` (585x384), `res/wine_main_client_vegas.png`, `res/wine_main_client_nostatus_back99.png` (Options without status bar; `Back` = 99 clamped to back 65), `res/wine_outline_drag.png` (outline dragging: R2_NOT frame), `res/wine_win_cascade.png` (the bouncing-cards win animation with `"Bonus: 0  Press Esc or a mouse button to stop..."`).
* Template-rendered mockups (XP-classic style; tan boxes mark static-control bounds): `res/mock_dlg_103.png`, `res/mock_dlg_101.png` (with the real back bitmaps, 56 selected), `res/mock_dlg_102.png`, `res/mock_dlg_999.png`.

ASCII sketches (1 char ~ 4 DLU horizontally, 1 line ~ 8 DLU vertically):
```
103 "Options" [?][x]  134x101 DLU              101 "Select Card Back" [?][x]  178x112 DLU
+-------------------------------------+       +----------------------------------------------+
| +-Draw----------+ +-Scoring-------+  |       |  [54] [55] [60] [61] [58] [59]               |
| | ( ) Draw One  | | (o) Standard  |  |       |  sky  aqua mosaic purple swirl palms         |
| | (o) Draw Three| | ( ) Vegas     |  |       |                                              |
| +---------------+ | ( ) None      |  |       |  [56] [57] [62] [63] [64] [65]               |
| [x] Timed game    +---------------+  |       |  fish frog dunes astro stripes cars          |
| [x] Status bar    [ ] Cumulative     |       |                                              |
| [ ] Outline dragging    Score        |       |            [  OK  ]   [Cancel]               |
|         [  OK  ] [ Cancel ]          |       +----------------------------------------------+
+-------------------------------------+
```

---------------------------------------------------------------------------------------------------

## 6. ICONS (sol.exe); no bitmaps or cursors

### 6.1 RT_GROUP_ICON 500 (class icon `LoadIconW(hInst, 500)` at 0x1001CBC; small icon `LoadImageW(hInst, 500, IMAGE_ICON, 16, 16, 0)` at 0x1001CD5; also the ShellAbout icon)

| Entry | RT_ICON id | Size | Colours/bpp | Bytes | PNG (`res/`) |
|---|---|---|---|---|---|
| 0 | 1 | 32x32 | 16 colours, 4bpp | 744 | `sol_icon_1_32x32_4bpp.png` |
| 1 | 2 | 16x16 | 16 colours, 4bpp | 296 | `sol_icon_2_16x16_4bpp.png` |
| 2 | 3 | 48x48 | 256 colours, 8bpp | 3752 | `sol_icon_3_48x48_8bpp.png` |
| 3 | 4 | 32x32 | 8bpp | 2216 | `sol_icon_4_32x32_8bpp.png` |
| 4 | 5 | 16x16 | 8bpp | 1384 | `sol_icon_5_16x16_8bpp.png` |
| 5 | 6 | 48x48 | 32bpp (alpha) | 9640 | `sol_icon_6_48x48_32bpp.png` |
| 6 | 7 | 32x32 | 32bpp (alpha) | 4264 | `sol_icon_7_32x32_32bpp.png` |
| 7 | 8 | 16x16 | 32bpp (alpha) | 1128 | `sol_icon_8_16x16_32bpp.png` |

Rebuilt multi-image icon: `res/sol_icon_group_500.ico`. (Unlike freecell.exe there is no 1-bpp image.)

Description: an **opened card box**: a light-blue/white carton seen at a slight angle, its top flap folded open to the upper left, a white card sticking out of the top showing **two red diamond pips**, and a deep-blue gradient panel (the card-back window) on the front. The 8/32-bpp images are XP-style with soft shading and alpha; the 4-bpp images are the same motif in flat VGA colours (grey/white box, navy front, dark-red pips, black outline). At 16x16 only the box with the blue front and two red dots remains.

### 6.2 Cursors

The class cursor is `LoadCursorW(NULL, IDC_ARROW)` (0x1001B72); the status-bar class also uses IDC_ARROW. sol.exe never calls SetCursor (not imported), so **the pointer is always the standard arrow**. `ShowCursor(TRUE/FALSE)` is called on WM_SETFOCUS/WM_KILLFOCUS, and `SetCursorPos` + `ClientToScreen` (0x100302C/0x1003060) move the pointer during keyboard play.

---------------------------------------------------------------------------------------------------

## 7. VERSION INFO (RT_VERSION 1) and MANIFEST

VS_FIXEDFILEINFO: FileVersion 5.1.2600.0, ProductVersion 5.1.2600.0, FileFlagsMask 0x3F, FileFlags 0, FileOS 0x40004 (VOS_NT_WINDOWS32), FileType 1 (VFT_APP), FileSubtype 0.
StringFileInfo `040904B0`:

| Key | Value (verbatim) |
|---|---|
| CompanyName | `"Microsoft Corporation"` |
| FileDescription | `"Solitaire Game Applet"` |
| FileVersion | `"5.1.2600.0 (xpclient.010817-1148)"` |
| InternalName | `"sol.exe"` |
| LegalCopyright | `"© Microsoft Corporation. All rights reserved."` |
| OriginalFilename | `"sol.exe"` |
| ProductName | `"Microsoft® Windows® Operating System"` |
| ProductVersion | `"5.1.2600.0"` |

VarFileInfo Translation: 0x0409 0x04B0.

RT_MANIFEST 1 (verbatim):
```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
<assemblyIdentity type="win32" name="Microsoft.Windows.Accessories.Games.Solitaire" version="1.0.0.0" processorArchitecture="x86"/>
<description>Solitaire Game</description>
<dependency>
    <dependentAssembly>
        <assemblyIdentity
            type="win32"
            name="Microsoft.Windows.Common-Controls"
            version="6.0.0.0"
            language="*"
            publicKeyToken="6595b64144ccf1df"
            processorArchitecture="x86"/>
    </dependentAssembly>
</dependency>
</assembly>
```
ComCtl32 v6 -> the dialogs are themed (Luna) on XP. `InitCommonControlsEx({8, 0x16FD})` at 0x1001CAA.

---------------------------------------------------------------------------------------------------

## 8. IMPORTS (sol.exe; static, bound; plus dynamic loads)

All eight DLLs are bound (TimeDateStamp 0xFFFFFFFF in the import descriptors; bound-import timestamps 0x3B7DFE0E for msvcrt/ADVAPI32/KERNEL32/GDI32/USER32, 0x3B7DFE43 CARDS, 0x3B7DFE0F SHELL32, 0x3B7DFE32 COMCTL32).

- **msvcrt.dll**: _except_handler3 (IAT 0x10011bc), _controlfp (0x10011c0), __set_app_type (0x10011c4), __p__fmode (0x10011c8), __p__commode (0x10011cc), _adjust_fdiv (0x10011d0), __setusermatherr (0x10011d4), _initterm (0x10011d8), __getmainargs (0x10011dc), _acmdln (0x10011e0), exit (0x10011e4), _cexit (0x10011e8), _XcptFilter (0x10011ec), _exit (0x10011f0), _c_exit (0x10011f4), rand (0x10011f8), time (0x10011fc), srand (0x1001200)
- **ADVAPI32.dll**: RegCreateKeyExW (0x1001000), RegQueryValueExW (0x1001004), RegOpenKeyExA (0x1001008), RegQueryValueExA (0x100100c), RegSetValueExW (0x1001010), RegCloseKey (0x1001014)
- **CARDS.dll**: cdtDrawExt (0x100101c), cdtInit (0x1001020), cdtTerm (0x1001024), cdtDraw (0x1001028) - called through jump thunks at 0x10061D8, 0x10061D2, 0x10061CC, 0x10061C6
- **COMCTL32.dll**: InitCommonControlsEx (0x1001030)
- **GDI32.dll**: SetTextColor (0x1001038), SetPixel (0x100103c), LineDDA (0x1001040), BitBlt (0x1001044), CreateCompatibleDC (0x1001048), GetStockObject (0x100104c), GetTextExtentPoint32W (0x1001050), CreateFontW (0x1001054), DeleteDC (0x1001058), TextOutW (0x100105c), PtVisible (0x1001060), SetBrushOrgEx (0x1001064), SelectObject (0x1001068), PatBlt (0x100106c), SetROP2 (0x1001070), MoveToEx (0x1001074), LineTo (0x1001078), GetTextMetricsW (0x100107c), DeleteObject (0x1001080), CreateSolidBrush (0x1001084), GetDeviceCaps (0x1001088), CreateCompatibleBitmap (0x100108c)
- **KERNEL32.dll**: GetStartupInfoA (0x1001094), GetModuleHandleA (0x1001098), MulDiv (0x100109c), lstrlenW (0x10010a0), LocalFree (0x10010a4), LocalAlloc (0x10010a8), GetCommandLineW (0x10010ac), GetProcAddress (0x10010b0), LoadLibraryA (0x10010b4)
- **SHELL32.dll**: ShellAboutW (0x10010bc)
- **USER32.dll**: SetFocus (0x10010c4), RegisterClassW (0x10010c8), CopyRect (0x10010cc), MoveWindow (0x10010d0), DestroyWindow (0x10010d4), GetSysColor (0x10010d8), EndPaint (0x10010dc), BeginPaint (0x10010e0), GetDC (0x10010e4), ReleaseDC (0x10010e8), InvalidateRect (0x10010ec), EndDialog (0x10010f0), GetDesktopWindow (0x10010f4), LoadStringA (0x10010f8), ReleaseCapture (0x10010fc), GetCapture (0x1001100), SetCapture (0x1001104), PostMessageW (0x1001108), EnableMenuItem (0x100110c), GetMenu (0x1001110), DefWindowProcW (0x1001114), ShowCursor (0x1001118), PostQuitMessage (0x100111c), KillTimer (0x1001120), GetClientRect (0x1001124), IsIconic (0x1001128), LoadAcceleratorsW (0x100112c), UpdateWindow (0x1001130), ShowWindow (0x1001134), SetTimer (0x1001138), CreateWindowExW (0x100113c), AdjustWindowRect (0x1001140), RegisterClassExW (0x1001144), LoadImageW (0x1001148), LoadIconW (0x100114c), RegisterWindowMessageW (0x1001150), LoadCursorW (0x1001154), DispatchMessageW (0x1001158), TranslateMessage (0x100115c), TranslateAcceleratorW (0x1001160), GetMessageW (0x1001164), InvertRect (0x1001168), IntersectRect (0x100116c), MessageBoxW (0x1001170), LoadStringW (0x1001174), SetCursorPos (0x1001178), ClientToScreen (0x100117c), GetKeyState (0x1001180), PtInRect (0x1001184), PeekMessageW (0x1001188), MsgWaitForMultipleObjects (0x100118c), DrawTextW (0x1001190), WinHelpW (0x1001194), CheckDlgButton (0x1001198), IsDlgButtonChecked (0x100119c), EnableWindow (0x10011a0), GetDlgItem (0x10011a4), CheckRadioButton (0x10011a8), DialogBoxParamW (0x10011ac), InflateRect (0x10011b0), FrameRect (0x10011b4)

Observations:
* **cards.dll usage** [code]:
  - `cdtInit(&g_dxCard 0x1007308, &g_dyCard 0x100730C)` at 0x1001B5D (71 x 96). If the screen is less than 300 px high (`GetDeviceCaps(VERTRES) < 300`), dyCard is halved (flag 0x100718C).
  - `cdtDrawExt(hdc, x, y, dxCard, dyCard, cd, md, rgbBk = 0x008000 green)` for every card: the pile card word has bit 15 = face up; face-up -> `md 0 (mdFaceUp), cd = word & 0x7FFF` (cdt index rank*4+suit); face-down -> `md 1 (mdFaceDown), cd = g_back` (fn 0x1001F45). During the win cascade the mode gets **0x80000000** ("do not save/restore the corner pixels") (fn 0x1002082, flag 0x10071CC), so the bouncing trail shows square card corners.
  - **Empty foundation/tableau slot**: `md 3 (mdGhost), cd 0` = green fill + the dotted bitmap 53 (fn 0x10020F2 from 0x1004518).
  - **Empty stock**: `md 7 (mdDeckO)` = bitmap 68 (green ring, "click to recycle"), or `md 6 (mdDeckX)` = bitmap 67 (red X, no more passes) when Scoring = Vegas and the passes are used up (`game+0x40 == draw - 1`) (0x1004728-0x1004746).
  - Deck dialog: `cdtDrawExt(..., 33, 62, backId, 1, 0)` (stretched).
  - `cdtDraw` is used only for the `"CardDraw"` registered message (0x1001ACB). `cdtTerm()` on WM_DESTROY (0x10017CB).
  - **`cdtAnimate` is not imported** and there is no `GetProcAddress` for it (the only GetProcAddress is HtmlHelpA, ordinal 14). See 10.4.
* **Registry**: only W functions for the Solitaire key, `RegCreateKeyExW` every time (the key is created on first launch even if nothing is written [Wine-verified: empty key after the first run]); `RegOpenKeyExA` + `RegQueryValueExA` only for the hhctrl.ocx CLSID. No RegDeleteValue/RegDeleteKey, no RegFlushKey.
* **Help**: HtmlHelp is not imported. fn 0x10061DE: if not yet loaded, read `HKCR\CLSID\{ADB880A6-D8FF-11CF-9377-00AA003B7A11}\InprocServer32` (default value) with KEY_READ (0x20019) and `LoadLibraryA` that path, else `LoadLibraryA("hhctrl.ocx")`; then `GetProcAddress(h, (LPCSTR)14)` = **HtmlHelpA by ordinal**. On failure a "help unavailable" flag (0x1007200) is set and every later call returns NULL (-> message 301). `WinHelpW` (dialog "?" help) with `L"sol.hlp"`.
* **No sound** (no PlaySound/winmm, no MessageBeep). **No INI file** access at all (unlike FreeCell's entpack.ini migration).
* `LoadStringA` exists only for the ANSI `"sol.chm"`. `LoadImageW` only for the 16x16 class icon. `RegisterClassW` (not Ex) only for the `"Stat"` class.
* `LineDDA` drives the card-flight animation (0x1003F14); `PeekMessageW` + `MsgWaitForMultipleObjects` run the win-cascade loop (0x1004D56) so Esc or a mouse button can stop it.
* **Command line** (`GetCommandLineW`, scanned at 0x1001C70): the only switch is **`/I`** (case-sensitive capital I, anywhere in the line), which adds WS_MINIMIZE to the CreateWindowExW style. The subsequent `ShowWindow(nCmdShow)` with a normal nCmdShow restores it [Wine-verified: no visible effect when launched normally]. If nCmdShow is SW_MINIMIZE (6) or SW_SHOWMINNOACTIVE (7) the initial deal is skipped; otherwise start-up posts `WM_COMMAND 1000`.

---------------------------------------------------------------------------------------------------

## 9. Strings outside resources, registry, window classes

### 9.1 Embedded strings (`.text` string pool 0x100123C-0x100136F, plus `.data`)

| VA | Enc | String (verbatim) | Use |
|---|---|---|---|
| 0x100123C | ASCII | `"NTHelp.chm"` | How to Use Help |
| 0x1001248 | UTF-16 | `"$"` | default currency symbol (sCurrency) |
| 0x100124C | UTF-16 | `"HiddenAccel"` | accelerator table name |
| 0x1001264 | UTF-16 | `"Not Yet Implemented"` | base-class message box (unreachable) |
| 0x100128C | UTF-16 | `"Software\\Microsoft\\Solitaire"` | registry key (HKCU) |
| 0x10012C8 | UTF-16 | `"MS Shell Dlg"` | status-bar font face |
| 0x10012E4 | UTF-16 | `"sol.hlp"` | WinHelp file for dialog context help |
| 0x10012F4 | UTF-16 | `""` | window name of the status bar |
| 0x1001308 | ASCII | `"hhctrl.ocx"` | HtmlHelp fallback DLL |
| 0x1001318 | ASCII | `"CLSID\\{ADB880A6-D8FF-11CF-9377-00AA003B7A11}\\InprocServer32"` | HtmlHelp control CLSID (under HKCR) |
| 0x1001354 | ASCII | `""` | default value name for the CLSID query |
| 0x1001358 | ASCII | `"NB10"` + `"sol.pdb"` (0x1001368) | CodeView debug record |
| 0x100700C | UTF-16 | `"Solitaire"` | main window class name (`.data`) |
| 0x1007138 | UTF-16 | `"Stat"` | status-bar window class name (`.data`) |

### 9.2 Registry: `HKEY_CURRENT_USER\Software\Microsoft\Solitaire`

Helpers [code]: `RegGetInt(sectionId, nameId, default)` 0x1002540, `RegSetInt(sectionId, nameId, value)` 0x1002415, `RegGetString(sectionId, nameId, buf, default, cb)` 0x100247F. Each opens the key with `RegCreateKeyExW(HKCU, L"Software\\Microsoft\\Solitaire", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL)` and loads the value name from the string table (`nameId`); `sectionId` is ignored. Reads accept only **REG_DWORD** (type 4, 4 bytes) for integers and REG_SZ for strings; anything else yields the default. Writes use `RegSetValueExW(..., REG_DWORD, &v, 4)`.

| Value (string id) | Type | Meaning | Default | Written |
|---|---|---|---|---|
| `Options` (201) | REG_DWORD | bit 0 (0x01) Status bar; bit 1 (0x02) Timed game; bit 2 (0x04) Outline dragging; bit 3 (0x08) Draw Three (clear = Draw One); bits 4-5 (0x30) scoring: 0x00 Standard, 0x10 Vegas, 0x20 None (0x30 also reads as Standard); bit 6 (0x40) Cumulative Score. Other bits ignored. | **0x0B** (Status bar, Timed, Draw Three, Standard), computed from the `.data` initial values (0x1007020 = 1, 0x1007024 = 1, 0x1007028 = 302, 0x100702C = 3, outline and cumulative 0) | Options -> OK only (fn 0x1001624 bit 0) [Wine-verified: 0x1B Vegas start, Options dialog shows Vegas; after OK with Draw One -> 3; with Vegas+Cumulative+Outline -> 0x5F] |
| `Back` (200) | REG_DWORD | card-back bitmap id **minus 53** (1..12 for ids 54..65); out-of-range values are clamped to 54..65 | `rand() % 12` (after `srand((WORD)time(NULL))`), i.e. id = clamp(53 + 0..11, 54, 65): **54..64 at random, 54 twice as likely, 65 never** by default | Deck -> OK only (fn 0x1001624 bit 2) [Wine-verified: back 63 -> 10; value 99 -> back 65] |
| `iCurrency` (203) | REG_DWORD | Vegas `$` placement 0-3 (as WIN.INI `[intl] iCurrency`) | 0 | never [Wine-verified formats in 4.2] |
| `sCurrency` (204) | REG_SZ | currency symbol | `"$"` | never; **buggy, see 9.3** |
| `Bitmap` (202) | - | never read | - | - |

The key is never cleaned up; there are **no statistics** of any kind.

### 9.3 Bug: `sCurrency` crashes Solitaire [code + Wine-verified]

`RegGetString` (0x100247F) passes the **default-string pointer** (0x1001248, inside the read-only `.text` section) as `lpData` to `RegQueryValueExW` (0x10024E6), with `*lpcbData = 10` (bytes). If `sCurrency` exists and its data fits in 10 bytes, the API writes into `.text` and the process dies with an access violation at start-up (Wine: `Unhandled page fault on write access to 01001248`). If the data is longer, ERROR_MORE_DATA is returned and the default `"$"` is copied into the real buffer 0x1007370 (copying `lstrlenW+1` **bytes**, i.e. only the `$` character; the zero-initialised buffer supplies the terminator). Even on success with REG_SZ the buffer would not be filled. The clone should simply always use `"$"` (and need not reproduce the crash).

### 9.4 Window classes and the main window

`"Solitaire"` (RegisterClassExW at 0x1001D3A, only when hPrevInstance is NULL):

| Field | Value |
|---|---|
| cbSize | 0x30 |
| style | 0x2008 = CS_DBLCLKS \| CS_BYTEALIGNWINDOW |
| lpfnWndProc | 0x10016BD |
| cbClsExtra / cbWndExtra | 0 / 0 |
| hIcon / hIconSm | LoadIconW(hInst, 500) / LoadImageW(hInst, 500, IMAGE_ICON, 16, 16, 0) |
| hCursor | LoadCursorW(NULL, IDC_ARROW) |
| hbrBackground | CreateSolidBrush(0x008000) = **RGB(0,128,0)**; white (0xFFFFFF) if `GetDeviceCaps(NUMCOLORS) == 2`. The same colour (0x1007348) is cdtDrawExt's rgbBk. |
| lpszMenuName | MAKEINTRESOURCE(1) |
| lpszClassName | `"Solitaire"` |

`CreateWindowExW(0, "Solitaire", "Solitaire", WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN (0x02CF0000) [|WS_MINIMIZE with /I], CW_USEDEFAULT, 0, w, h, NULL, NULL, hInst, NULL)` at 0x1001DD8, where the client size is `(7*dxCard + 8*(dxCard/8 + 3)) x (4*dyCard)` = **585 x 384**, converted with `AdjustWindowRect(..., WS_OVERLAPPEDWINDOW, TRUE)`, and the height is clamped to the screen height (VERTRES). Wine: window 593 x 437, client 585 x 384, final style 0x16CF0000, ex-style WS_EX_WINDOWEDGE [Wine-verified]. No WM_GETMINMAXINFO handling (freely resizable); WM_SIZE recomputes the column gap as `max((cx - 7*dxCard)/8, dxCard/8 + 3)`.

`"Stat"` (RegisterClassW in fn 0x1005F1E): style 0, wndproc 0x1005EBF (paints only on WM_PAINT; everything else to DefWindowProc), hCursor IDC_ARROW, hbrBackground `GetStockObject(WHITE_BRUSH)`, no icon/menu. Created by fn 0x1005C79 when the Status bar option is on: `CreateWindowExW(0, "Stat", "", 0x40800003 (WS_CHILD|WS_BORDER|3), rc.left-1, rc.bottom-h+1, rc.right-rc.left+2, h, hwndMain, 0, hInst, 0)` with `h = tmHeight + 2` of the screen DC's system font (0x1007368; 18 at 96 DPI), then `ShowWindow(SW_SHOWNOACTIVATE (4))` and UpdateWindow. Resized with MoveWindow on WM_SIZE/WM_MOVE (fn 0x1005D94); destroyed when the option is turned off. The status bar overlaps the bottom 17 px of the 384-px client (it is not subtracted from the playing area).

---------------------------------------------------------------------------------------------------

## 10. cards.dll (as used by Solitaire)

The DLL is byte-identical to the one in `../resources.md` section 10 (exports WEP, cdtAnimate, cdtDraw, cdtDrawExt, cdtInit, cdtTerm; draw modes; black border on red pip cards; 3-pixel corner save/restore). Version: FileDescription `"Entertainment Pack Cardplaying Helper DLL"`, FileVersion `"5.1.2600.0 (xpclient.010817-1148)"`, InternalName/OriginalFilename `"cards"`, FileType 2 (VFT_DLL).

### 10.1 All bitmap resources (RT_BITMAP, 74 entries, lang 0x409)

| ID(s) | Content | Size | Format | Bytes each |
|---|---|---|---|---|
| 1-13 / 14-26 / 27-39 / 40-52 | faces: Clubs / Diamonds / Hearts / Spades A..K (`id = suit*13 + rank + 1`) | 71x96 | A-10: 1bpp (C/S BITMAPCOREHEADER black/white; D/H BITMAPINFOHEADER red #FF0000/white); J-K: 4bpp BITMAPCOREHEADER | 1170 / 1200 / 3536 |
| 53 | ghost / empty-slot template: white, black 1-px border with rounded corners, regular dot grid | 71x96 | 1bpp core | 1170 |
| 54-65 | the 12 card backs (10.2) | 71x96 | BITMAPINFOHEADER 24bpp | 20778 |
| (66) | **absent** | | | |
| 67 | mdDeckX: red #FF0000 X (bbox 7,19-64,77) on dark green #008000, black border | 71x96 | 4bpp core | 3516 |
| 68 | mdDeckO: bright green #00FF00 ring (bbox 7,18-63,78) on #008000, black border | 71x96 | 4bpp core | 3516 |
| 678, 679 | legacy sprite: "card hand" (10.4) | 32x22 | 4bpp core, VGA palette | 412 |
| 680 | legacy sprite: castle bats | 26x12 | 4bpp core | 252 |
| 681, 682 | legacy sprite: beach sun | 14x12 | 4bpp core | 156 |
| 683, 684 | legacy sprite: robot gauge and lamps | 24x7 | 4bpp core | 144 |

All 74 PNGs: `res/cards/cards_bitmap_<id>.png`. **Contact sheet** of everything at 1:1 (sprites at 4x): `res/cards_contact.png`. Backs in dialog order at 1:1 and 2x: `res/cards_backs_dialog_order.png`, `res/cards_backs_dialog_order_2x.png`. Sprites at 8x: `res/cards_sprites_678_684_x8.png`. These are raw bitmaps (no border fix-up or corner treatment).

### 10.2 The 12 backs (ids 54-65; all 71x96, 24bpp, 20,778 bytes)

Every back has a **1-px black outline** (RGB(134,134,134) grey for 63, the astronaut) with pixel (1,1) etc. also outline-coloured, and **magenta RGB(255,0,255) in the 3 corner pixels of each corner** (12 pixels: (0,0),(1,0),(0,1) and mirrors). Those are exactly the pixels cards.dll saves before and restores after a native-size draw, so the magenta never shows at 71x96 [Wine-verified: no magenta in the captures]; a clone must treat them as transparent / table colour.

| ID | Dialog position | Back |
|---|---|---|
| 54 | row 1, col 1 | blue sky with white cumulus clouds |
| 55 | row 1, col 2 | aqua/teal abstract glass with light streaks |
| 60 | row 1, col 3 | blue/teal pixel mosaic (4 x 5 squares) |
| 61 | row 1, col 4 | purple/magenta abstract with a row of glowing dots near the bottom |
| 58 | row 1, col 5 | red abstract swirl (rose-like) on white |
| 59 | row 1, col 6 | tropical island with palm trees over turquoise water |
| 56 | row 2, col 1 | orange-striped tropical fish on blue |
| 57 | row 2, col 2 | green frog on orange |
| 62 | row 2, col 3 | red desert dunes under a dark-blue sky with the moon |
| 63 | row 2, col 4 | astronaut (spacewalk) above Earth, grey outline |
| 64 | row 2, col 5 | orange/yellow horizontal stripes |
| 65 | row 2, col 6 | three yellow vintage race cars on green |

None of these backs is animated in XP (10.4).

### 10.3 Draw modes used by Solitaire

`md` 0 (face up), 1 (back), 3 (ghost: empty foundation/tableau), 6 (deck X), 7 (deck O); 0x80000000 flag during the win cascade. Not used: 2 (mdHilite, FreeCell's inverted selection), 4, 5.

### 10.4 Card-back animation: none in XP [code]

* `cdtAnimate` (ordinal 2, 0x6FC1118A) is `xor eax,eax / inc eax / ret 0x14`: it takes the classic 5 arguments `(HDC hdc, int cdBack, int x, int y, int ispr)` and returns TRUE without drawing.
* sol.exe neither imports nor resolves it. The only remnant on the Solitaire side: the 250-ms timer (fn 0x1005189), while a timed game's clock runs and no card is being moved, sends pile message **0x1B** with the current tick count to **pile 0 (the stock)**; the stock's handler (0x1004823) just returns TRUE. In the 16-bit versions this is where the stock's top back was animated (UNVERIFIED historical interpretation, but the plumbing matches).
* cards.dll never loads ids 678-684: its three `LoadBitmapA` call sites load the face id computed from `cd`, the ids 53/67/68 (in cdtInit) and the back id = `cd` for md 1. The sprites are unreferenced data.
* The sprite frames (letters: K black, W white, R red, Y yellow, B blue, C cyan, M magenta, G green; full maps in `res/cards_sprites_678_684_x8.png`):
  - **678/679 (32x22) "card hand / ace up the sleeve"**: a red-white-yellow checked sleeve (diagonal R/W/Y check) across the top right, a white cuff/hand with black outline at the lower left. In 678 only a small red tip shows at the cuff; in 679 a larger red shape (the card) slides out. Two frames.
  - **680 (26x12) "castle bats"**: three small black bat shapes (`K.....K` / `KKKKKKK` / `.K.K.K.`) on a blue (night-sky) field with a white sliver at the top edge. One frame (bats drawn vs. background restored).
  - **681/682 (14x12) "beach sun"**: a yellow smiley sun wearing black sunglasses on cyan; in 682 it sticks out a red tongue. Two frames.
  - **683/684 (24x7) "robot"**: a white dial with black outline whose needle is vertical in 683 and tilted in 684, and two lamps on a red panel that swap magenta/green between the frames. Two frames.
* These match the four animated backs of the Windows 3.x-2000 cards.dll (robot, castle, beach, card hand); those backs themselves are **not** in this DLL (ids 54-65 were replaced by the photo backs), so the sprites could not be placed correctly even if cdtAnimate worked. The mapping of sprites to old backs is from their content (UNVERIFIED against an old cards.dll, which is not available here). **The clone should not animate any back.**

---------------------------------------------------------------------------------------------------

## 11. Files produced (`<sr>` = `/private/tmp/claude-502/-Users-erko-IdeaProjects-xp-cards/4b6e3af5-e647-41ff-a37b-eccdd0578cdc/scratchpad/sol-research`)

* `<sr>/res/sol_icon_{1..8}_*.png`, `sol_icon_group_500.ico`
* `<sr>/res/cards/cards_bitmap_<id>.png` (74 files), `cards_contact.png`, `cards_backs_dialog_order.png`, `cards_backs_dialog_order_2x.png`, `cards_sprites_678_684_x8.png`
* `<sr>/res/mock_dlg_{101,102,103,999}.png`
* `<sr>/res/wine_*.png`: main window/client (default, Vegas, no status bar), Options (default, Vegas), Deck, Deal Again?, help error, outline drag, win cascade, status-bar strips (menu help, modes, currency formats)
* `<sr>/rsx/work/sol.dis` (annotated disassembly of sol.exe), `<sr>/rsx/work/cards.dis`

## 12. UNVERIFIED / caveats

* XP (Luna) frame metrics, the colour of the status bar's WS_BORDER line (expected black; Wine draws grey) and the exact glyph rendering of the bold MS Shell Dlg status font were not checked on real XP.
* Whether XP ships `sol.hlp` for the dialogs' "?" help (the code calls WinHelpW with it; `sol.chm` is in `%windir%\Help`). HtmlHelp is given the bare name `"sol.chm"`, so it relies on HTML Help's own search path.
* The purpose of the `"CardDraw"` registered message (which external client used it).
* The historical identity of sprites 678-684 (content-based; no older cards.dll was available) and the meaning of pile message 0x1B as the old animation tick.
* The sCurrency crash was reproduced in Wine; on XP the `.text` page is also read-only (section characteristics 0x60000020), so the same access violation is expected.
* `/I` start-up behaviour on real XP with a minimized shortcut was not tested.
