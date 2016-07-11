#include "wor_file.h"

#include "memory.h"
#include "logging.h"
#include "string_utilities.h"
#include "byte_buffer.h"
#include "asset_handling.h"

static void insert_string(ByteBuffer* buffer, const char* string) {
    insert_bytes(buffer, reinterpret_cast<const u8*>(string), string_size(string));
}

namespace wor {

void save_chunk(const char* filename) {
    ByteBuffer buffer = {};
    const char* contents = "Entities";
    insert_string(&buffer, contents);
    save_whole_file(FileType::Asset_World_Chunk, filename, buffer.data, buffer.position);
    clear(&buffer);
}

void load_chunk(const char* filename) {
    void* contents;
    s64 size;
    load_whole_file(FileType::Asset_World_Chunk, filename, &contents, &size);
    LOG_DEBUG("%s contents: %.*s", filename, size, contents);
    DEALLOCATE(contents);
}

} // namespace wor
