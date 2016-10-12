#include "font.h"

#include "string_utilities.h"
#include "memory.h"
#include "asset_handling.h"
#include "assert.h"

// Hashing mapping functions...................................................

// a public domain 4-byte hash funciton by Bob Jenkins, adapted from a
// multiplicative method by Thomas Wang, to do it 6-shifts
// http://burtleburtle.net/bob/hash/integer.html
static inline uint32_t hash_bj6(uint32_t a) {
    a = (a + 0x7ED55d16) + (a << 12);
    a = (a ^ 0xC761C23C) ^ (a >> 19);
    a = (a + 0x165667B1) + (a << 5);
    a = (a + 0xD3A2646C) ^ (a << 9);
    a = (a + 0xFD7046C5) + (a << 3);
    a = (a ^ 0xB55A4F09) ^ (a >> 16);
    return a;
}

// Thomas Wang's 64-bit hash function
static inline uint64_t hash_wang(uint64_t k) {
    k = ~k + (k << 21); // k = (k << 21) - k - 1;
    k =  k ^ (k >> 24);
    k = (k + (k << 3)) + (k << 8); // k * 265
    k =  k ^ (k >> 14);
    k = (k + (k << 2)) + (k << 4); // k * 21
    k =  k ^ (k >> 28);
    k =  k + (k << 31);
    return k;
}

static inline int hash_codepoint(char32_t c, int n) {
    return hash_bj6(c) % n;
}

static inline void cycle_increment(int* s, int n) {
    *s = (*s + 1) % n;
}

// Character hash map functions................................................

// this is a noncharacter permanently reserved by the Unicode standard for
// internal use, here being defined to represent an empty spot in the hash map
static const char32_t invalid_codepoint = U'\U0000FFFF';

static void character_map_clear(char32_t* map, int map_count) {
    for (int i = 0; i < map_count; ++i) {
        map[i] = invalid_codepoint;
    }
}

static int character_map_insert(char32_t* map, int map_count, char32_t value) {
    // Hash to a spot in the map, and if it's not open linearly probe until an
    // open spot is found.
    int probe = hash_codepoint(value, map_count);
    while (map[probe] != invalid_codepoint) {
        cycle_increment(&probe, map_count);
    }
    map[probe] = value;
    return probe;
}

static int character_map_search(char32_t* map, int map_count, char32_t value) {
    // Hash to where the value should be, then linearly probe until either
    // the value is found or an empty spot is hit, which would mean the value's
    // not in the map.
    int probe = hash_codepoint(value, map_count);
    for (int i = 0; i < map_count; ++i) {
        if (map[probe] == invalid_codepoint) {
            break;
        }
        if (map[probe] == value) {
            return probe;
        }
        cycle_increment(&probe, map_count);
    }
    return -1;
}

// Kerning table hash map functions............................................

static int hash_pair(char32_t a, char32_t b, int n) {
    return hash_wang(static_cast<uint64_t>(a) << 32 |
                     static_cast<uint64_t>(b)) % n;
}

static void kerning_table_clear(BmFont::KerningPair* table, int table_count) {
    for (int i = 0; i < table_count; ++i) {
        table[i].first = invalid_codepoint;
    }
}

static int kerning_table_insert(BmFont::KerningPair* table, int table_count,
                                char32_t a, char32_t b, int amount) {
    int probe = hash_pair(a, b, table_count);
    while (table[probe].first != invalid_codepoint) {
        cycle_increment(&probe, table_count);
    }
    table[probe].first = a;
    table[probe].second = b;
    table[probe].amount = amount;
    return probe;
}

static int kerning_table_search(BmFont::KerningPair* table, int table_count,
                                char32_t a, char32_t b) {
    int probe = hash_pair(a, b, table_count);
    for (int i = 0; i < table_count; ++i) {
        if (table[probe].first == invalid_codepoint) {
            break;
        }
        if (table[probe].first == a && table[probe].second == b) {
            return probe;
        }
        cycle_increment(&probe, table_count);
    }
    return -1;
}

// General String Search Functions.............................................

static char* find_char(const char* s, char c) {
    ASSERT(s);
    while (*s != c) {
        if (*s++ == '\0') {
            return nullptr;
        }
    }
    return const_cast<char*>(s);
}

static int count_span_without_chars(const char* s, const char* set) {
    ASSERT(s);
    ASSERT(set);
    int count = 0;
    while (*s) {
        if (find_char(set, *s)) {
            return count;
        } else {
            s++;
            count++;
        }
    }
    return count;
}

// Text File Reader Functions..................................................

struct Reader {
    char* current;
    bool read_error;
};

static void seek_in_line(Reader* reader, const char* target) {
    reader->current = find_string(reader->current, target);
}

static void seek_next_line(Reader* reader) {
    reader->current = find_char(reader->current, '\n');
}

static int get_integer(Reader* reader, const char* tag) {
    char* attribute = find_string(reader->current, tag);
    if (!attribute) {
        reader->read_error = true;
        return 0;
    }
    attribute += string_size(tag) + 1;
    return string_to_int(attribute);
}

