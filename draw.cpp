#include "draw.h"

#include "unicode.h"
#include "string_utilities.h"

#include <alloca.h> // @Incomplete: remove dependency on linux-specific stack allocation

#include <cassert>
#include <cmath>

// A colour table maximising the colour difference between each value and all
// of the others. CIEDE2000 was used as the formula for comparison.
const u32 distinct_colour_table[64] = {
    0x000000, 0x00FF00, 0x0000FF, 0xFF0000, 0x01FFFE, 0xFFA6FE, 0xFFDB66, 0x006401,
    0x010067, 0x95003A, 0x007DB5, 0xFF00F6, 0xFFEEE8, 0x774D00, 0x90FB92, 0x0076FF,
    0xD5FF00, 0xFF937E, 0x6A826C, 0xFF029D, 0xFE8900, 0x7A4782, 0x7E2DD2, 0x85A900,
    0xFF0056, 0xA42400, 0x00AE7E, 0x683D3B, 0xBDC6FF, 0x263400, 0xBDD393, 0x00B917,
    0x9E008E, 0x001544, 0xC28C9F, 0xFF74A3, 0x01D0FF, 0x004754, 0xE56FFE, 0x788231,
    0x0E4CA1, 0x91D0CB, 0xBE9970, 0x968AE8, 0xBB8800, 0x43002C, 0xDEFF74, 0x00FFC6,
    0xFFE502, 0x620E00, 0x008F9C, 0x98FF52, 0x7544B1, 0xB500FF, 0x00FF78, 0xFF6E41,
    0x005F39, 0x6B6882, 0x5FAD4E, 0xA75740, 0xA5FFD2, 0xFFB167, 0x009BFF, 0xE85EBE,
};

void canvas_fill(Canvas* canvas, u32 colour) {
    int pixel_count = canvas->width * canvas->height;
    for (int i = 0; i < pixel_count; ++i) {
        canvas->pixels[i] = colour;
    }
}

static inline void set_pixel(Canvas* canvas, int x, int y, u32 value) {
    assert(x >= 0 && x < canvas->width);
    assert(y >= 0 && y < canvas->height);
    canvas->pixels[y * canvas->width + x] = value;
}

#define GET_ALPHA(colour) ((colour) >> 24 & 0xFF)
#define GET_RED(colour)   ((colour) >> 16 & 0xFF)
#define GET_GREEN(colour) ((colour) >> 8 & 0xFF)
#define GET_BLUE(colour)  ((colour) & 0xFF)

#define PACK_RGB(r, g, b) \
    ((r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF))

#define PACK_RGBA(r, g, b, a) \
    ((a & 0xFF) << 24 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF))

static inline void set_pixel_alpha(Canvas* canvas, int x, int y, u32 value) {

    assert(x >= 0 && x < canvas->width);
    assert(y >= 0 && y < canvas->height);

    u8 alpha = GET_ALPHA(value);
    u32 a = alpha + 1;    // alpha
    u32 ia = 256 - alpha; // inverse alpha

    int index = y * canvas->width + x;
    u32 background = canvas->pixels[index];
    u32 br = GET_RED(background);
    u32 bg = GET_GREEN(background);
    u32 bb = GET_BLUE(background);

    u32 fr = GET_RED(value); // foreground colour
    u32 fg = GET_GREEN(value);
    u32 fb = GET_BLUE(value);

    canvas->pixels[index] = PACK_RGB((a * fr + ia * br) >> 8,
                                     (a * fg + ia * bg) >> 8,
                                     (a * fb + ia * bb) >> 8);
}

static int mod(int x, int m) {
    return (x % m + m) % m;
}

void draw_subimage(Canvas* canvas, Atlas* atlas, int cx, int cy,
                   int tx, int ty, int width, int height) {
    if (cx < 0) {
        tx -= cx;
        width += cx;
        if (width > 0) {
            cx = 0;
        }
    }

    int extra_width = (cx + width) - canvas->width;
    if (extra_width > 0) {
        width -= extra_width;
    }

    if (cy < 0) {
        ty -= cy;
        height += cy;
        if (height > 0) {
            cy = 0;
        }
    }

    int extra_height = (cy + height) - canvas->height;
    if (extra_height > 0) {
        height -= extra_height;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int atlas_x = mod(tx + x, atlas->width);
            int atlas_y = mod(ty + y, atlas->height);
            int ai = atlas_y * atlas->width + atlas_x;
            u32 c = reinterpret_cast<u32*>(atlas->data)[ai];
            if (GET_ALPHA(c)) {
                set_pixel(canvas, cx + x, cy + y, c);
            }
        }
    }
}

