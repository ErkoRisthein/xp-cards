/*
 * FreeCell HD — the set of won deals (an extra; always on, silent). Platform independent.
 *
 * Remembers every game number the player has won: 1..1000000 and the special games -1 / -2. Shown in
 * the Select Game dialog ("You have won this game before.") and in Statistics ("Different games won").
 *
 * File format (all integers little-endian), designed to stay small for the usual few hundred wins:
 *
 *   offset 0   "FCWD"           magic
 *          4   u32 version      1
 *          8   u32 nbits        bits stored, 1..FC_WON_NBITS (one past the highest won index)
 *         12   u32 count        number of set bits (checked)
 *         16   u32 crc          CRC-32 (IEEE) of the bitset bytes
 *         20   ceil(nbits/8) bytes: bit i of byte i/8 (LSB first) = index i won
 *
 * Index = game + 2: -2 -> 0, -1 -> 1, (0 unused), 1..1000000 -> 3..1000002. A missing file is an
 * empty set; a damaged one (bad magic/version/size/count/CRC) is rejected as a whole.
 */
#ifndef FC_WONDEALS_H
#define FC_WONDEALS_H

#include <stddef.h>
#include <stdint.h>

#define FC_WON_NBITS     1000003u
#define FC_WON_HEADER    20u
#define FC_WON_MAX_FILE  (FC_WON_HEADER + (FC_WON_NBITS + 7u) / 8u)

typedef struct FcWonDeals {
    uint8_t *bits;           /* FC_WON_NBITS bits, allocated on first use (NULL = empty set) */
    uint32_t count;
} FcWonDeals;

/* Whole-file I/O for the persisted set (the Win32 layer maps it onto %APPDATA%; tests use memory). */
typedef struct FcBlobIO {
    void *ctx;
    /* Read the whole file into buf (at most cap bytes). Returns the number of bytes read, -1 if the file
     * does not exist or cannot be read, or cap + 1 (any value > cap) if it is larger than cap. */
    long (*read)(void *ctx, void *buf, size_t cap);
    /* Replace the file with data, atomically (temp file + rename). Returns 1 on success. */
    int  (*write)(void *ctx, const void *data, size_t len);
} FcBlobIO;

void     fc_won_init(FcWonDeals *w);                 /* empty set, nothing allocated */
void     fc_won_free(FcWonDeals *w);
int      fc_won_has(const FcWonDeals *w, int game);
int      fc_won_add(FcWonDeals *w, int game);        /* 1 if newly added; 0 if known / invalid / no memory */
uint32_t fc_won_count(const FcWonDeals *w);

/* Serialized form: returns the length written into out (out must hold FC_WON_MAX_FILE bytes). */
size_t   fc_won_serialize(const FcWonDeals *w, uint8_t *out);
/* Parse a file image; returns 1 and replaces *w on success, 0 if damaged (*w unchanged). */
int      fc_won_deserialize(FcWonDeals *w, const uint8_t *data, size_t len);

/* Load / save through io. Load: 1 = loaded, 0 = no file (empty set), -1 = damaged (empty set). */
int      fc_won_load(FcWonDeals *w, const FcBlobIO *io);
int      fc_won_save(const FcWonDeals *w, const FcBlobIO *io);   /* 1 on success */

uint32_t fc_crc32(const void *data, size_t len);

#endif
