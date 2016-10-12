#pragma once

#include "sized_types.h"
#include "memory.h"

struct ByteBuffer {
    Stack* stack;
    StackHandle handle;
    void* data;
    s64 position;
    s64 end;
    bool end_of_file;
    bool reallocation_error;
};

void clear(ByteBuffer* buffer);

void insert8(ByteBuffer* buffer, u8 value);
void insert16(ByteBuffer* buffer, u16 value);
void insert32(ByteBuffer* buffer, u32 value);
void insert64(ByteBuffer* buffer, u64 value);
void insert_bytes(ByteBuffer* buffer, const u8* bytes, int bytes_count);

u8 extract8(ByteBuffer* buffer);
u16 extract16(ByteBuffer* buffer);
u32 extract32(ByteBuffer* buffer);
u64 extract64(ByteBuffer* buffer);