static void seek_to_attribute(Reader* reader, const char* tag) {
    char* attribute = find_string(reader->current, tag);
    if (!attribute) {
        reader->read_error = true;
    } else {
        attribute += string_size(tag) + 1;
        reader->current = attribute;
    }
}

static int get_attribute_size(Reader* reader) {
    return count_span_without_chars(reader->current, " \n");
}

// BmFont Functions............................................................

bool bm_font_load(BmFont* font, const char* filename, Heap* heap,
                  Stack* stack) {
    font->tracking = 0;

    StackHandle font_base = stack->top;

    // Fetch the whole .fnt file and copy the contents into memory.

    void* data;
    s64 total_bytes;
    bool loaded = load_file_to_stack(FileType::Asset_Font, filename,
                                     &data, &total_bytes, stack);
    if (!loaded) {
        return false;
    }

    Reader reader = {};
    reader.current = static_cast<char*>(data);

    // Info section
    seek_in_line(&reader, "info");
    font->size = get_integer(&reader, "size");
    seek_next_line(&reader);

    // Common section
    seek_in_line(&reader, "common");
    font->baseline = get_integer(&reader, "base");
    font->leading = get_integer(&reader, "lineHeight");
    font->scale_horizontal = get_integer(&reader, "scaleW");
    font->scale_vertical = get_integer(&reader, "scaleH");
    int num_pages = get_integer(&reader, "pages");
    if (num_pages > 1) {
        stack_rewind(stack, font_base);
        return false;
    }
    seek_next_line(&reader);

    // Page section
    ASSERT(num_pages == 1);
    seek_in_line(&reader, "page");
    seek_to_attribute(&reader, "file");
    int filename_size = get_attribute_size(&reader);
    if (filename_size <= 2) {
        stack_rewind(stack, font_base);
        return false;
    }
    filename_size -= 1;
    font->image.filename = ALLOCATE(heap, char, filename_size);
    copy_string(font->image.filename, filename_size, reader.current + 1);
    seek_next_line(&reader);

    // Char section
    seek_in_line(&reader, "chars");
    int num_chars = get_integer(&reader, "count");
    font->num_glyphs = num_chars;
    font->character_map = ALLOCATE(heap, char32_t, num_chars);
    character_map_clear(font->character_map, num_chars);
    font->glyphs = ALLOCATE(heap, BmFont::Glyph, num_chars);
    seek_next_line(&reader);

    for (int i = 0; i < num_chars; ++i) {
        seek_in_line(&reader, "char");
        char32_t codepoint = get_integer(&reader, "id");
        int index = character_map_insert(font->character_map,
                                         font->num_glyphs, codepoint);
        BmFont::Glyph* glyph = font->glyphs + index;
        glyph->texcoord.left = get_integer(&reader, "x");
        glyph->texcoord.top = get_integer(&reader, "y");
        glyph->texcoord.width = get_integer(&reader, "width");
        glyph->texcoord.height = get_integer(&reader, "height");
        glyph->x_advance = get_integer(&reader, "xadvance");
        glyph->x_offset = get_integer(&reader, "xoffset");
        glyph->y_offset = get_integer(&reader, "yoffset");
        seek_next_line(&reader);
    }

    // Kerning section
    seek_in_line(&reader, "kernings");
    int num_kerning_pairs = get_integer(&reader, "count");
    font->num_kerning_pairs = num_kerning_pairs;
    font->kerning_table = ALLOCATE(heap, BmFont::KerningPair, num_kerning_pairs);
    kerning_table_clear(font->kerning_table, num_kerning_pairs);
    seek_next_line(&reader);

    for (int i = 0; i < num_kerning_pairs; ++i) {
        seek_in_line(&reader, "kerning");
        char32_t first = get_integer(&reader, "first");
        char32_t second = get_integer(&reader, "second");
        int amount = get_integer(&reader, "amount");
        int index = kerning_table_insert(font->kerning_table, num_kerning_pairs,
                                         first, second, amount);
        BmFont::KerningPair* pair = font->kerning_table + index;
        pair->first = first;
        pair->second = second;
        pair->amount = amount;
        seek_next_line(&reader);
    }

    // Deallocate the file data and do one last check to see if there were
    // errors before returning that the loading succeeded.

    stack_rewind(stack, font_base);

    if (reader.read_error) {
        return false;
    }

    return true;
}

void bm_font_unload(BmFont* font, Heap* heap) {
    DEALLOCATE(heap, font->kerning_table);
    DEALLOCATE(heap, font->glyphs);
    DEALLOCATE(heap, font->character_map);
    DEALLOCATE(heap, font->image.filename);
}

// Font usage functions........................................................

BmFont::Glyph* bm_font_get_character_mapping(BmFont* font, char32_t c) {
    int index = character_map_search(font->character_map, font->num_glyphs, c);
    if (index >= 0) {
        return font->glyphs + index;
    }
    return font->glyphs;
}

int bm_font_get_kerning(BmFont* font, char32_t first, char32_t second) {
    int index = kerning_table_search(font->kerning_table,
                                     font->num_kerning_pairs, first, second);
    if (index >= 0) {
        return font->kerning_table[index].amount;
    }
    return 0;
}
