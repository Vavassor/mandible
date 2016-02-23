#ifndef FONT_H_
#define FONT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#if defined(_MSC_VER)
#include <stdint.h>
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#else
#include <uchar.h>
#endif

typedef struct {
    char* filename;
} FontImage;

typedef struct {
    struct Texcoord {
        int left, top, width, height;
    } texcoord;
    int x_offset, y_offset, x_advance;
} FontGlyph;

typedef struct {
    char32_t first, second;
    int amount;
} FontKerningPair;

/* Bitmap Font Generator .fnt file */
typedef struct {
    FontImage image;
    FontGlyph *glyphs;
    FontKerningPair *kerning_table;
    char32_t *character_map;
    int num_glyphs;
    int num_kerning_pairs;
    int size;
    int baseline, tracking, leading;
    int scale_horizontal, scale_vertical;
} BmFont;

bool bm_font_load(BmFont *font, const char *filename);
void bm_font_unload(BmFont *font);

FontGlyph *bm_font_get_character_mapping(BmFont *font, char32_t c);
int bm_font_get_kerning(BmFont *font, char32_t first, char32_t second);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FONT_H_ */
