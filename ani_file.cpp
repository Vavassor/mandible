#include "ani_file.h"

#include "logging.h"
#include "string_utilities.h"
#include "memory.h"
#include "asset_handling.h"
#include "byte_buffer.h"
#include "assert.h"

#include <cinttypes>

static void seek_buffer(ByteBuffer* buffer, s64 offset) {
    ASSERT(offset >= 0);
    if (offset > 0) {
        buffer->position += offset;
        if (buffer->position < 0) {
            buffer->position = 0;
        }
        if (buffer->position >= buffer->end) {
            buffer->end_of_file = true;
        }
    }
}

namespace ani {

/*
.ani Animation File Specification

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
    ByteBuffer buffer = {};
    bool loaded = load_whole_file(FileType::Asset_Animation, filename, &buffer.data, &buffer.end);
    if (!loaded) {
        LOG_ERROR("Failed to open file %s.", filename);
        return false;
    }

    u64 signature = extract64(&buffer);
    u16 version = extract16(&buffer);

    if (signature != ANI_SIGNATURE) {
        LOG_ERROR("The file signature was not the type expected; instead it "
                  "was 0x%" PRIx64 ".", signature);
        goto error;
    }
    ASSERT(version == 1);
    if (version == 0) {
        LOG_ERROR("The file was version %i, which this program can't read.",
                  version);
        goto error;
    }

    while (!buffer.end_of_file) {
        u32 chunk_size = extract32(&buffer);
        u32 chunk_type = extract32(&buffer);
        u32 data_size = chunk_size - sizeof chunk_type;
        switch (chunk_type) {
            case CHUNK_TYPE_SEQUENCE: {
                asset->sequences_count = extract16(&buffer);
                asset->sequences = ALLOCATE(Sequence, asset->sequences_count);
                if (!asset->sequences) {
                    LOG_ERROR("Failed to allocate the memory needed for "
                              "storing the frame sequences.");
                    goto error;
                }
                u16 frame_size = extract16(&buffer);
                for (int i = 0; i < asset->sequences_count; ++i) {
                    Sequence* sequence = asset->sequences + i;
                    sequence->frames_count = extract16(&buffer);
                    sequence->frames = ALLOCATE(Frame, sequence->frames_count);
                    if (!sequence->frames) {
                        LOG_ERROR("Failed to allocate the memory needed for "
                                  "storing frame data.");
                        goto error;
                    }
                    for (int j = 0; j < sequence->frames_count; ++j) {
                        Frame* frame = sequence->frames + j;
                        frame->x = extract16(&buffer);
                        frame->y = extract16(&buffer);
                        frame->width = extract16(&buffer);
                        frame->height = extract16(&buffer);
                        frame->origin_x = static_cast<s16>(extract16(&buffer));
                        frame->origin_y = static_cast<s16>(extract16(&buffer));
                        frame->ticks = extract16(&buffer);
                        seek_buffer(&buffer, frame_size - basic_frame_size);
                    }
                }
                break;
            }
            case CHUNK_TYPE_NAME: {
                u32 names_size = data_size + asset->sequences_count
                               - sizeof(u16) * asset->sequences_count;
                char* names = ALLOCATE(char, names_size);
                if (!names) {
                    LOG_ERROR("Failed to allocate the memory needed to store "
                              "the sequence names.");
                    goto error;
                }
                char* pointer = names;
                for (int i = 0; i < asset->sequences_count; ++i) {
                    u16 name_size = extract16(&buffer);
                    asset->sequences[i].name = pointer;
                    for (u16 j = 0; j < name_size; ++j) {
                        pointer[j] = extract8(&buffer);
                    }
                    pointer += name_size;
                    *pointer = '\0';
                    pointer += 1;
                }
                ASSERT(pointer - names <= names_size);
                break;
            }
            default: {
                // Skip all unrecognised and unneeded chunks.
                seek_buffer(&buffer, data_size);
                break;
            }
        }
    }

    clear(&buffer);
    return true;

error:
    clear(&buffer);
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
    ByteBuffer buffer = {};

    // Header

    insert64(&buffer, ANI_SIGNATURE);
    insert16(&buffer, 1); // version

    // Sequence Chunk

    int total_frames = 0;
    for (int i = 0; i < asset->sequences_count; ++i) {
        total_frames += asset->sequences[i].frames_count;
    }
    u16 frame_size = basic_frame_size;
    u32 chunk_size = sizeof(u32)                          // chunk type
                   + 2 * sizeof(u16)                      // chunk header
                   + asset->sequences_count * sizeof(u16) // sequence headers
                   + total_frames * frame_size;           // frame data
    insert32(&buffer, chunk_size);
    insert32(&buffer, CHUNK_TYPE_SEQUENCE);
    insert16(&buffer, asset->sequences_count);
    insert16(&buffer, frame_size);

    for (int i = 0; i < asset->sequences_count; ++i) {
        Sequence* sequence = asset->sequences + i;
        insert16(&buffer, sequence->frames_count);
        for (int j = 0; j < sequence->frames_count; ++j) {
            Frame* frame = sequence->frames + j;
            insert16(&buffer, frame->x);
            insert16(&buffer, frame->y);
            insert16(&buffer, frame->width);
            insert16(&buffer, frame->height);
            insert16(&buffer, frame->origin_x);
            insert16(&buffer, frame->origin_y);
            insert16(&buffer, frame->ticks);
        }
    }

    // Name Chunk

    u32 total_name_size = 0;
    for (int i = 0; i < asset->sequences_count; ++i) {
        total_name_size += string_size(asset->sequences[i].name);
    }
    chunk_size = sizeof(u32) + total_name_size
               + sizeof(u16) * asset->sequences_count;
    insert32(&buffer, chunk_size);
    insert32(&buffer, CHUNK_TYPE_NAME);
    for (int i = 0; i < asset->sequences_count; ++i) {
        char* name = asset->sequences[i].name;
        u16 name_size = string_size(name);
        insert16(&buffer, name_size);
        for (u16 j = 0; j < name_size; ++j) {
            insert8(&buffer, name[j]);
        }
    }

    if (buffer.reallocation_error) {
        clear(&buffer);
        return false;
    }

    bool saved = save_whole_file(FileType::Asset_Animation, filename, buffer.data, buffer.position);
    clear(&buffer);

    return saved;
}

} // namespace ani
