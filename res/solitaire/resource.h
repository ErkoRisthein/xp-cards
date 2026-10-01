/*
 * Solitaire HD — resource ids. Command, dialog, control, string and icon ids are XP Solitaire's
 * (docs/xp-reference/solitaire/resources.md); RCDATA ids are the engine's (src/engine/cardset.h).
 * Included by res/solitaire/solitaire.rc and the Win32 sources.
 */
#ifndef SOL_RESOURCE_H
#define SOL_RESOURCE_H

/* Menu / accelerator commands (resources.md §2.1). XP shows the string with the command's id in the
 * status bar while a menu item is highlighted (WM_MENUSELECT), so each has a string below. */
#define IDM_DEAL            1000    /* Game > Deal (F2) */
#define IDM_UNDO            1001    /* Game > Undo (no accelerator in XP) */
#define IDM_DECK            1002    /* Game > Deck... */
#define IDM_OPTIONS         1003    /* Game > Options... */
#define IDM_EXIT            1004    /* Game > Exit */
#define IDM_FORCEWIN        1010    /* Alt+Shift+2 only: force a win (the cascade) */
#define IDM_ABOUT           2000    /* Help > About Solitaire */
#define IDM_HELPCONTENTS    0xFFE2  /* Help > Contents (F1 arrives as WM_HELP) */
#define IDM_HELPSEARCH      0xFFE3  /* Help > Search for Help on... */
#define IDM_HELPHOWTO       0xFFE4  /* Help > How to Use Help */
/* Extras (not in XP) */
#define IDM_FULLSCREEN      1100    /* Game > Full Screen (F11, Alt+Enter; Esc leaves) */
#define IDM_REDO            1101    /* Game > Redo (Ctrl+Y) = SOL_CMD_REDO (src/solitaire/session.h) */
/* Extras (v1.1) */
#define IDM_HINT            1102    /* Game > Hint (H, WM_CHAR) = SOL_CMD_HINT */
#define IDM_FINISH          1103    /* Game > Finish (F6) = SOL_CMD_FINISH */
#define IDM_STATISTICS      1104    /* Game > Statistics... (F4) */

/* Numeric resources */
#define IDR_MENU            1       /* menu (XP: MAKEINTRESOURCE(1)) */
#define IDI_SOLITAIRE       500     /* group icon (XP: 500; also the small icon and ShellAbout's) */

/* Dialogs (resources.md §5); XP has no DS_CENTER: each appears at its template x/y from the main
 * window's client origin */
#define IDD_DECK            101     /* "Select Card Back" */
#define IDD_OPTIONS         103     /* "Options" */
#define IDD_STATS           105     /* "Solitaire Statistics" (extra, v1.1) */

/* Select Card Back: the 12 owner-drawn buttons. A button's id is XP's cards.dll back id 54..65 = back
 * index + IDC_BACK0 (render.h SOL_NBACKS); rows 54 55 60 61 58 59 / 56 57 62 63 64 65 as XP's. */
#define IDC_BACK0           54
#define IDC_BACKLAST        65

/* Options */
#define IDC_DRAWONE         300     /* "Draw &One" (BS_RADIOBUTTON: the dialog checks it itself) */
#define IDC_DRAWTHREE       301     /* "Draw &Three" */
#define IDC_STANDARD        302     /* "St&andard" */
#define IDC_VEGAS           303     /* "&Vegas" */
#define IDC_NONE            304     /* "&None" */
#define IDC_TIMED           305     /* "T&imed game" */
#define IDC_STATUSBAR       306     /* "Status &bar" */
#define IDC_OUTLINE         307     /* "Out&line dragging" */
#define IDC_CUMULATIVE      308     /* "&Cumulative" (enabled only with Vegas) */
#define IDC_CUMSCORE        310     /* "Score": the second line of "Cumulative Score" (enabled with it) */
/* Options, the "Extras" group (v1.1, not in XP; HKCU\Software\xp-cards\Solitaire HD) */
#define IDC_EXTRAS_GROUP    320     /* "Extras" */
#define IDC_AUTOTURN        321     /* "T&urn cards over automatically" (AutoTurn) */
#define IDC_CLICKMOVE       322     /* "Single clic&k moves a card" (ClickToMove) */
#define IDC_AUTOFINISH      323     /* "&Finish automatically" (AutoFinish) */
#define IDC_WINNABLE        324     /* "Deal only winnable &games" (WinnableOnly) */
#define IDC_SAVEGAME        325     /* "Sav&e game on exit, resume at start" (SaveGame) */
#define IDC_WARNUNWINNABLE  326     /* "&Warn when the game can't be won" (WarnUnwinnable) */
/* Statistics */
#define IDC_STATS_MODE      330     /* the mode: a drop-down list, the current game's mode first selected */
#define IDC_STATS_LABELS    331     /* the labels (IDS_STATS_LABELS) */
#define IDC_STATS_VALUES    332     /* the values (src/solitaire/stats.h sol_stats_format) */
#define IDC_STATS_RESET     333     /* "&Reset": all modes, after IDS_RESETSTATS */
#ifndef IDC_STATIC
#define IDC_STATIC          (-1)
#endif

/* String table (resources.md §4) */
#define IDS_APPNAME         100     /* "Solitaire": window title, message box captions, ShellAbout */
#define IDS_SCORE           101     /* "Score: " (status bar; one trailing space) */
#define IDS_TIME            102     /* "Time: " */
#define IDS_DEALAGAIN       104     /* "Deal Again?" (after the cascade, MB_YESNO | MB_ICONEXCLAMATION) */
#define IDS_HELPFILE        105     /* "sol.chm" */
#define IDS_PRESSESC        106     /* "Press Esc or a mouse button to stop..." (status bar, cascade) */
#define IDS_BONUS           107     /* "Bonus: " (Standard: "Bonus: <n>  " + IDS_PRESSESC) */
#define IDS_BYLINE          108     /* "Developed for Microsoft by Wes Cherry" (XP's About text) */
#define IDS_ABOUTTEXT       109     /* Solitaire HD's About text (extra): what it is, the art credit */
#define IDS_OUTOFMEMORY     300     /* "Out of memory" */
#define IDS_HELPFAILED      301     /* "Unable to load Windows Help application" (XP: "Unable load") */
#define IDS_NODRAGMEM       303     /* full drag image could not be allocated: use Outline dragging */
/* Extra strings (v1.1): the session's messages (= SOL_MSG_*), the Statistics dialog */
#define IDS_MSG_NOHINT      1110    /* "No hint is available." */
#define IDS_MSG_UNWINNABLE  1111    /* "This game can no longer be won. Use Undo to go back." */
#define IDS_MSG_UNWINNABLE_DEAL 1112 /* "This game cannot be won." */
#define IDS_STATS_MODE0     1120    /* "Draw One, Standard" .. 1125 "Draw Three, None" (stats.h's modes) */
#define IDS_STATS_LABELS    1126    /* the labels, one per line */
#define IDS_RESETSTATS      1127    /* "Are you sure you want to delete all statistics?" */

#ifndef RC_INVOKED
#define RES_ACCEL           L"HiddenAccel"   /* XP loads its accelerators by this name */
#endif

/* RCDATA (PNG): the 52 faces are 1000 + card (res/common/cards.rc), the backs 1200 + index
 * (CE_ASSET_BACK0 + i, res/solitaire/backs/<54 + i>_<name>.png). */
#define IDR_BACK0           1200

#endif
