/*
 * FreeCell HD — resource ids. Command, control, dialog, string, icon and cursor ids are XP
 * FreeCell's (docs/xp-reference/resources.md); RCDATA ids are fixed by src/engine/cardset.h (cards)
 * and src/freecell/sprites.h (kings). Included by res/freecell/freecell.rc and the Win32 sources.
 */
#ifndef FC_RESOURCE_H
#define FC_RESOURCE_H

/* Menu / accelerator commands (resources.md §2, §3) */
#define IDM_ABOUT           101     /* Help > About FreeCell... */
#define IDM_NEWGAME         102     /* Game > New Game (F2) */
#define IDM_SELECTGAME      103     /* Game > Select Game (F3) */
#define IDM_STATISTICS      105     /* Game > Statistics... (F4) */
#define IDM_HELPCONTENTS    106     /* Help > Contents (F1, Shift+F1) */
#define IDM_RESTART         107     /* Game > Restart Game (initially grayed) */
#define IDM_EXIT            108     /* Game > Exit */
#define IDM_OPTIONS         109     /* Game > Options... (F5) */
#define IDM_HELPSEARCH      110     /* Help > Search for Help on... */
#define IDM_HELPHOWTO       111     /* Help > How to Use Help */
#define IDM_CHEAT           114     /* Ctrl+Shift+F10 only: "User-Friendly User Interface" */
#define IDM_UNDO            115     /* Game > Undo (F10, initially grayed) */
#define IDM_REDO            116     /* Game > Redo (Ctrl+Y, initially grayed); extra, not in XP */
#define IDM_FULLSCREEN      117     /* Game > Full Screen (F11, Alt+Enter; Esc leaves); extra */
#define IDM_HINT            118     /* Game > Hint (key H, WM_CHAR); extra (v1.2) */
#define IDM_FINISH          119     /* Game > Finish (F6, enabled on a sure win); extra (v1.2) */
#define IDM_UNDOALL         120     /* Game > Undo All (asks first; enabled with Undo); extra (v1.4) */

/* Dialog control ids (resources.md §5) */
#define IDC_MOVECOLUMN      201     /* MoveCol: "Move &column" (default) */
#define IDC_MOVESINGLE      202     /* MoveCol: "Move &single card" */
#define IDC_GAMENUMBER      203     /* GameNum: edit */
#define IDC_SAMEGAME        205     /* YouLose: "&Same game" */
#define IDC_CLEARSTATS      207     /* Stats: "&Clear" */
#define IDC_SELECTGAME      208     /* YouWin: "&Select game" */
#define IDC_MESSAGES        209     /* Options: "Display &messages on illegal moves" */
#define IDC_QUICKPLAY       210     /* Options: "&Quick play (no animation)" */
#define IDC_DBLCLICK        211     /* Options: "&Double click moves card to free cell" */
#define IDC_STATS_SESSION   212     /* Stats: string 319 */
#define IDC_STATS_TOTAL     213     /* Stats: string 320 */
#define IDC_STATS_STREAKS   214     /* Stats: string 321 */
/* Extra controls (v1.1, not in XP) */
#define IDC_EXTRAS_GROUP    220     /* Options: "Extras" group box */
#define IDC_SHOWTIME        221     /* Options: "Show &time and moves" */
#define IDC_STDSUPERMOVE    222     /* Options: "&Standard multi-card moves" */
#define IDC_FULLRANGE       223     /* Options: "&New Game picks from all 1,000,000 games" */
#define IDC_WONBEFORE       224     /* GameNum: "You have won this game before." / empty */
#define IDC_STATS_WONDEALS  225     /* Stats: "Different games won: %u" */
/* Extra controls (v1.2) */
#define IDC_WARNUNWINNABLE  226     /* Options: "&Warn when the game can't be won" */
#define IDC_AUTOFINISH      227     /* Options: "Finish &automatically" */
/* Extra controls (v1.4) */
#define IDC_SINGLECLICK     228     /* Options: "Single &click moves a card" */
#define IDC_DRAGDROP        229     /* Options: "D&rag and drop cards" */
/* Extra controls (2d) */
#define IDC_ENHANCEDANIM    230     /* Options: "&Enhanced animations" */
/* Extra controls (2c, Large Print) */
#define IDC_LARGEPRINT      231     /* Options: "&Large print cards" */
#ifndef IDC_STATIC
#define IDC_STATIC          (-1)
#endif

/* Numeric resources */
#define IDD_OPTIONS         505     /* the Options dialog is numeric in XP; the others are named */
#define IDI_FREECELL        601     /* group icon (LoadIcon(hInst, MAKEINTRESOURCE(601))) */

/* String table (resources.md §4) */
#define IDS_APPNAME         301     /* "FreeCell" (message box captions, window title) */
#define IDS_BYLINE          302     /* "by Jim Horne" (ShellAbout) */
#define IDS_TITLE_GAME      303     /* "FreeCell Game #%d" */
#define IDS_OUTOFMEMORY     304
#define IDS_RESIGN          305     /* "Do you want to resign this game?" */
#define IDS_ILLEGAL         306     /* "That move is not allowed." */
#define IDS_NOTENOUGH       307     /* "That move requires moving %u cards. You only have ..." */
#define IDS_CARDSLEFT       308     /* "Cards Left: %u" */
#define IDS_CLEARSTATS      309     /* "Are you sure you want to delete all statistics?" */
#define IDS_1WIN            310
#define IDS_1LOSS           311     /* = IDS_1WIN + 1 */
#define IDS_NWINS           312
#define IDS_NLOSSES         313     /* = IDS_NWINS + 1 */
#define IDS_HOWTOPLAY       314     /* unused in XP */
#define IDS_COMMANDS        315     /* unused in XP */
#define IDS_STREAK          316     /* unused in XP */
#define IDS_STYPE           317     /* unused in XP */
#define IDS_ABOUTNAME       318     /* "FreeCell" (ShellAbout caption) */
#define IDS_STATS_SESSION   319     /* wsprintf(pct, won, lost) */
#define IDS_STATS_TOTAL     320     /* wsprintf(pct, won, lost) */
#define IDS_STATS_STREAKS   321     /* wsprintf(wins, losses, current string) */
/* Extra strings (v1.1) */
#define IDS_MOVES           401     /* "Moves: %u" (menu bar, left of Cards Left) */
#define IDS_TIME            402     /* "Time: %s" */
#define IDS_WONBEFORE       403     /* "You have won this game before." */
#define IDS_WONDEALS        404     /* "Different games won: %u" */
#define IDS_FINISHBTN       405     /* "Finish": the push button on the table (2d) */

/* RCDATA PNGs: card c (rank*4 + suit) = IDR_CARD0 + c; kings = FC_ASSET_KING_* in sprites.h */
#define IDR_CARD0           1000
#define IDR_KING_RIGHT      1100
#define IDR_KING_LEFT       1101
#define IDR_KING_SMILE      1102

/* Named resources (XP loads these by name; lookup is case-insensitive) */
#ifndef RC_INVOKED
#define RES_MENU            L"FreeMenu"     /* menu, accelerators, class menu name */
#define RES_DLG_GAMENUM     L"GameNum"
#define RES_DLG_MOVECOL     L"MoveCol"
#define RES_DLG_STATS       L"Stats"
#define RES_DLG_YOULOSE     L"YouLose"
#define RES_DLG_YOUWIN      L"YouWin"
#define RES_CUR_DOWNARROW   L"DownArrow"
#endif

#endif
