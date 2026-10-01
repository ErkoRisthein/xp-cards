/*
 * Solitaire HD — native tests of the win cascade (src/solitaire/cascade.c) against the real sol.exe:
 * every frame of all 52 cards of three logged wins (layout.md §8; gdilog captures of cdtDrawExt under
 * Wine, 585 x 384 client). The deal seeds were recovered by brute force: the cascade's rand() continues
 * from the deal (srand(seed) + the 260 shuffle calls), and these seeds reproduce all 156 trajectories.
 * Each card: its id, the first frame's position, the number of frames, the last frame's position and a
 * hash of all positions (h = h * 31 + (x & 0xFFFF) * 65537 + (y & 0xFFFF), mod 2^32).
 */
#include "sol_test.h"
#include "solitaire/cascade.h"

typedef struct CascadeCard {
    int card, x0, y0, frames, x1, y1;
    uint32_t hash;
} CascadeCard;

/* natural win 1: deal seed 29300, 8145 frames (<sr>/visuals/natwin_gdilog.txt) */
static const CascadeCard cascade0[52] = {
    {48,263,8,167,-69,186,0xD99C7E0Cu}, {49,345,8,104,-67,74,0xB837CCA9u},
    {50,427,8,53,583,175,0xC6310D99u}, {51,509,8,290,-69,254,0xDE6E3926u},
    {44,261,7,111,-69,277,0x94C16F36u}, {45,343,7,104,-69,82,0xF68D8A3Au},
    {46,425,7,124,-67,280,0x895AAC7Au}, {47,507,7,289,-69,258,0xDB4A4398u},
    {40,261,7,166,-69,183,0xD4E4AFFCu}, {41,343,7,104,-69,148,0x89F9B972u},
    {42,425,7,496,-70,289,0x2EA73F39u}, {43,507,7,39,583,261,0x3336FA09u},
    {36,261,7,166,-69,170,0x231EA14Au}, {37,343,7,207,-69,219,0xD447415Eu},
    {38,425,7,160,584,165,0x0862CA60u}, {39,507,7,289,-69,257,0x4BC1B1B0u},
    {32,261,7,83,-67,106,0x07A18134u}, {33,343,7,207,-69,292,0x0A0338ECu},
    {34,425,7,100,-70,141,0x3605FB4Fu}, {35,507,7,26,582,108,0xDA931FDBu},
    {28,259,6,165,-69,227,0xA95BAAF2u}, {29,341,6,103,-67,131,0x15705036u},
    {30,423,6,124,-69,163,0xD738BEFCu}, {31,505,6,192,-68,218,0x5B64B32Fu},
    {24,259,6,66,-66,155,0x55A8E687u}, {25,341,6,206,-69,145,0xAB185EA3u},
    {26,423,6,247,-69,200,0x5D817F89u}, {27,505,6,27,583,-2,0xA53648C4u},
    {20,259,6,109,583,108,0x188B63C6u}, {21,341,6,122,583,290,0x8DC8C7B2u},
    {22,423,6,247,-69,235,0x2CEEBB29u}, {23,505,6,288,-69,252,0xAF40D308u},
    {16,259,6,66,-66,209,0x294ACB18u}, {17,341,6,83,-69,122,0x909898ECu},
    {18,423,6,247,-69,267,0x3B6B00CCu}, {19,505,6,288,-69,253,0x1EA2F030u},
    {12,257,5,164,-69,165,0xB035126Bu}, {13,339,5,69,-69,117,0x32310289u},
    {14,421,5,99,-69,87,0xC8DA1F45u}, {15,503,5,287,-69,255,0x09EBA153u},
    {8,257,5,164,-69,163,0xF5CDC2D6u}, {9,339,5,205,-69,211,0x9472C63Au},
    {10,421,5,41,581,166,0x3CAA6A8Eu}, {11,503,5,41,583,36,0xFBB893D6u},
    {4,257,5,164,-69,180,0x3A6B0DF1u}, {5,339,5,82,-66,103,0x1E28939Eu},
    {6,421,5,246,-69,295,0xF1BD21ADu}, {7,503,5,144,-69,227,0x45875016u},
    {0,257,5,164,-69,183,0x2FAD27A9u}, {1,339,5,82,582,133,0x5BA981E0u},
    {2,421,5,246,-69,210,0x47622075u}, {3,503,5,82,584,156,0x52CD6FEBu},
};
/* natural win 2: deal seed 29785, 9416 frames (<sr>/visuals/natwin2_gdilog.txt) */
static const CascadeCard cascade1[52] = {
    {48,263,8,322,584,270,0x7FF99E41u}, {49,345,8,416,-70,284,0x40A1EA45u},
    {50,427,8,83,-65,106,0x38F3FAF4u}, {51,509,8,580,-70,289,0x91F14107u},
    {44,261,7,108,582,188,0x157A8AA3u}, {45,343,7,83,-67,105,0x943633A7u},
    {46,425,7,160,584,200,0x6CBA3F90u}, {47,507,7,26,582,60,0xEFA05A23u},
    {40,261,7,166,-69,224,0xFDF44EDFu}, {41,343,7,69,-65,161,0x35F985A3u},
    {42,425,7,248,-69,241,0x177BD079u}, {43,507,7,289,-69,257,0x07B1578Eu},
    {36,261,7,166,-69,174,0xA5A26362u}, {37,343,7,83,-67,197,0x7E3A27D5u},
    {38,425,7,166,-70,165,0x3CF63F01u}, {39,507,7,145,-69,157,0xD685B8A9u},
    {32,261,7,56,-69,179,0xB9564ED0u}, {33,343,7,81,583,103,0xCE1CACEEu},
    {34,425,7,54,584,205,0x1D5035D7u}, {35,507,7,289,-69,254,0x6789CA78u},
    {28,259,6,330,-70,266,0x61D8B986u}, {29,341,6,82,584,169,0x4CE6362Fu},
    {30,423,6,494,-70,288,0xA75F20DBu}, {31,505,6,288,-69,236,0x82229A53u},
    {24,259,6,165,-69,240,0x575EFE58u}, {25,341,6,206,-69,212,0xF1D2CAF2u},
    {26,423,6,247,-69,260,0xB2A6C7BCu}, {27,505,6,27,583,-16,0x0E522C5Du},
    {20,259,6,83,-69,129,0xAAF7E07Bu}, {21,341,6,206,-69,283,0xA0583E42u},
    {22,423,6,247,-69,283,0x916904FEu}, {23,505,6,288,-69,257,0x333F24AEu},
    {16,259,6,326,584,251,0x27B75165u}, {17,341,6,82,584,108,0xE4D6CA13u},
    {18,423,6,83,-69,107,0x0C9E7B70u}, {19,505,6,116,-70,172,0x5250F88Bu},
    {12,257,5,66,-68,259,0x9FCBB99Cu}, {13,339,5,123,583,234,0xB877AFE1u},
    {14,421,5,246,-69,243,0xC3DACE7Fu}, {15,503,5,144,-69,161,0x2EB9B0C7u},
    {8,257,5,82,-67,140,0xE45B9862u}, {9,339,5,410,-70,270,0xFD5E7DD8u},
    {10,421,5,55,583,208,0xF92CAA58u}, {11,503,5,192,-70,252,0x6CE7F6E1u},
    {4,257,5,328,-70,281,0xFC78B225u}, {5,339,5,62,583,161,0x3E831D2Bu},
    {6,421,5,164,-68,181,0x17520FDAu}, {7,503,5,41,583,237,0x0D12E41Au},
    {0,257,5,164,-69,216,0xB56D4EC5u}, {1,339,5,205,-69,213,0x33753A7Au},
    {2,421,5,246,-69,262,0x1749E892u}, {3,503,5,28,584,-29,0x9DB9BBB8u},
};
/* Alt+Shift+2 win: deal seed 28087, 8228 frames (<sr>/visuals/win1_gdilog.txt) */
static const CascadeCard cascade2[52] = {
    {48,257,5,66,-68,117,0x4E646CF9u}, {49,339,5,82,582,207,0x2292A282u},
    {50,421,5,55,583,110,0x46AAACB4u}, {51,503,5,28,584,32,0xEC380918u},
    {44,257,5,110,-70,219,0xE826F10Eu}, {45,339,5,205,-69,210,0x63114156u},
    {46,421,5,123,-67,248,0x9FFD64D7u}, {47,503,5,287,-69,231,0x5E54E70Au},
    {40,257,5,164,-69,209,0x6F3BE104u}, {41,339,5,205,-69,222,0x61C17D13u},
    {42,421,5,246,-69,243,0xC3DACE7Fu}, {43,503,5,144,-69,170,0x6B452DDEu},
    {36,257,5,328,-70,285,0x84C2C55Fu}, {37,339,5,205,-69,214,0xE7F394ABu},
    {38,421,5,55,583,150,0x7122B426u}, {39,503,5,28,584,-74,0xB203BE20u},
    {32,257,5,164,-69,174,0xB6760C70u}, {33,339,5,205,-69,211,0x647728ACu},
    {34,421,5,246,-69,239,0xE48952BDu}, {35,503,5,28,584,96,0x9A7A2899u},
    {28,257,5,110,-70,130,0xA3B5CD61u}, {29,339,5,246,584,280,0x7D11F375u},
    {30,421,5,246,-69,260,0x32C299BFu}, {31,503,5,287,-69,258,0x373ACD24u},
    {24,257,5,164,-69,165,0xA214787Eu}, {25,339,5,82,-66,104,0x16A154FFu},
    {26,421,5,99,-69,128,0xC17FECAEu}, {27,503,5,41,583,229,0xABA6EA16u},
    {20,257,5,82,-67,100,0x741E4973u}, {21,339,5,205,-69,212,0x1A4FD37Au},
    {22,421,5,246,-69,206,0x8A6DD724u}, {23,503,5,96,-67,94,0xEDB98344u},
    {16,257,5,110,584,74,0xFDCF63A0u}, {17,339,5,137,-69,165,0xB1F4671Du},
    {18,421,5,246,-69,198,0x787FFF24u}, {19,503,5,96,-67,100,0xB7BA2E08u},
    {12,257,5,82,581,130,0xC74422C8u}, {13,339,5,82,582,104,0xBED1ED47u},
    {14,421,5,246,-69,237,0x93A7BF1Cu}, {15,503,5,96,-67,120,0x791873B5u},
    {8,257,5,82,581,112,0x8A708D1Fu}, {9,339,5,205,-69,210,0x750A600Cu},
    {10,421,5,82,583,108,0x6D57837Fu}, {11,503,5,287,-69,259,0x4FF66B04u},
    {4,257,5,164,583,166,0x10A7DEC8u}, {5,339,5,103,-69,73,0xD0030C75u},
    {6,421,5,492,-70,286,0x95760290u}, {7,503,5,115,-67,148,0x242B128Fu},
    {0,257,5,110,-70,243,0xA24C518Eu}, {1,339,5,205,-69,282,0x022644FAu},
    {2,421,5,123,-67,259,0x35C93DFCu}, {3,503,5,287,-69,251,0xF42761F6u},
};

