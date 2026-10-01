/* Renders one card sprite with the UNCHANGED runtime code (image.c + cardset.c, included textually):
 * decode -> clean_master -> ce_image_resample(quality 1) ('box', v1.1) or ce_image_resample_card
 * (quality 1) ('card', v1.1.1 and later) -> ce_image_card_finish. Used by proto/check.py to verify
 * that crisplab.py models the runtime. usage: runtime_card <png> <ch> <out.raw> [box|card] */
#include <stdio.h>
#include <math.h>
#include "image.c"
#include "cardset.c"
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    static unsigned char buf[1 << 22];
    size_t len = fread(buf, 1, sizeof buf, f);
    int ch = atoi(argv[2]), cw = (int)floor(71 * ch / 96.0 + 0.5);
    CeImage *m, *s;
    fclose(f);
    m = ce_image_decode_png(buf, len);
    clean_master(m, CE_CARD_WHITE);
    s = argc > 4 && !strcmp(argv[4], "card") ? ce_image_resample_card(m, cw, ch, 1) : ce_image_resample(m, cw, ch, 1);
    ce_image_card_finish(s, 0.0372 * ch, frame_px(ch), CE_FRAME_COLOUR, CE_CARD_WHITE);
    f = fopen(argv[3], "wb");
    fwrite(s->px, 4, (size_t)cw * ch, f);
    fclose(f);
    printf("%d %d\n", cw, ch);
    return 0;
}
