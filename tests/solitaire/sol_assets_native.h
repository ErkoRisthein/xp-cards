/*
 * Solitaire HD — native asset loader for tests and snapshots (header-only): the engine's card faces
 * (CE_ASSET_CARD0 + c, <res>/common/cards/<R><S>.png, through tests/assets_native.h) and Solitaire's 12
 * backs (CE_ASSET_BACK0 + i, <res>/solitaire/backs/<54 + i>_<name>.png), as the exe's RCDATA resources.
 */
#ifndef SOL_ASSETS_NATIVE_H
#define SOL_ASSETS_NATIVE_H

#include <ctype.h>

#include "assets_native.h"
#include "solitaire/render.h"

typedef struct SolNativeAssets {
    FcNativeAssets faces;
    void  *buf[SOL_NBACKS];
    size_t len[SOL_NBACKS];
    int    tried[SOL_NBACKS];
} SolNativeAssets;

static void sol_native_assets_init(SolNativeAssets *a, const char *res_dir)
{
    memset(a, 0, sizeof *a);
    fc_native_assets_init(&a->faces, res_dir);
}

static void sol_native_assets_free(SolNativeAssets *a)
{
    int i;
    fc_native_assets_free(&a->faces);
    for (i = 0; i < SOL_NBACKS; i++)
        free(a->buf[i]);
    memset(a->buf, 0, sizeof a->buf);
}

/* CeAssetLoader; ctx = SolNativeAssets*. */
static const void *sol_native_asset_loader(int id, size_t *len, void *ctx)
{
    SolNativeAssets *a = (SolNativeAssets *)ctx;
    if (id >= CE_ASSET_BACK0 && id < CE_ASSET_BACK0 + SOL_NBACKS) {
        int i = id - CE_ASSET_BACK0, k;
        if (!a->tried[i]) {
            char name[32], path[600];
            for (k = 0; sol_back_names[i][k] && k < 31; k++)
                name[k] = (char)tolower((unsigned char)sol_back_names[i][k]);
            name[k] = 0;
            snprintf(path, sizeof path, "%s/solitaire/backs/%d_%s.png", a->faces.dir, 54 + i, name);
            a->tried[i] = 1;
            a->buf[i] = fc_native_read_file(path, &a->len[i]);
        }
        *len = a->len[i];
        return a->buf[i];
    }
    if (id >= CE_ASSET_CARD0 && id < CE_ASSET_CARD0 + 52)
        return fc_native_asset_loader(id, len, &a->faces);
    *len = 0;
    return NULL;
}

#endif
