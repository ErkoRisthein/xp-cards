/*
 * Card engine, Win32 — a push button drawn on the table (ROADMAP 2d: the Finish button, MSC's "Solve").
 *
 * The button is part of the board: the game draws it into its back buffer (ce_tbutton_draw, after the
 * board's own render of a region that meets it), so flying and dragged cards pass over it like over any
 * card, and nothing flickers. It looks like an XP push button: the visual style's (uxtheme.dll, loaded at
 * run time; normal, hot, pressed) when a theme is active, else the classic 3-D button (DrawFrameControl),
 * with its text in Tahoma scaled with the board. It never takes the keyboard focus. The mouse is the
 * game's to route (ce_tbutton_mouse): a press on it captures the mouse, the button shows pressed while
 * the pointer is over it, and a release over it is the click.
 */
#ifndef CE_BUTTON_H
#define CE_BUTTON_H

#include <windows.h>
#include "engine/geom.h"
#include "backbuf.h"

typedef struct CeTableButton {
    CeRect r;                    /* on the client; w = 0: no room for it in this layout */
    int    font_px;              /* text height in pixels */
    WCHAR  text[32];
    int    shown;                /* the game shows it now (the board region is redrawn on a change) */
    int    hot, pressed, armed;  /* the pointer is over it; pressed down; the press began on it */
    int    tracking;             /* TrackMouseEvent(TME_LEAVE) is pending */
    HFONT  font;
    int    font_made_px;
} CeTableButton;

/* Where the button goes in the new layout (r.w = 0: nowhere) and its text height. */
void ce_tbutton_place(CeTableButton *b, CeRect r, int font_px);
void ce_tbutton_set_text(CeTableButton *b, const WCHAR *text);
void ce_tbutton_free(CeTableButton *b);
/* The rect when shown (and placed), else an empty rect. */
CeRect ce_tbutton_rect(const CeTableButton *b);
/* Draw it into the back buffer, clipped to clip (the region the board was just rendered in). */
void ce_tbutton_draw(CeTableButton *b, CeBackBuf *bb, HWND hwnd, CeRect clip);

/* Mouse routing. msg is WM_LBUTTONDOWN / WM_LBUTTONDBLCLK / WM_MOUSEMOVE / WM_LBUTTONUP /
 * WM_MOUSELEAVE / WM_CAPTURECHANGED (lp of that message). Returns CE_TBUTTON_* bits: USED (the button
 * took the message: the game must not act on it), REDRAW (its look changed: redraw its rect),
 * CLICK (released over it: run its command). */
enum { CE_TBUTTON_USED = 1, CE_TBUTTON_REDRAW = 2, CE_TBUTTON_CLICK = 4 };
int  ce_tbutton_mouse(CeTableButton *b, HWND hwnd, UINT msg, int x, int y);
/* The button goes away (hidden, a new layout, a dialog): drop the press and the hover. Returns REDRAW
 * when the look changed. */
int  ce_tbutton_reset(CeTableButton *b, HWND hwnd);

#endif
