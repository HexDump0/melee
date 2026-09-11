#ifndef MELEE_NATIVE_EXTRAS_FONT_H
#define MELEE_NATIVE_EXTRAS_FONT_H

/* Tiny original 5x7 bitmap alphabet for the HUD; no font dependency.
 * The host renderer supplies font_rect(), which emits one lit font pixel
 * as a quad through whatever 2D path it uses (the port's shader overlay). */
void font_rect(float x, float y, float w, float h);

static const unsigned char font_glyphs[][7] = {
 {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
 {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
 {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
 {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
 {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
 {17,2,4,8,17,0,0}, /* % */
 {0,4,4,0,4,4,0}, /* : */
 {0,0,0,31,0,0,0}, /* - */
 {1,2,2,4,8,8,16}, /* / */
 {0,0,0,0,0,6,6}, /* . */
 {0,4,4,31,4,4,0}, /* + */
};

static void font_draw(float x, float y, float size, const char *s)
{
    for (; *s; ++s, x += 6 * size) {
        int idx = -1;
        unsigned char c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') idx = c - 'A';
        else if (c >= '0' && c <= '9') idx = c - '0' + 26;
        else if (c == '%') idx = 36;
        else if (c == ':') idx = 37;
        else if (c == '-') idx = 38;
        else if (c == '/') idx = 39;
        else if (c == '.') idx = 40;
        else if (c == '+') idx = 41;
        if (idx < 0) continue;
        for (int r = 0; r < 7; ++r) for (int col = 0; col < 5; ++col) {
            if (!(font_glyphs[idx][r] & (16 >> col))) continue;
            float a = x + col * size, b = y + r * size;
            font_rect(a, b, size, size);
        }
    }
}
#endif
