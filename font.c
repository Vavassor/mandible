#include "font.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Text File Reader Functions................................................*/

typedef struct {
    char *current;
    bool read_error;
} Reader;

static void seek_in_line(Reader *reader, const char *target) {
    reader->current = strstr(reader->current, target);
}

static void seek_next_line(Reader *reader) {
    reader->current = strchr(reader->current, '\n');
}

static int get_integer(Reader *reader, const char *tag) {
    char *attribute = strstr(reader->current, tag);
    if (!attribute) {
        reader->read_error = true;
        return 0;
    }
    attribute += strlen(tag) + 1;
    return atoi(attribute);
}

static void seek_to_attribute(Reader *reader, const char *tag) {
    char *attribute = strstr(reader->current, tag);
    if (!attribute) {
        reader->read_error = true;
    } else {
        attribute += strlen(tag) + 1;
        reader->current = attribute;
    }
}

static size_t get_attribute_size(Reader *reader) {
    return strcspn(reader->current, " \n");
}

/* BmFont Functions..........................................................*/

bool bm_font_load(BmFont *font, const char *filename) {
    font->tracking = 0;

    /* Fetch the whole .fnt file and copy the contents into memory. */

    FILE *file = fopen(filename, "r");
    if (!file) {
        return false;
    }

    fseek(file, 0, SEEK_END);
    long int total_bytes = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *data = malloc(total_bytes + 1);
    if (!data) {
        fclose(file);
        return false;
    }
    data[total_bytes] = '\0';

    size_t read_count = fread(data, total_bytes, 1, file);
    fclose(file);
    if (read_count != 1) {
        free(data);
        return false;
    }

    Reader reader = {};
    reader.current = (char *) data;

    /* Info section */
    seek_in_line(&reader, "info");
    font->size = get_integer(&reader, "size");
    seek_next_line(&reader);

    /* Common section */
    seek_in_line(&reader, "common");
    font->baseline = get_integer(&reader, "base");
    font->leading = get_integer(&reader, "lineHeight");
    font->scale_horizontal = get_integer(&reader, "scaleW");
    font->scale_vertical = get_integer(&reader, "scaleH");
    int num_pages = get_integer(&reader, "pages");
    if (num_pages > 1) {
        free(data);
        return false;
    }
    seek_next_line(&reader);

    /* Page section */
    assert(num_pages == 1);
    seek_in_line(&reader, "page");
    seek_to_attribute(&reader, "file");
    size_t filename_size = get_attribute_size(&reader);
    if (filename_size <= 2) {
        free(data);
        return false;
    }
    filename_size -= 1;
    font->image.filename = malloc(filename_size);
    memcpy(font->image.filename, reader.current + 1, filename_size - 1);
    font->image.filename[filename_size - 1] = '\0';
    seek_next_line(&reader);

    /* Char section */
    seek_in_line(&reader, "chars");
    int num_chars = get_integer(&reader, "count");
    font->num_glyphs = num_chars;
    font->character_map = malloc(sizeof(char32_t) * num_chars);
    font->glyphs = malloc(sizeof(FontGlyph) * num_chars);
    seek_next_line(&reader);

    int i;
    for (i = 0; i < num_chars; ++i) {
        seek_in_line(&reader, "char");
        font->character_map[i] = get_integer(&reader, "id");
        FontGlyph *glyph = font->glyphs + i;
        glyph->texcoord.left = get_integer(&reader, "x");
        glyph->texcoord.top = get_integer(&reader, "y");
        glyph->texcoord.width = get_integer(&reader, "width");
        glyph->texcoord.height = get_integer(&reader, "height");
        glyph->x_advance = get_integer(&reader, "xadvance");
        glyph->x_offset = get_integer(&reader, "xoffset");
        glyph->y_offset = get_integer(&reader, "yoffset");
        seek_next_line(&reader);
    }

    /* Kerning section */
    seek_in_line(&reader, "kernings");
    int num_kerning_pairs = get_integer(&reader, "count");
    font->num_kerning_pairs = num_kerning_pairs;
    font->kerning_table = malloc(sizeof(FontKerningPair) * num_kerning_pairs);
    seek_next_line(&reader);

    for (i = 0; i < num_kerning_pairs; ++i) {
        seek_in_line(&reader, "kerning");
        FontKerningPair *pair = font->kerning_table + i;
        pair->first = get_integer(&reader, "first");
        pair->second = get_integer(&reader, "second");
        pair->amount = get_integer(&reader, "amount");
        seek_next_line(&reader);
    }

    /* Deallocate the file data and do one last check to see if there were
       errors before returning that the loading succeeded. */

    free(data);

    if (reader.read_error) {
        return false;
    }

    return true;
}

void bm_font_unload(BmFont *font) {
    free(font->kerning_table);
    free(font->glyphs);
    free(font->character_map);
    free(font->image.filename);
}

/* Font usage functions......................................................*/

FontGlyph *bm_font_get_character_mapping(BmFont *font, char32_t c) {
    int i;
    for (i = 0; i < font->num_glyphs; ++i) {
        if (font->character_map[i] == c) {
            return font->glyphs + i;
        }
    }
    return font->glyphs;
}

int bm_font_get_kerning(BmFont *font, char32_t first, char32_t second) {
    int i;
    for (i = 0; i < font->num_kerning_pairs; ++i) {
        if (font->kerning_table[i].first == first &&
            font->kerning_table[i].second == second) {
            return font->kerning_table[i].amount;
        }
    }
    return 0;
}