void draw_rectangle(Canvas* canvas, int cx, int cy, int width, int height,
                    u32 colour) {
    if (cx < 0) {
        width += cx;
        if (width > 0) {
            cx = 0;
        }
    }

    int extra_width = (cx + width) - canvas->width;
    if (extra_width > 0) {
        width -= extra_width;
    }

    if (cy < 0) {
        height += cy;
        if (height > 0) {
            cy = 0;
        }
    }

    int extra_height = (cy + height) - canvas->height;
    if (extra_height > 0) {
        height -= extra_height;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            set_pixel(canvas, cx + x, cy + y, colour);
        }
    }
}

void draw_rectangle_transparent(Canvas* canvas, int cx, int cy,
                                int width, int height, u32 colour) {
    if (cx < 0) {
        width += cx;
        if (width > 0) {
            cx = 0;
        }
    }

    int extra_width = (cx + width) - canvas->width;
    if (extra_width > 0) {
        width -= extra_width;
    }

    if (cy < 0) {
        height += cy;
        if (height > 0) {
            cy = 0;
        }
    }

    int extra_height = (cy + height) - canvas->height;
    if (extra_height > 0) {
        height -= extra_height;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            set_pixel_alpha(canvas, cx + x, cy + y, colour);
        }
    }
}

static int clip_test(int q, int p, double* te, double* tl) {
    if (p == 0) {
        return q < 0;
    }
    double t = static_cast<double>(q) / p;
    if (p > 0) {
        if (t > *tl) {
            return 0;
        }
        if (t > *te) {
            *te = t;
        }
    } else {
        if (t < *te) {
            return 0;
        }
        if (t < *tl) {
            *tl = t;
        }
    }
    return 1;
}

static int sign(int x) {
    return (x > 0) - (x < 0);
}

void draw_line(Canvas* canvas, int x1, int y1, int x2, int y2, u32 colour) {

    // Clip the line to the canvas rectangle.
    // Uses the Liang–Barsky line clipping algorithm.
    {
        // the rectangle's boundaries
        int x_min = 0;
        int x_max = canvas->width - 1;
        int y_min = 0;
        int y_max = canvas->height - 1;

        // for the line segment (x1, y1) to (x2, y2), derive the parametric form
        // of its line:
        // x = x1 + t * (x2 - x1)
        // y = y1 + t * (y2 - y1)

        int dx = x2 - x1;
        int dy = y2 - y1;

        double te = 0.0; // entering
        double tl = 1.0; // leaving
        if (clip_test(x_min - x1,  dx, &te, &tl) &&
            clip_test(x1 - x_max, -dx, &te, &tl) &&
            clip_test(y_min - y1,  dy, &te, &tl) &&
            clip_test(y1 - y_max, -dy, &te, &tl)) {
            if (tl < 1.0) {
                x2 = static_cast<int>(static_cast<double>(x1) + tl * dx);
                y2 = static_cast<int>(static_cast<double>(y1) + tl * dy);
            }
            if (te > 0.0) {
                x1 += te * dx;
                y1 += te * dy;
            }
        }
    }

    // Draw the clipped line segment to the canvas.
    {
        int adx = std::abs(x2 - x1);  // absolute value of delta x
        int ady = std::abs(y2 - y1);  // absolute value of delta y
        int sdx = sign(x2 - x1); // sign of delta x
        int sdy = sign(y2 - y1); // sign of delta y
        int x = adx / 2;
        int y = ady / 2;
        int px = x1;             // plot x
        int py = y1;             // plot y

        set_pixel(canvas, px, py, colour);

        if (adx >= ady) {
            for (int i = 0; i < adx; ++i) {
                y += ady;
                if (y >= adx) {
                    y -= adx;
                    py += sdy;
                }
                px += sdx;
                set_pixel(canvas, px, py, colour);
            }
        } else {
            for (int i = 0; i < ady; ++i) {
                x += adx;
                if (x >= ady) {
                    x -= ady;
                    px += sdx;
                }
                py += sdy;
                set_pixel(canvas, px, py, colour);
            }
        }
    }
}

static void hue_shift_matrix(double matrix[3][3], double h) {
    double u = std::cos(h);
    double w = std::sin(h);

    matrix[0][0] = u + (1.0 - u) / 3.0;
    matrix[0][1] = 1.0 / 3.0 * (1.0 - u) - std::sqrt(1.0 / 3.0) * w;
    matrix[0][2] = 1.0 / 3.0 * (1.0 - u) + std::sqrt(1.0 / 3.0) * w;

    matrix[1][0] = 1.0 / 3.0 * (1.0 - u) + std::sqrt(1.0 / 3.0) * w;
    matrix[1][1] = u + 1.0 / 3.0 * (1.0 - u);
    matrix[1][2] = 1.0 / 3.0 * (1.0 - u) - std::sqrt(1.0 / 3.0) * w;

    matrix[2][0] = 1.0 / 3.0 * (1.0 - u) - std::sqrt(1.0 / 3.0) * w;
    matrix[2][1] = 1.0 / 3.0 * (1.0 - u) + std::sqrt(1.0 / 3.0) * w;
    matrix[2][2] = u + 1.0 / 3.0 * (1.0 - u);
}

