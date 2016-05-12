#include "ani_file.h"

#include "logging.h"
#include "string_utilities.h"
#include "memory.h"
#include "sized_types.h"

#include <cstdio>
#include <cstring>
#include <cassert>
#include <cinttypes>

struct File {
    void* data;
    s64 position;
    s64 end;
    bool end_of_file;
    bool reallocation_error;
};

static bool load_whole_binary_file(File* out_file, const char* filename) {
    std::FILE* file = std::fopen(filename, "rb");
    if (!file) {
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    long int total_bytes = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    char* data = ALLOCATE(char, total_bytes);
    if (!data) {
        std::fclose(file);
        return false;
    }

    std::size_t read_count = std::fread(data, total_bytes, 1, file);
    std::fclose(file);
    if (read_count != 1) {
        DEALLOCATE(data);
        return false;
    }

    out_file->data = data;
    out_file->end = total_bytes;
    return true;
}

static bool save_whole_file(File* save_file, const char* filename) {
    std::FILE* file = std::fopen(filename, "wb");
    if (!file) {
        return false;
    }

    std::size_t write_count = std::fwrite(save_file->data,
                                          save_file->position, 1, file);
    std::fclose(file);
    if (write_count != 1) {
        return false;
    }

    return true;
}

static void unload_file(File* file) {
    SAFE_DEALLOCATE(file->data);
}

static inline u8 extract8(File* file) {
    if (file->position >= file->end) {
        file->end_of_file = true;
        return 0;
    }
    u8* pointer = static_cast<u8*>(file->data);
    u8 value = pointer[file->position];
    file->position += 1;
    return value;
}

static u16 extract16(File* file) {
    u16 x;
    x = static_cast<u16>(extract8(file));
    x += static_cast<u16>(extract8(file)) << 8;
    return x;
}

static u32 extract32(File* file) {
    u32 x;
    x = static_cast<u32>(extract8(file));
    x += static_cast<u32>(extract8(file)) << 8;
    x += static_cast<u32>(extract8(file)) << 16;
    x += static_cast<u32>(extract8(file)) << 24;
    return x;
}

static u64 extract64(File* file) {
    u64 x;
    x = static_cast<u64>(extract8(file));
    x += static_cast<u64>(extract8(file)) << 8;
    x += static_cast<u64>(extract8(file)) << 16;
    x += static_cast<u64>(extract8(file)) << 24;
    x += static_cast<u64>(extract8(file)) << 32;
    x += static_cast<u64>(extract8(file)) << 40;
    x += static_cast<u64>(extract8(file)) << 48;
    x += static_cast<u64>(extract8(file)) << 56;
    return x;
}

static void seek_file(File* file, s64 offset) {
    assert(offset >= 0);
    if (offset > 0) {
        file->position += offset;
        if (file->position < 0) {
            file->position = 0;
        }
        if (file->position >= file->end) {
            file->end_of_file = true;
        }
    }
}

static inline void insert8(File* file, u8 value) {
    assert(file->position <= file->end);
    if (file->position >= file->end) {
        s64 end = file->end + 4096;
        u8* data = ALLOCATE(u8, end);
        if (!data) {
            file->reallocation_error = true;
            return;
        }
        file->end = end;
        if (file->data) {
            std::memcpy(data, file->data, file->end);
        }
        DEALLOCATE(file->data);
        file->data = data;
    }
    u8* pointer = static_cast<u8*>(file->data);
    pointer[file->position] = value;
    file->position += 1;
}

static void insert16(File* file, u16 x) {
    insert8(file, x & 0xFF);
    insert8(file, x >> 8 & 0xFF);
}

static void insert32(File* file, u32 x) {
    insert8(file, x & 0xFF);
    insert8(file, x >> 8 & 0xFF);
    insert8(file, x >> 16 & 0xFF);
    insert8(file, x >> 24 & 0xFF);
}

static void insert64(File* file, u64 x) {
    insert8(file, x & 0xFF);
    insert8(file, x >> 8 & 0xFF);
    insert8(file, x >> 16 & 0xFF);
    insert8(file, x >> 24 & 0xFF);
    insert8(file, x >> 32 & 0xFF);
    insert8(file, x >> 40 & 0xFF);
    insert8(file, x >> 48 & 0xFF);
    insert8(file, x >> 56 & 0xFF);
}

namespace ani {

/*
ANI File Specification

All integers are stored using little-endian byte order.
fields are listed by field_name : number_of_bytes

Header
signature : 8
version   : 2
pad       : 2

Chunk layout
size : 4
type : 4
data : size - 4

Sequence chunk
=====

Header
sequence_count : 2
frame_size     : 2

Sequence layout
frame_count : 2
frames      : frame_count * frame_size

Frame basic layout (version 1)
x        : 2
y        : 2
width    : 2
height   : 2
offset_x : 2
offset_y : 2
ticks    : 2
extra    : frame_size - 14

=====
*/

enum ChunkType {
    CHUNK_TYPE_SEQUENCE = 0x53514553, // SEQS in ASCII, byte-reversed
    CHUNK_TYPE_NAME     = 0x454D414E, // NAME
};

#define ANI_SIGNATURE 0x444E414DF0494E41ull // ANIðMAND in ASCII, byte-reversed

namespace {
    const u16 basic_frame_size = 7 * sizeof(u16);
}

bool load_asset(Asset* asset, const char* filename) {
    File file = {};
    bool loaded = load_whole_binary_file(&file, filename);
    if (!loaded) {
        LOG_ERROR("Failed to open file %s.", filename);
        return false;
    }

    u64 signature = extract64(&file);
    u16 version = extract16(&file);

    if (signature != ANI_SIGNATURE) {
        LOG_ERROR("The file signature was not the type expected; instead it "
                  "was 0x%" PRIx64 ".", signature);
        goto error;
    }
    assert(version == 1);
    if (version == 0) {
        LOG_ERROR("The file was version %i, which this program can't read.",
                  version);
        goto error;
    }

    while (!file.end_of_file) {
        u32 chunk_size = extract32(&file);
        u32 chunk_type = extract32(&file);
        u32 data_size = chunk_size - sizeof chunk_type;
        switch (chunk_type) {
            case CHUNK_TYPE_SEQUENCE: {
                asset->sequences_count = extract16(&file);
                asset->sequences = ALLOCATE(Sequence, asset->sequences_count);
                if (!asset->sequences) {
                    LOG_ERROR("Failed to allocate the memory needed for "
                              "storing the frame sequences.");
                    goto error;
                }
                u16 frame_size = extract16(&file);
                for (int i = 0; i < asset->sequences_count; ++i) {
                    Sequence* sequence = asset->sequences + i;
                    sequence->frames_count = extract16(&file);
                    sequence->frames = ALLOCATE(Frame, sequence->frames_count);
                    if (!sequence->frames) {
                        LOG_ERROR("Failed to allocate the memory needed for "
                                  "storing frame data.");
                        goto error;
                    }
                    for (int j = 0; j < sequence->frames_count; ++j) {
                        Frame* frame = sequence->frames + j;
                        frame->x = extract16(&file);
                        frame->y = extract16(&file);
                        frame->width = extract16(&file);
                        frame->height = extract16(&file);
                        frame->origin_x = static_cast<s16>(extract16(&file));
                        frame->origin_y = static_cast<s16>(extract16(&file));
                        frame->ticks = extract16(&file);
                        seek_file(&file, frame_size - basic_frame_size);
                    }
                }
                break;
            }
            case CHUNK_TYPE_NAME: {
                u32 buffer_size = data_size + asset->sequences_count
                                - sizeof(u16) * asset->sequences_count;
                char* buffer = ALLOCATE(char, buffer_size);
                if (!buffer) {
                    LOG_ERROR("Failed to allocate the memory needed to store "
                              "the sequence names.");
                    goto error;
                }
                char* pointer = buffer;
                for (int i = 0; i < asset->sequences_count; ++i) {
                    u16 name_size = extract16(&file);
                    asset->sequences[i].name = pointer;
                    for (u16 j = 0; j < name_size; ++j) {
                        pointer[j] = extract8(&file);
                    }
                    pointer += name_size;
                    *pointer = '\0';
                    pointer += 1;
                }
                assert(pointer - buffer <= buffer_size);
                break;
            }
            default: {
                // Skip all unrecognised and unneeded chunks.
                seek_file(&file, data_size);
                break;
            }
        }
    }

    unload_file(&file);
    return true;

error:
    unload_file(&file);
    unload_asset(asset);
    return false;
}

void unload_asset(Asset* asset) {
    for (int i = 0; i < asset->sequences_count; ++i) {
        SAFE_DEALLOCATE(asset->sequences[i].frames);
    }
    SAFE_DEALLOCATE(asset->sequences->name);
    SAFE_DEALLOCATE(asset->sequences);
}

bool save_asset(Asset* asset, const char* filename) {
    File file = {};
    insert64(&file, ANI_SIGNATURE);
    insert16(&file, 1); // version

    int total_frames = 0;
    for (int i = 0; i < asset->sequences_count; ++i) {
        total_frames += asset->sequences[i].frames_count;
    }
    u16 frame_size = basic_frame_size;
    u32 chunk_size = sizeof(u32)                          // chunk type
                   + 2 * sizeof(u16)                      // chunk header
                   + asset->sequences_count * sizeof(u16) // sequence headers
                   + total_frames * frame_size;           // frame data
    insert32(&file, chunk_size);
    insert32(&file, CHUNK_TYPE_SEQUENCE);
    insert16(&file, asset->sequences_count);
    insert16(&file, frame_size);

    for (int i = 0; i < asset->sequences_count; ++i) {
        Sequence* sequence = asset->sequences + i;
        insert16(&file, sequence->frames_count);
        for (int j = 0; j < sequence->frames_count; ++j) {
            Frame* frame = sequence->frames + j;
            insert16(&file, frame->x);
            insert16(&file, frame->y);
            insert16(&file, frame->width);
            insert16(&file, frame->height);
            insert16(&file, frame->origin_x);
            insert16(&file, frame->origin_y);
            insert16(&file, frame->ticks);
        }
    }

    u32 total_name_size = 0;
    for (int i = 0; i < asset->sequences_count; ++i) {
        total_name_size += string_size(asset->sequences[i].name);
    }
    chunk_size = sizeof(u32) + total_name_size
               + sizeof(u16) * asset->sequences_count;
    insert32(&file, chunk_size);
    insert32(&file, CHUNK_TYPE_NAME);
    for (int i = 0; i < asset->sequences_count; ++i) {
        char* name = asset->sequences[i].name;
        u16 name_size = string_size(name);
        insert16(&file, name_size);
        for (u16 j = 0; j < name_size; ++j) {
            insert8(&file, name[j]);
        }
    }

    if (file.reallocation_error) {
        unload_file(&file);
        return false;
    }

    bool saved = save_whole_file(&file, filename);
    unload_file(&file);

    return saved;
}

} // namespace ani
