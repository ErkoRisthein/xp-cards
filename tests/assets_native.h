/*
 * Native asset loader for tests and snapshots (header-only): serves the engine's card faces
 * (CE_ASSET_CARD0 + c) from <res>/common/cards/<R><S>.png, the Large Print faces (CE_ASSET_LARGE0 + c)
 * from <res>/common/cards-large/<R><S>.png and FreeCell's kings (FC_ASSET_KING_*) from
 * <res>/freecell/king/king_{right,left,smile}.png, as the exes' RCDATA resources do.
 */
#ifndef FC_ASSETS_NATIVE_H
#define FC_ASSETS_NATIVE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/cardset.h"

typedef struct FcNativeAssets {
    char   dir[512];
    void  *buf[107];  /* 0..51 cards, 52..54 kings, 55..106 Large Print cards */
    size_t len[107];
    int    tried[107];
} FcNativeAssets;

static void fc_native_assets_init(FcNativeAssets *a, const char *res_dir)
{
    memset(a, 0, sizeof *a);
    snprintf(a->dir, sizeof a->dir, "%s", res_dir ? res_dir : "res");
}

static void fc_native_assets_free(FcNativeAssets *a)
{
    int i;
    for (i = 0; i < 107; i++)
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

/* CeAssetLoader; ctx = FcNativeAssets*. Missing files and other ids return NULL. */
static const void *fc_native_asset_loader(int id, size_t *len, void *ctx)
{
    static const char ranks[] = "A23456789TJQK", suits[] = "CDHS";
    static const char *kings[3] = { "king_right", "king_left", "king_smile" };
    FcNativeAssets *a = (FcNativeAssets *)ctx;
    char path[600];
    int slot;
    if (id >= CE_ASSET_CARD0 && id < CE_ASSET_CARD0 + 52) {
        int c = id - CE_ASSET_CARD0;
        slot = c;
        snprintf(path, sizeof path, "%s/common/cards/%c%c.png", a->dir, ranks[c >> 2], suits[c & 3]);
    } else if (id >= CE_ASSET_LARGE0 && id < CE_ASSET_LARGE0 + 52) {
        int c = id - CE_ASSET_LARGE0;
        slot = 55 + c;
        snprintf(path, sizeof path, "%s/common/cards-large/%c%c.png", a->dir, ranks[c >> 2], suits[c & 3]);
    } else if (id >= CE_ASSET_GAME0 && id <= CE_ASSET_GAME0 + 2) {     /* FreeCell's FC_ASSET_KING_* */
        slot = 52 + id - CE_ASSET_GAME0;
        snprintf(path, sizeof path, "%s/freecell/king/%s.png", a->dir, kings[id - CE_ASSET_GAME0]);
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
