/*
 * FreeCell HD — the board's sprites at the current display size: the engine's card faces
 * (engine/cardset.h) plus FreeCell's three kings.
 *
 * King masters are square RGBA PNGs (res/freecell/king/king_{right,left,smile}.png, asset ids
 * FC_ASSET_KING_*), fetched through the same loader as the card faces.
 */
#ifndef FC_SPRITES_H
#define FC_SPRITES_H

#include <stddef.h>
#include <stdint.h>
#include "engine/cardset.h"
#include "engine/image.h"
#include "game.h"

/* FreeCell's own asset ids (the engine's game range). */
enum {
    FC_ASSET_KING_RIGHT = CE_ASSET_GAME0 + 0,   /* 1100: small king looking right (XP "KingBitmap", default) */
    FC_ASSET_KING_LEFT  = CE_ASSET_GAME0 + 1,   /* 1101: small king looking left  (XP "KingLeft") */
    FC_ASSET_KING_SMILE = CE_ASSET_GAME0 + 2    /* 1102: winning king             (XP "KingSmile") */
};

typedef struct FcCardSet FcCardSet;

enum { FC_KING_RIGHT = 0, FC_KING_LEFT = 1, FC_KING_SMILE = 2 };

/* Decodes the kings, then the 52 card faces (NULL if a face is missing; a missing king is drawn as a
 * pixel-art placeholder). */
FcCardSet *fc_cardset_new(CeAssetLoader loader, void *ctx);
/* The same with the engine face set to start with (CE_FACES_*: the Large Print faces, extras.large_print). */
FcCardSet *fc_cardset_new_faces(CeAssetLoader loader, void *ctx, int faces);
void       fc_cardset_free(FcCardSet *cs);

/* Set the card cell size (cw x ch) and king sizes. Scaled sprites are rebuilt lazily on next use.
 * quality: 0 = fast (live resize), 1 = best. Calling again with the same sizes and quality is free;
 * so is asking for quality 0 at the sizes already built (the better sprites are kept). */
void fc_cardset_set_size(FcCardSet *cs, int cw, int ch, int king_px, int big_king_px, int quality);

/* Scaled card sprite (ce_cardset_card: cw x ch, premultiplied, transparent rounded corners, crisp
 * dark outline). */
const CeImage *fc_cardset_card(FcCardSet *cs, Card c);
/* Scaled king sprites: which = FC_KING_*; big = 0 small box king (king_px), 1 big win king. */
const CeImage *fc_cardset_king(FcCardSet *cs, int which, int big);

/* The engine card set inside (for its bevel-ring cache, ce_draw_ring); NULL for NULL. */
CeCardSet *fc_cardset_cards(FcCardSet *cs);

/* ce_cardset_bevel on the card set inside (NULL for NULL). */
const CeImage *fc_cardset_bevel(FcCardSet *cs, int w, int h, double t, double rad, uint32_t tl, uint32_t br);

#endif
