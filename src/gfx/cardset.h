/*
 * FreeCell HD — card and king sprites at the current display size.
 *
 * Masters are 400x560 RGBA PNG card faces (res/cards/<R><S>.png, CC0 art) and square RGBA king
 * PNGs (res/king/*.png). Assets are fetched through a loader callback so the same code reads Win32
 * RCDATA resources in the exe and plain files in native tests.
 */
#ifndef FC_CARDSET_H
#define FC_CARDSET_H

#include <stddef.h>
#include "image.h"
#include "../core/game.h"

/* Asset ids passed to the loader. Card c (0..51, rank*4+suit) has id FC_ASSET_CARD0 + c. */
enum {
    FC_ASSET_CARD0      = 1000,          /* .. 1051 */
    FC_ASSET_KING_RIGHT = 1100,          /* small king looking right (XP "KingBitmap", default) */
    FC_ASSET_KING_LEFT  = 1101,          /* small king looking left  (XP "KingLeft") */
    FC_ASSET_KING_SMILE = 1102           /* winning king             (XP "KingSmile") */
};

/* Returns a pointer to the PNG bytes of asset id and stores its length; NULL if missing. The memory
 * must stay valid for the lifetime of the card set (resources/mmapped/loaded files). */
typedef const void *(*FcAssetLoader)(int asset_id, size_t *len, void *ctx);

typedef struct FcCardSet FcCardSet;

enum { FC_KING_RIGHT = 0, FC_KING_LEFT = 1, FC_KING_SMILE = 2 };

FcCardSet *fc_cardset_new(FcAssetLoader loader, void *ctx);   /* decodes masters (NULL on failure) */
void       fc_cardset_free(FcCardSet *cs);

/* Set the card cell size (cw x ch) and king sizes. Scaled sprites are rebuilt lazily on next use.
 * quality: 0 = fast (live resize), 1 = best. Calling again with the same sizes and quality is free. */
void fc_cardset_set_size(FcCardSet *cs, int cw, int ch, int king_px, int big_king_px, int quality);

/* Scaled card sprite (cw x ch, premultiplied, transparent rounded corners, crisp dark outline). */
const FcImage *fc_cardset_card(FcCardSet *cs, Card c);
/* Scaled king sprites: which = FC_KING_*; big = 0 small box king (king_px), 1 big win king. */
const FcImage *fc_cardset_king(FcCardSet *cs, int which, int big);

#endif
