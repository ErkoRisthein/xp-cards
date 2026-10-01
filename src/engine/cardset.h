/*
 * Card engine — card sprites at the current display size: the 52 faces and, optionally, card backs.
 *
 * Masters are RGBA PNGs of the card's 5:7 shape (the faces: res/common/cards/<R><S>.png, 400x560,
 * CC0 art; see docs/card-art.md). They are fetched through a loader callback, so the same code reads
 * Win32 RCDATA resources in an exe and plain files in native tests. A game chooses the sprite size
 * (cw x ch; the art is stretched to it) and the quality.
 *
 * Card c (0..51) is rank * 4 + suit: rank 0 = A .. 12 = K, suit 0 = clubs, 1 = diamonds, 2 = hearts,
 * 3 = spades. That is the encoding of XP's cards.dll, FreeCell and Solitaire.
 *
 * Every sprite is cw x ch, premultiplied, opaque inside an exact rounded card shape with transparent
 * corners, with a crisp black frame drawn after scaling (the art's own thin outline vanishes when
 * downscaled; XP draws a black frame on every card). docs/DESIGN.md "Rendering" and "Crispness
 * decisions" describe the pipeline.
 */
#ifndef CE_CARDSET_H
#define CE_CARDSET_H

#include <stddef.h>
#include <stdint.h>
#include "image.h"

enum {
    CE_NCARDS      = 52,
    CE_MAX_BACKS   = 16,
    CE_ASSET_CARD0 = 1000,   /* card c: asset id 1000 + c (.. 1051) */
    CE_ASSET_GAME0 = 1100,   /* 1100 .. 1199: a game's own assets (FreeCell: its kings) */
    CE_ASSET_BACK0 = 1200    /* back i: asset id 1200 + i (.. 1215) */
};

/* Returns a pointer to the PNG bytes of asset id and stores its length; NULL if missing. The memory
 * must stay valid for the lifetime of the card set (resources/mmapped/loaded files). */
typedef const void *(*CeAssetLoader)(int asset_id, size_t *len, void *ctx);

typedef struct CeCardSet CeCardSet;

/* Decodes the 52 face masters (NULL if one is missing or memory runs out). nbacks (0 .. CE_MAX_BACKS)
 * card backs are available too; each is decoded on first use. */
CeCardSet *ce_cardset_new(CeAssetLoader loader, void *ctx, int nbacks);
void       ce_cardset_free(CeCardSet *cs);

/* Set the sprite size (cw x ch). Sprites are rebuilt lazily on next use. quality: 0 = fast (live
 * resize), 1 = best. Calling again with the same size and quality is free; so is asking for quality 0
 * at the size already built (the better sprites are kept). */
void ce_cardset_set_size(CeCardSet *cs, int cw, int ch, int quality);

/* Card c's face (0..51); NULL for a bad card, before the first ce_cardset_set_size, or out of memory. */
const CeImage *ce_cardset_card(CeCardSet *cs, int c);

/* Card back i (0 .. nbacks - 1); NULL if i is out of range or the back's asset is missing or broken
 * (it is not asked for again). A back master is processed like a face, except that the band along
 * its edge (the art's outline) takes the colour just inside it instead of white, so a back whose
 * colour runs to the edge keeps it up to the frame. */
const CeImage *ce_cardset_back(CeCardSet *cs, int i);

/* ce_bevel_ring_new(w, h, t, rad, tl, br), cached (the last few distinct rings are kept): empty-pile
 * outlines are redrawn on every render, their rings are built once per size. NULL without a card set. */
const CeImage *ce_cardset_bevel(CeCardSet *cs, int w, int h, double t, double rad, uint32_t tl, uint32_t br);

#endif
