/*
 * Card engine, Win32 — everything a game's Win32 front end uses (docs/ENGINE.md):
 *
 *   winutil.h   timing log, wide strings, RCDATA asset loader, clock seed, mouse, message loop
 *   backbuf.h   DIB back buffer + scratch DIB, painting, live-resize quality handling
 *   anim.h      frame-timed animation (timeBeginPeriod), straight flights
 *   drag.h      cards dragged with the mouse: the lifted stack, the zip-back, the drag threshold
 *   menubar.h   text at the right end of the menu bar
 *   window.h    window sizes, first-run rect, remembered placement, borderless full screen
 *   regstore.h  registry CeStore (XP's formats), registry blobs, %APPDATA% data file
 *   dialog.h    dialog centring / clamping to the work area
 *   help.h      HtmlHelp (.chm) and ShellAbout
 *   worker.h    one background worker thread
 */
#ifndef CE_SHELL_H
#define CE_SHELL_H

#include "winutil.h"
#include "backbuf.h"
#include "anim.h"
#include "drag.h"
#include "menubar.h"
#include "window.h"
#include "regstore.h"
#include "dialog.h"
#include "help.h"
#include "worker.h"

#endif
