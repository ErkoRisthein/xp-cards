/*
 * Card engine, Win32 — text drawn at the right end of the menu bar (XP FreeCell's "Cards Left: N",
 * docs/xp-reference/layout.md §8; FreeCell HD adds "Moves" and "Time").
 *
 * XP draws it through a window DC right-aligned to the client width, at y = SM_CYFRAME + SM_CYCAPTION
 * + (SM_CYMENU - tmHeight) / 2, in the menu font on the menu colour, never over the menu items. The
 * system repaints the menu bar on its own (WM_NCPAINT, WM_NCACTIVATE, WM_SETTEXT, menu loops, size
 * changes), so the game calls ce_menubar_draw after each of those and after the text changes, and
 * ce_menubar_reset when the bar was repainted from scratch.
 */
#ifndef CE_MENUBAR_H
#define CE_MENUBAR_H

#include <windows.h>

typedef struct CeMenuBarText {
    HFONT font;                 /* the menu font (NONCLIENTMETRICS lfMenuFont) */
    int   prev_left;            /* left edge of the last drawn text (to erase a longer old one) */
} CeMenuBarText;

void ce_menubar_init(CeMenuBarText *mt);          /* the font, and nothing drawn yet */
void ce_menubar_free(CeMenuBarText *mt);
void ce_menubar_font_update(CeMenuBarText *mt);   /* WM_SETTINGCHANGE / WM_SYSCOLORCHANGE */
void ce_menubar_reset(CeMenuBarText *mt);         /* the bar was repainted: nothing of ours is left */

/* Draw the first of texts[0..n-1] (most complete first) that fits between the menu items and the
 * right end (with a gap of about two characters); n >= 1, the last one is the fallback whose height
 * places the text. fullscreen: the window has no caption or frame (y from the bar's top). Nothing is
 * drawn for a missing, minimized or hidden window or menu. */
void ce_menubar_draw(CeMenuBarText *mt, HWND hwnd, HMENU menu, int fullscreen, const WCHAR *const *texts, int n);

#endif
