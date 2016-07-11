#pragma once

#include "sized_types.h"

enum class FileType {
    Asset_Animation,
    Asset_Audio,
    Asset_Font,
    Asset_Icon,
    Asset_Image,
    Asset_Shader,
    Asset_World_Chunk,
    Config,
    Saved_Game,
};

bool load_whole_file(FileType type, const char* relative_path, void** data, s64* size);
bool save_whole_file(FileType type, const char* relative_path, const void* data, s64 size);
void delete_config_file_if_too_large(const char* relative_path, int limit);

enum class FileMode { Read, Write };

struct File;
File* open_file(FileType type, FileMode mode, const char* relative_path);
void close_file(File* file);
bool write_file(File* file, const void* data, s64 size);
s64 read_file(File* file, void* data, s64 size);
s64 seek_file(File* file, s64 offset);
s64 seek_file_forward(File* file, s64 offset);
s64 get_file_size(File* file);
s64 get_file_offset(File* file);

void print(const char* string, bool is_error);
