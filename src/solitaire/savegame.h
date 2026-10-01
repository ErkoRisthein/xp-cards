/*
 * Solitaire HD — the saved game (the "Save game on exit" extra). Platform independent.
 *
 * At exit the game in progress is written whole: the board, the waste's fan, the score, the clock, the
 * recycles, the deal (seed and rand state: the win cascade continues from it), the mode it is played
 * in, whether it already counts in the statistics, and the whole undo and redo history. At the next
 * start it is restored exactly; the clock waits for the first press, as after a deal.
 *
 * File (%APPDATA%\xp-cards\Solitaire HD\game.bin through a CeBlobIO, written atomically; an empty file
 * means no saved game; all integers little-endian):
 *   "SOLG", u32 version 1, u32 payload length, the payload, u32 CRC-32 of the payload.
 *   payload: u32 Options (packed, rules.md §10: the draw, scoring and timed of the game), u8 draw,
 *     scoring, waste fan, undo_fresh, counted, reserved; 65-byte packed board; i32 score, ticks,
 *     recycles, clock_pen, carry; u32 seed, rng; u32 history count, redo count; then each action
 *     (history oldest first, then the redo stack bottom first): u8 type, src, dst, n, nsteps, autoturn,
 *     waste fan, then nsteps x (src, dst), the 65-byte board before it, i32 score, recycles, clock_pen.
 * Rejected as a whole when damaged (magic, version, lengths, CRC, a board that is not a Klondike
 * position, values out of range), and ignored when it was saved with other Options than the current
 * ones (Draw, Scoring or Timed game changed meanwhile, e.g. by XP's sol.exe, which shares them).
 */
#ifndef SOL_SAVEGAME_H
#define SOL_SAVEGAME_H

#include <stddef.h>
#include <stdint.h>

#include "engine/store.h"
#include "session.h"

#define SOL_SAVE_MAX_FILE (4u << 20)     /* 4 MiB: the oldest history is left out beyond it */

enum { SOL_LOAD_NONE = 0, SOL_LOAD_OK = 1, SOL_LOAD_DAMAGED = -1, SOL_LOAD_OTHER_OPTIONS = -2 };

/* The game in progress as a file image (malloc'ed, free it); 0 if there is none (nothing dealt, or the
 * game is won) or no memory. */
int sol_game_serialize(const SolSession *s, uint8_t **out, size_t *len);
/* Restore an image into s (sol_init'ed, a game dealt or not): SOL_LOAD_OK, SOL_LOAD_DAMAGED or
 * SOL_LOAD_OTHER_OPTIONS (s unchanged). A game in progress in s is replaced without a result. */
int sol_game_restore(SolSession *s, const uint8_t *data, size_t len);

/* Through io: save writes the game, or an empty file when there is none (so a stale game is never
 * resumed); load returns SOL_LOAD_* (SOL_LOAD_NONE: no file, or an empty one). */
int sol_game_save(const SolSession *s, const CeBlobIO *io);
int sol_game_load(SolSession *s, const CeBlobIO *io);
int sol_game_clear(const CeBlobIO *io);  /* write the empty file */

#endif
