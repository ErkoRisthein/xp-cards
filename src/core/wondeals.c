/*
 * FreeCell HD — the set of won deals (see wondeals.h for the file format).
 */
#include "wondeals.h"
#include "game.h"

#include <stdlib.h>
#include <string.h>

#define WON_BYTES ((FC_WON_NBITS + 7u) / 8u)

static int index_of(int game)
{
    if (game < FC_GAME_MIN || game > FC_GAME_MAX || game == 0) return -1;
    return game + 2;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t fc_crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

static unsigned popcount8(uint8_t b)
{
    unsigned n = 0;
    while (b) { n += b & 1u; b >>= 1; }
    return n;
}

void fc_won_init(FcWonDeals *w)
{
    w->bits = NULL;
    w->count = 0;
}

void fc_won_free(FcWonDeals *w)
{
    free(w->bits);
    fc_won_init(w);
}

int fc_won_has(const FcWonDeals *w, int game)
{
    int i = index_of(game);
    return i >= 0 && w->bits && (w->bits[i >> 3] >> (i & 7) & 1u);
}

int fc_won_add(FcWonDeals *w, int game)
{
    int i = index_of(game);
    if (i < 0) return 0;
    if (!w->bits) {
        w->bits = calloc(WON_BYTES, 1);
        if (!w->bits) return 0;
    }
    if (w->bits[i >> 3] >> (i & 7) & 1u) return 0;
    w->bits[i >> 3] |= (uint8_t)(1u << (i & 7));
    w->count++;
    return 1;
}

uint32_t fc_won_count(const FcWonDeals *w) { return w->count; }

size_t fc_won_serialize(const FcWonDeals *w, uint8_t *out)
{
    uint32_t nbits = 1, nbytes;
    if (w->bits) {
        uint32_t i = WON_BYTES;
        while (i > 0 && w->bits[i - 1] == 0) i--;
        if (i > 0) {
            uint8_t b = w->bits[i - 1];
            int hi = 7;
            while (!(b >> hi & 1u)) hi--;
            nbits = (i - 1) * 8u + (uint32_t)hi + 1u;
        }
    }
    nbytes = (nbits + 7u) / 8u;
    memcpy(out, "FCWD", 4);
    put32(out + 4, 1);
    put32(out + 8, nbits);
    put32(out + 12, w->count);
    if (w->bits) memcpy(out + FC_WON_HEADER, w->bits, nbytes);
    else memset(out + FC_WON_HEADER, 0, nbytes);
    put32(out + 16, fc_crc32(out + FC_WON_HEADER, nbytes));
    return FC_WON_HEADER + nbytes;
}

int fc_won_deserialize(FcWonDeals *w, const uint8_t *data, size_t len)
{
    if (!data || len < FC_WON_HEADER + 1 || memcmp(data, "FCWD", 4) != 0 || get32(data + 4) != 1) return 0;
    uint32_t nbits = get32(data + 8), count = get32(data + 12), nbytes, n = 0;
    if (nbits < 1 || nbits > FC_WON_NBITS) return 0;
    nbytes = (nbits + 7u) / 8u;
    if (len != FC_WON_HEADER + nbytes) return 0;
    const uint8_t *bits = data + FC_WON_HEADER;
    if (fc_crc32(bits, nbytes) != get32(data + 16)) return 0;
    if (nbits & 7u) {
        if (bits[nbytes - 1] >> (nbits & 7u)) return 0;          /* bits past nbits */
    }
    if (bits[0] & 4u) return 0;                                   /* index 2 = game 0 */
    for (uint32_t i = 0; i < nbytes; i++) n += popcount8(bits[i]);
    if (n != count) return 0;
    uint8_t *nb = calloc(WON_BYTES, 1);
    if (!nb) return 0;
    memcpy(nb, bits, nbytes);
    free(w->bits);
    w->bits = nb;
    w->count = count;
    return 1;
}

int fc_won_load(FcWonDeals *w, const FcBlobIO *io)
{
    fc_won_free(w);
    if (!io || !io->read) return 0;
    uint8_t *buf = malloc(FC_WON_MAX_FILE);
    if (!buf) return 0;
    long n = io->read(io->ctx, buf, FC_WON_MAX_FILE);
    int r;
    if (n < 0) r = 0;
    else r = (size_t)n <= FC_WON_MAX_FILE && fc_won_deserialize(w, buf, (size_t)n) ? 1 : -1;
    free(buf);
    return r;
}

int fc_won_save(const FcWonDeals *w, const FcBlobIO *io)
{
    if (!io || !io->write) return 0;
    uint8_t *buf = malloc(FC_WON_MAX_FILE);
    if (!buf) return 0;
    size_t n = fc_won_serialize(w, buf);
    int r = io->write(io->ctx, buf, n) == 1;
    free(buf);
    return r;
}
