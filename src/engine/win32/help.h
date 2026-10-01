/*
 * Card engine, Win32 — Help menu plumbing: XP's .chm files through HtmlHelp, and the About box.
 *
 * HtmlHelpW is loaded at run time from hhctrl.ocx (XP resolves it the same way). XP ships each game's
 * help as %windir%\Help\<game>.chm (freecell.chm, sol.chm) and NTHelp.chm for "How to Use Help"; a
 * game shows its own built-in text when ce_help_open_chm fails.
 */
#ifndef CE_HELP_H
#define CE_HELP_H

#include <windows.h>

#define CE_HH_DISPLAY_TOPIC 0x0000
#define CE_HH_DISPLAY_INDEX 0x0002

/* Open name (no path: HtmlHelp's own search of Windows\Help first, then %windir%\Help\name
 * explicitly) with HH_DISPLAY_TOPIC / HH_DISPLAY_INDEX. Returns 1 if a help window opened. */
int  ce_help_open_chm(HWND owner, const WCHAR *name, UINT cmd);

/* Close every help window before exit (WM_DESTROY). hhctrl.ocx is not unloaded: its worker thread
 * may still run, and unloading under it is a known crash; the process exit releases it. */
void ce_help_shutdown(void);

/* ShellAboutW(owner, title, other, icon) with both strings copied into writable buffers first: XP's
 * ShellAboutW splits "caption#first line" by writing a NUL over the '#' in the caller's buffer, so a
 * string literal (read-only .rdata) crashes it on real XP (Wine copies first, so it never shows
 * there). title is "caption#first line" (at most 127 characters), other at most 511. The text under
 * the copyright line holds two lines of about 270 px (180 x 20 DLU) on XP; more is clipped. */
void ce_shell_about(HWND owner, const WCHAR *title, const WCHAR *other, HICON icon);

#endif