static int run_logged(const CascadeCard *v, unsigned seed, int forced)
{
    SolSession s;
    Fake f;
    Reg r;
    SolCascade c;
    int fd, rank, x, y, bad = 0, total = 0;
    start(&s, &f, &r, 0, (int)seed);
    sol_cascade_init(&c, s.rng, 585, 384, 71, 96);
    for (int k = 0; sol_cascade_next(&c, &fd, &rank); k++) {
        const CascadeCard *e = &v[k];
        /* order: K..A, foundations left to right; here foundation f holds suit f */
        if (rank != 12 - k / 4 || fd != k % 4 || e->card != rank * 4 + fd) bad++;
        /* where the card lies: the foundation's origin (257 + 82 f, 5), plus the 3-D edge (+2, +1 per
         * 4 cards) in a natural win; the origin after Alt+Shift+2 */
        int ox = 257 + 82 * fd + (forced ? 0 : 2 * (rank / 4)), oy = 5 + (forced ? 0 : rank / 4);
        if (ox != e->x0 || oy != e->y0) bad++;
        sol_cascade_place(&c, e->x0, e->y0);
        int n = 0, lx = 0, ly = 0;
        uint32_t h = 0;
        while (sol_cascade_frame(&c, &x, &y)) {
            h = h * 31u + (uint32_t)(x & 0xFFFF) * 65537u + (uint32_t)(y & 0xFFFF);
            lx = x;
            ly = y;
            n++;
        }
        total += n;
        if (n != e->frames || lx != e->x1 || ly != e->y1 || h != e->hash) {
            bad++;
            printf("  seed %u card %d: %d frames to (%d,%d) h %08X, expected %d to (%d,%d) h %08X\n", seed, k, n,
                   lx, ly, h, e->frames, e->x1, e->y1, e->hash);
        }
        CHECK(!sol_cascade_frame(&c, &x, &y));     /* stays finished */
    }
    sol_free(&s);
    CHECK_EQ(c.next, 52);
    CHECK(!sol_cascade_next(&c, &fd, &rank));
    CHECK_EQ(bad, 0);
    return total;
}

