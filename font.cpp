#include "font.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cassert>

#define ALLOCATE(type, count) \
    static_cast<type *>(std::malloc(sizeof(type) * (count)))

#define DEALLOCATE(memory) \
    std::free(memory)

/* Text File Reader Functions................................................*/

struct Reader {
    char *current;
    bool read_error;
};

static void seek_in_line(Reader *reader, const char *target) {
    reader->current = std::strstr(reader->current, target);
}

static void seek_next_line(Reader *reader) {
    reader->current = std::strchr(reader->current, '\n');
}

static int get_integer(Reader *reader, const char *tag) {
    char *attribute = std::strstr(reader->current, tag);
    if (!attribute) {
        reader->read_error = true;
        return 0;
    }
    attribute += std::strlen(tag) + 1;
    return std::atoi(attribute);
}

static void seek_to_attribute(Reader *reader, const char *tag) {
    char *attribute = std::strstr(reader->current, tag);
    if (!attribute) {
        reader->read_error = true;
    } else {
        attribute += std::strlen(tag) + 1;
        reader->current = attribute;
    }
}

static std::size_t get_attribute_size(Reader *reader) {
    return std::strcspn(reader->current, " \n");
}

/* BmFont Functions..........................................................*/

bool bm_font_load(BmFont *font, const char *filename) {
    font->tracking = 0;

    /* Fetch the whole .fnt file and copy the contents into memory. */

    std::FILE *file = std::fopen(filename, "r");
    if (!file) {
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    long int total_bytes = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    char *data = ALLOCATE(char, total_bytes + 1);
    if (!data) {
        std::fclose(file);
        return false;
    }
    data[total_bytes] = '\0';

    std::size_t read_count = std::fread(data, total_bytes, 1, file);
    std::fclose(file);
    if (read_count != 1) {
        DEALLOCATE(data);
        return false;
    }

    Reader reader = {};
    reader.current = data;

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
        DEALLOCATE(data);
        return false;
    }
    seek_next_line(&reader);

    /* Page section */
    assert(num_pages == 1);
    seek_in_line(&reader, "page");
    seek_to_attribute(&reader, "file");
    std::size_t filename_size = get_attribute_size(&reader);
    if (filename_size <= 2) {
        DEALLOCATE(data);
        return false;
    }
    filename_size -= 1;
    font->image.filename = ALLOCATE(char, filename_size);
    std::memcpy(font->image.filename, reader.current + 1, filename_size - 1);
    font->image.filename[filename_size - 1] = '\0';
    seek_next_line(&reader);

    /* Char section */
    seek_in_line(&reader, "chars");
    int num_chars = get_integer(&reader, "count");
    font->num_glyphs = num_chars;
    font->character_map = ALLOCATE(char32_t, num_chars);
    font->glyphs = ALLOCATE(BmFont::Glyph, num_chars);
    seek_next_line(&reader);

    for (int i = 0; i < num_chars; ++i) {
        seek_in_line(&reader, "char");
        font->character_map[i] = get_integer(&reader, "id");
        BmFont::Glyph *glyph = font->glyphs + i;
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
    font->kerning_table = ALLOCATE(BmFont::KerningPair, num_kerning_pairs);
    seek_next_line(&reader);

    for (int i = 0; i < num_kerning_pairs; ++i) {
        seek_in_line(&reader, "kerning");
        BmFont::KerningPair *pair = font->kerning_table + i;
        pair->first = get_integer(&reader, "first");
        pair->second = get_integer(&reader, "second");
        pair->amount = get_integer(&reader, "amount");
        seek_next_line(&reader);
    }

    /* Deallocate the file data and do one last check to see if there were
       errors before returning that the loading succeeded. */

    DEALLOCATE(data);

    if (reader.read_error) {
        return false;
    }

    return true;
}

void bm_font_unload(BmFont *font) {
    DEALLOCATE(font->kerning_table);
    DEALLOCATE(font->glyphs);
    DEALLOCATE(font->character_map);
    DEALLOCATE(font->image.filename);
}

/* Font usage functions......................................................*/

BmFont::Glyph *bm_font_get_character_mapping(BmFont *font, char32_t c) {
    for (int i = 0; i < font->num_glyphs; ++i) {
        if (font->character_map[i] == c) {
            return font->glyphs + i;
        }
    }
    return font->glyphs;
}

int bm_font_get_kerning(BmFont *font, char32_t first, char32_t second) {
    for (int i = 0; i < font->num_kerning_pairs; ++i) {
        if (font->kerning_table[i].first == first &&
            font->kerning_table[i].second == second) {
            return font->kerning_table[i].amount;
        }
    }
    return 0;
}
