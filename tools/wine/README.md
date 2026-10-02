# fcdrive: an end-to-end driver for Windows GUI apps under Wine

`fcdrive.exe` runs a script of UI actions and checks against a Windows GUI program. The actions are launch,
click, keys, menu commands, dialogs, resize, registry, captures and asserts. It is written for FreeCell HD
and tested against the original XP `freecell.exe`, but nothing in it is FreeCell-specific. FreeCell knowledge
lives only in the scripts under `tests/e2e/`.

| File | Purpose |
|---|---|
| `fcdrive.c` | the driver: a 32-bit console program run by Wine |
| `fchook.c` | `fchook.dll`, the in-process helper that the hooks load into the target's GUI thread |
| `fcipc.h` | the shared-memory protocol between the two |
| `run.sh` | wrapper: builds both into `build/wine/`, prepares the Wine prefix, runs scripts, converts captures to PNG |

## Usage

    tools/wine/run.sh [-o OUTDIR] [-k] [-v] SCRIPT...        # captures go to OUTDIR (default build/e2e)
    tools/wine/run.sh --exec [-o OUTDIR] -e "launch x.exe" -e "capture a.bmp" ...   # ad hoc commands
    tools/wine/run.sh --exec -h                               # list of commands
    tools/wine/run.sh --build | --init                        # build only / prepare the prefix only
    tests/e2e/run_xp_original_smoke.sh                        # the smoke test against XP's freecell.exe

* **Prefix.** The default is `$SCRATCH/wineprefix-e2e`. It is created on first use (about 20 s) and set to
  `winxp` through `HKCU\Software\Wine\Version`. Override it with `WINEPREFIX=`. The Wine binary comes from
  `WINE=` (default: the Wine Devel.app in the scratchpad). `WINEDEBUG=-all` is set, and MoltenVK logging is
  silenced.
* **Exit status.** 0 when every command succeeded. 1 on the first failing command or assert; the rest of the
  script is skipped and the line is reported as `file:line: ERROR: ...`. 2 for usage errors.
* **Cleanup.** Every process the driver launched is terminated at the end of the run, also after an error, and
  a warning is printed if it had to be killed. Use `--keep` (driver option) to leave the processes running.
* **Captures.** These are 24-bit BMPs. `run.sh` converts the BMPs written during a run to PNG with ImageMagick,
  and `-k` keeps the BMPs.

## Script syntax

One command per line. `#` starts a comment. `"double quotes"` group words, with `\"` and `\\` as escapes inside
quotes. `${NAME}` expands an environment variable, and `${NAME:-default}` falls back to the default.
`run.sh` exports `${OUT}` and `${REPO}`. Unix absolute paths such as `/Users/...` are mapped to Wine's `Z:`
drive. Relative capture paths are relative to OUTDIR. Coordinates are client-area pixels.

The commands are listed below (`fcdrive.exe -h` has the full list with arguments).

| Group | Commands |
|---|---|
| process | `launch <exe> [args]` (cwd = exe dir, or `launch_dir`), `setenv <name> [value]` (the environment of later launches), `close [Yes\|No\|<button>\|none]`, `kill`, `wait_exit [ms]`, `assert_running` |
| timing | `wait <ms>`, `sync`, `timeout <ms>` (default 10000), `settle <ms>` (delay after each input, default 100) |
| window | `info`, `title`, `assert_title <t>`, `maximize`, `minimize`, `restore`, `resize_client <w> <h>`, `move_window <x> <y>`, `assert_client_size <w> <h>` |
| input | `command <id>`, `click`, `dblclick`, `ldown`, `lup`, `move`, `rclick`, `rclick_down`, `rclick_up` (all `<x> <y>`), `key <chars>`, `vkey <F5\|ESC\|ENTER\|code> [shift] [ctrl] [alt] [repeat N]` (a number is a VK code: the "2" key is `vkey 50`; `repeat N`: N key-downs, the later ones flagged as auto-repeat, as a held key sends them), `drag <x0> <y0> <x1> <y1> [steps]` (button down, `steps` moves with it held, button up), `hold <shift\|ctrl\|alt>...` / `release` (modifier keys held for the mouse input in between), `mouse_activate <hittest> <mouse msg>` (sends `WM_MOUSEACTIVATE`, which posted input never causes), `sendmsg <msg> <wparam> <lparam>` (SendMessage to the main window, e.g. a `WM_MENUSELECT`) |
| look | `capture <f.bmp>`, `capture_window <f.bmp>`, `capture_dialog <f.bmp>`, `capture_method dc\|print`, `pixel <x> <y>`, `assert_pixel <x> <y> <RRGGBB> [tol]`, `wait_pixel <x> <y> <RRGGBB> [tol] [ms]` (polls until it matches, e.g. a flashing card) |
| menu | `menu_state <id>`, `assert_menu <id> enabled\|grayed\|checked\|unchecked`, `menubar_text`, `assert_menubar_text <s>`, `drawn_text`, `window_text`, `assert_window_text <s>` |
| dialogs | `wait_dialog [title] [ms]`, `dialog_text`, `assert_dialog_text <s>`, `dialog_click <id\|caption>`, `dialog_set_text <id> <text>`, `dialog_check <id> <0\|1>`, `assert_no_dialog` |
| registry | `regdump <key>`, `regset <key> <name> dword\|bin32\|binary\|sz <data>`, `regdel <key> [name]`, `assert_reg <key> <name> <value\|absent>` |
| misc | `echo <text>`, `fail [msg]` |