/* The model's properties on other canvases: every card leaves, velocities in XP's ranges, a bounce
 * keeps 80 % and never sinks below the floor by more than one step. */
static void test_properties(void)
{
    for (unsigned seed = 0; seed < 32768; seed += 97) {
        SolBoard b;
        uint32_t rng;
        SolCascade c;
        int fd, rank, x, y;
        sol_deal_board(&b, seed, &rng);
        int w = 300 + (int)(seed % 1500), h = 200 + (int)(seed % 900);
        sol_cascade_init(&c, rng, w, h, 71, 96);
        int ok = 1;
        while (sol_cascade_next(&c, &fd, &rank)) {
            if (c.vx < -65 || c.vx > 44 || (c.vx > -15 && c.vx < 15) || c.vy < -75 || c.vy > 34) ok = 0;
            sol_cascade_place(&c, w / 2, 5);
            int n = 0;
            while (sol_cascade_frame(&c, &x, &y)) {
                if (y > h - 96 + 40) ok = 0;            /* one falling step at most */
                if (++n > 100000) { ok = 0; break; }
            }
            if (!(c.x <= -71 || c.x >= w)) ok = 0;
        }
        CHECK(ok);
    }
}

int main(void)
{
    CHECK_EQ(run_logged(cascade0, 29300, 0), 8145);
    CHECK_EQ(run_logged(cascade1, 29785, 0), 9416);
    CHECK_EQ(run_logged(cascade2, 28087, 1), 8228);
    test_properties();
    return test_summary("test_sol_cascade");
}