static u8 clamp8(double s) {
    if (s < 0) {
        return 0;
    } else if (s > 255) {
        return 255;
    } else {
        return s;
    }
}

static u32 transform_colour(u32 colour, double matrix[3][3]) {
    struct { double r, g, b; } in;
    in.r = GET_RED(colour);
    in.g = GET_GREEN(colour);
    in.b = GET_BLUE(colour);

    struct { u8 r, g, b; } out;
    out.r = clamp8(in.r * matrix[0][0] + in.g * matrix[0][1] + in.b * matrix[0][2]);
    out.g = clamp8(in.r * matrix[1][0] + in.g * matrix[1][1] + in.b * matrix[1][2]);
    out.b = clamp8(in.r * matrix[2][0] + in.g * matrix[2][1] + in.b * matrix[2][2]);

    return PACK_RGBA(out.r, out.g, out.b, GET_ALPHA(colour));
}

// Text-rendering functions....................................................

/*
Distinguishes characters which have no visible mark or glyph. This is as
opposed to "whitespace" characters, which includes the ogham space mark
conditionally displayed as a glyph depending on context.
*/
static bool is_character_non_displayable(char32_t codepoint) {
    switch (codepoint) {
        case 0x9: // character tabulation
        case 0xA: // line feed
        case 0xB: // line tabulation
        case 0xC: // form feed
        case 0xD: // carriage return
        case 0x20: // space
        case 0x85: // next line
        case 0xA0: // no-break space
        case 0x2000: // en quad
        case 0x2001: // em quad
        case 0x2002: // en space
        case 0x2003: // em space
        case 0x2004: // three-per-em space
        case 0x2005: // four-per-em space
        case 0x2006: // six-per-em space
        case 0x2007: // figure space
        case 0x2008: // punctuation space
        case 0x2009: // thin space
        case 0x200A: // hair space
        case 0x2028: // line separator
        case 0x2029: // paragraph separator
        case 0x202F: // narrow no-break space
        case 0x205F: // medium mathematical space
        case 0x3000: // ideographic space
            return true;
    }
    return false;
}

// @Incomplete: This doesn't fully handle Unicode line breaks. If this turns
// out to be insufficient, check the Lattice project for how to do more
// complete line-break handling.
static bool is_line_break(char32_t codepoint) {
    switch (codepoint) {
        case 0xA: // line feed
        case 0xC: // form feed
        case 0xD: // carriage return
        case 0x85: // next line
        case 0x2028: // line separator
        case 0x2029: // paragraph separator
            return true;
    }
    return false;
}

#define STACK_ALLOCATE_ARRAY(count, type) \
    static_cast<type*>(alloca(sizeof(type) * (count)))

void draw_text(Canvas* canvas, Atlas* atlas, BmFont* font, const char* text,
               int cx, int cy) {

    int char_count = string_size(text);
    int codepoint_count = utf8_codepoint_count(text);
    char32_t* codepoints = STACK_ALLOCATE_ARRAY(codepoint_count, char32_t);
    utf8_to_utf32(text, char_count, codepoints, codepoint_count);

    struct { int x, y; } pen;
    pen.x = cx;
    pen.y = cy;

    char32_t prior_char = 0x0;
    for (int i = 0; i < char_count; ++i) {
        char32_t c = text[i];
        BmFont::Glyph* glyph = bm_font_get_character_mapping(font, c);

        if (is_line_break(c)) {
            pen.x = cx;
            pen.y += font->leading;
        } else {
            pen.x += bm_font_get_kerning(font, prior_char, c);

            if (is_character_non_displayable(c)) {
                pen.x += glyph->x_advance;
            } else {
                int x = pen.x + glyph->x_offset;
                int y = pen.y + glyph->y_offset;
                int tx = glyph->texcoord.left;
                int ty = glyph->texcoord.top;
                int tw = glyph->texcoord.width;
                int th = glyph->texcoord.height;
                draw_subimage(canvas, atlas, x, y, tx, ty, tw, th);
                pen.x += font->tracking + glyph->x_advance;
            }
        }

        prior_char = c;
    }
}
