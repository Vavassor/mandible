#pragma once

// Bitmap Font Generator .fnt file
struct BmFont {
    struct Image {
        char* filename;
    };

    struct Glyph {
        struct Texcoord {
            int left, top, width, height;
        } texcoord;
        int x_offset, y_offset, x_advance;
    };

    struct KerningPair {
        char32_t first, second;
        int amount;
    };

    Image image;
    Glyph* glyphs;
    KerningPair* kerning_table;
    char32_t* character_map;
    int num_glyphs;
    int num_kerning_pairs;
    int size;
    int baseline, tracking, leading;
    int scale_horizontal, scale_vertical;
};

struct Stack;
struct Heap;

bool bm_font_load(BmFont* font, const char* filename, Heap* heap, Stack* stack);
void bm_font_unload(BmFont* font, Heap* heap);

BmFont::Glyph* bm_font_get_character_mapping(BmFont* font, char32_t c);
int bm_font_get_kerning(BmFont* font, char32_t first, char32_t second);