* **Dialogs.** The "dialog" is the topmost visible top-level window of the target process other than its main
  window, for example a `#32770` dialog or a message box. `dialog_text` prints each control as
  `[id] Class "text" [x]/[ ] (default) (disabled)`, with `\n` and `\t` escaped; `assert_dialog_text` matches
  against that form. `dialog_click` accepts a button id or a caption, which is case-insensitive and ignores
  `&`. It posts `WM_COMMAND(id, BN_CLICKED)`.
* **`close`.** This posts `WM_CLOSE` and answers each confirmation with the given button. With `Yes` (the
  default) it succeeds when the process exits. With `No` or `Cancel` it succeeds when the process keeps
  running with no dialog left. With `none` it leaves the confirmation open.
* **`menubar_text`.** `fchook.dll` patches the target exe's IAT entries for `TextOut`, `ExtTextOut` and
  `DrawText` (A/W) and logs every string the application draws. `menubar_text` forces a full redraw,
  including the frame, and prints the strings that were drawn above the client area through a window DC. That
  is how XP FreeCell paints "Cards Left: N". If the app draws nothing there, the command prints a note and
  succeeds. `drawn_text` prints the whole log. `window_text` repaints the window and its children (e.g.
  Solitaire's status bar, drawn off screen) and prints the strings drawn in the client areas; with
  `assert_window_text` one of them must contain the given text.

## How it works, and the lessons from the research helpers

* The target is identified by the **process id** returned by `CreateProcess`. The driver never looks windows
  up by class or title, so leftover or foreign windows cannot be picked up by mistake. The main window is the
  first visible, unowned top-level window of that pid, and a captioned window is preferred.
* **Captures run inside the target.** Under Wine, `GetDC` + `BitBlt` on another process's window returns
  black. The driver therefore installs `WH_CALLWNDPROC` and `WH_GETMESSAGE` hooks for the target's GUI
  thread, which loads `fchook.dll` into it. It then sends a registered message, and the hook does the
  `GetDC`/`GetWindowDC` + `BitBlt` (or `PrintWindow` with `capture_method print`) inside the process and
  writes the BMP. Pixel reads, menu-item state, the modifier key state and the redraw all run the same way.
  Shared memory `fcdrive_ipc_<pid>` carries the arguments and results.
* **Synchronisation.** Input is posted to the target's queue with `PostMessage`. After each input the
  driver posts a marker message. When the target thread retrieves it, the `WH_GETMESSAGE` hook signals the
  event `fcdrive_sync_<pid>`, and at that point every message posted earlier has been dispatched. Modal loops
  (dialogs, message boxes) also pump the marker. After the sync the driver waits `settle` ms, which covers
  timer-driven animation; use `wait` for anything longer.
* **Modifier keys.** `vkey ... ctrl shift` sets the target thread's keyboard state with `SetKeyboardState`
  from inside the hook, posts `WM_KEYDOWN`/`WM_KEYUP`, waits for the sync and then restores the state. Code
  that reads modifiers with `GetKeyState`, as `TranslateAccelerator` does for Ctrl+Shift+F10, therefore sees
  them.

## Limitations

* Mouse input is posted, not injected. `GetCursorPos`, `GetAsyncKeyState` and `GetKeyState(VK_LBUTTON)` in the
  target do not reflect it. The real macOS cursor is never moved.
* `capture_method print` (`PrintWindow`) only works for windows that handle `WM_PRINTCLIENT`. XP FreeCell
  does not, so its capture shows only the table colour. Keep the default `dc` method.
* `capture_window` under the macOS Wine driver shows the caption area black, because the title bar is drawn
  by macOS. The menu bar and the frame are captured.
* XP FreeCell limits its window width to 640 px (`WM_GETMINMAXINFO`), so `resize_client` reports an error
  when it asks for more.

## Examples

* `tests/e2e/xp_original_smoke.txt` covers: launch, Select Game #1 through the dialog, capture, select a
  card (checks the inverted pixel), read Statistics, exit answering the resign prompt, and check the
  registry. Run it with `tests/e2e/run_xp_original_smoke.sh`.
* `tests/e2e/xp_original_driver_selftest.txt` covers: accelerators, Ctrl+Shift+F10, digit keys, the
  right-button peek, check boxes, registry seeding, resize/maximize/restore, `close No`, and the text log.
