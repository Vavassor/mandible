#pragma once

#include "sized_types.h"

bool load_whole_file(const char* path, void** data, s64* size);
bool save_whole_file(const char* path, const void* data, s64 size);
