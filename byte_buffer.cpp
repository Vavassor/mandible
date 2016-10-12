#include "byte_buffer.h"

#include "assert.h"

void clear(ByteBuffer* buffer) {
    stack_rewind(buffer->stack, buffer->handle);
    *buffer = {};
}

void insert8(ByteBuffer* buffer, u8 value) {
    ASSERT(buffer->position <= buffer->end);
    if (buffer->position >= buffer->end) {
        s64 end = 2 * buffer->end;
        if (end < 16) {
            end = 16;
        }
        u8* data = STACK_REALLOCATE(buffer->stack, u8, end, &buffer->handle);
        if (!data) {
            buffer->reallocation_error = true;
            return;
        }
        buffer->data = data;
    }
    u8* pointer = static_cast<u8*>(buffer->data);
    pointer[buffer->position] = value;
    buffer->position += 1;
    return;
}

void insert16(ByteBuffer* buffer, u16 x) {
    insert8(buffer, x);
    insert8(buffer, x >> 8);
}

void insert32(ByteBuffer* buffer, u32 x) {
    insert8(buffer, x);
    insert8(buffer, x >> 8);
    insert8(buffer, x >> 16);
    insert8(buffer, x >> 24);
}

void insert64(ByteBuffer* buffer, u64 x) {
    insert8(buffer, x);
    insert8(buffer, x >> 8);
    insert8(buffer, x >> 16);
    insert8(buffer, x >> 24);
    insert8(buffer, x >> 32);
    insert8(buffer, x >> 40);
    insert8(buffer, x >> 48);
    insert8(buffer, x >> 56);
}

void insert_bytes(ByteBuffer* buffer, const u8* bytes, int bytes_count) {
    for (int i = 0; i < bytes_count; ++i) {
        insert8(buffer, bytes[i]);
    }
}

u8 extract8(ByteBuffer* buffer) {
    if (buffer->position >= buffer->end) {
        buffer->end_of_file = true;
        return 0;
    }
    u8* pointer = static_cast<u8*>(buffer->data);
    u8 value = pointer[buffer->position];
    buffer->position += 1;
    return value;
}

u16 extract16(ByteBuffer* buffer) {
    return extract8(buffer)
         | static_cast<u16>(extract8(buffer)) << 8;
}

u32 extract32(ByteBuffer* buffer) {
    return extract8(buffer)
         | static_cast<u32>(extract8(buffer)) << 8
         | static_cast<u32>(extract8(buffer)) << 16
         | static_cast<u32>(extract8(buffer)) << 24;
}

u64 extract64(ByteBuffer* buffer) {
    return extract8(buffer)
         | static_cast<u64>(extract8(buffer)) << 8
         | static_cast<u64>(extract8(buffer)) << 16
         | static_cast<u64>(extract8(buffer)) << 24
         | static_cast<u64>(extract8(buffer)) << 32
         | static_cast<u64>(extract8(buffer)) << 40
         | static_cast<u64>(extract8(buffer)) << 48
         | static_cast<u64>(extract8(buffer)) << 56;
}
