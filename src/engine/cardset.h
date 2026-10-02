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
 * Two face sets: the normal faces (asset CE_ASSET_CARD0 + c) and the Large Print faces (CE_ASSET_LARGE0 + c,
 * res/common/cards-large: the same art with a 1.45x index, its suit beside the rank; docs/card-art.md). One
 * set is in use at a time; only its masters are decoded (switching drops the other's).
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
    CE_ASSET_BACK0 = 1200,   /* back i: asset id 1200 + i (.. 1215) */
    CE_ASSET_LARGE0 = 1300   /* card c's Large Print face: asset id 1300 + c (.. 1351) */
};
enum { CE_FACES_NORMAL = 0, CE_FACES_LARGE = 1 };

/* Returns a pointer to the PNG bytes of asset id and stores its length; NULL if missing. The memory
 * must stay valid for the lifetime of the card set (resources/mmapped/loaded files). */
typedef const void *(*CeAssetLoader)(int asset_id, size_t *len, void *ctx);

typedef struct CeCardSet CeCardSet;

/* Decodes the 52 face masters (NULL if one is missing or memory runs out). nbacks (0 .. CE_MAX_BACKS)
 * card backs are available too; each is decoded on first use. */
CeCardSet *ce_cardset_new(CeAssetLoader loader, void *ctx, int nbacks);
/* The same with the face set to start with (CE_FACES_*): the Large Print set when all 52 of its assets
 * are there (one that does not decode: that card's normal face), else the normal set (ce_cardset_faces
 * tells which). */
CeCardSet *ce_cardset_new_faces(CeAssetLoader loader, void *ctx, int nbacks, int faces);
void       ce_cardset_free(CeCardSet *cs);

/* Switch the face set (CE_FACES_*). The faces' sprites and masters are dropped; the new set's masters are
 * decoded on demand, card by card, as its sprites are built (the next render), under the same memory
 * policy. Returns 1 if that set is in use now, 0 if its assets are missing (nothing changed). A Large
 * Print face that fails to decode later falls back to the normal face. Backs are not affected. */
int ce_cardset_set_faces(CeCardSet *cs, int faces);
int ce_cardset_faces(const CeCardSet *cs);        /* CE_FACES_*; CE_FACES_NORMAL for NULL */

/* The column step (px) at which a stacked Large Print face shows its whole index, rank and suit: round(21 ch
 * / 96) (the index ends at 0.192 ch, the pips and the court picture start at 0.23 ch; tools/crisplab
 * stack.json XPLIKE_BITTER_HYBRID_LARGE). Both games use it for face-up columns with Large Print on. */
static inline int ce_large_print_step(int ch) { return (21 * ch + 48) / 96; }

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
