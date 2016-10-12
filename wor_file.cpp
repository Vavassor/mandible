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

void save_chunk(const char* filename, Stack* stack) {
    ByteBuffer buffer = {};
    buffer.stack = stack;
    const char* contents = "Entities";
    insert_string(&buffer, contents);
    save_whole_file(FileType::Asset_World_Chunk, filename, buffer.data, buffer.position, stack);
    clear(&buffer);
}

void load_chunk(const char* filename, Stack* stack) {
    StackHandle file_base = stack->top;
    void* contents;
    s64 size;
    bool loaded = load_file_to_stack(FileType::Asset_World_Chunk, filename, &contents, &size, stack);
    if (!loaded) {
        LOG_ERROR("Failed to load world chunk %s.", filename);
        return;
    }
    LOG_DEBUG("%s contents: %.*s", filename, size, contents);
    stack_rewind(stack, file_base);
}

} // namespace wor
