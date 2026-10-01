/*
 * FreeCell HD — native asset loader for tests and snapshots (header-only): serves the card set's
 * asset ids from res/cards/<R><S>.png and res/king/king_{right,left,smile}.png.
 */
#ifndef FC_ASSETS_NATIVE_H
#define FC_ASSETS_NATIVE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx/cardset.h"

typedef struct FcNativeAssets {
    char   dir[512];
    void  *buf[55];   /* 0..51 cards, 52..54 kings */
    size_t len[55];
    int    tried[55];
} FcNativeAssets;

static void fc_native_assets_init(FcNativeAssets *a, const char *res_dir)
{
    memset(a, 0, sizeof *a);
    snprintf(a->dir, sizeof a->dir, "%s", res_dir ? res_dir : "res");
}

static void fc_native_assets_free(FcNativeAssets *a)
{
    int i;
    for (i = 0; i < 55; i++)
        free(a->buf[i]);
    memset(a->buf, 0, sizeof a->buf);
}

static void *fc_native_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    void *p = NULL;
    *len = 0;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        p = malloc((size_t)n);
        if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
            free(p);
            p = NULL;
        }
        if (p)
            *len = (size_t)n;
    }
    fclose(f);
    return p;
}

/* FcAssetLoader; ctx = FcNativeAssets*. Missing files return NULL. */
static const void *fc_native_asset_loader(int id, size_t *len, void *ctx)
{
    static const char ranks[] = "A23456789TJQK", suits[] = "CDHS";
    static const char *kings[3] = { "king_right", "king_left", "king_smile" };
    FcNativeAssets *a = (FcNativeAssets *)ctx;
    char path[600];
    int slot;
    if (id >= FC_ASSET_CARD0 && id < FC_ASSET_CARD0 + 52) {
        int c = id - FC_ASSET_CARD0;
        slot = c;
        snprintf(path, sizeof path, "%s/cards/%c%c.png", a->dir, ranks[c >> 2], suits[c & 3]);
    } else if (id >= FC_ASSET_KING_RIGHT && id <= FC_ASSET_KING_SMILE) {
        slot = 52 + id - FC_ASSET_KING_RIGHT;
        snprintf(path, sizeof path, "%s/king/%s.png", a->dir, kings[id - FC_ASSET_KING_RIGHT]);
    } else {
        *len = 0;
        return NULL;
    }
    if (!a->tried[slot]) {
        a->tried[slot] = 1;
        a->buf[slot] = fc_native_read_file(path, &a->len[slot]);
    }
    *len = a->len[slot];
    return a->buf[slot];
}

#endif
