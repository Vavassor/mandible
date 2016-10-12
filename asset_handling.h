#pragma once

#include "sized_types.h"

struct Stack;
struct Heap;

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

bool load_whole_file(FileType type, const char* relative_path, void** data, s64* size, Heap* heap, Stack* stack);
bool load_file_to_stack(FileType type, const char* relative_path, void** data, s64* size, Stack* stack);
bool save_whole_file(FileType type, const char* relative_path, const void* data, s64 size, Stack* stack);
void delete_config_file_if_too_large(const char* relative_path, int limit, Stack* stack);

enum class FileMode { Read, Write };

#if defined(_WIN32)
typedef void* FileHandle; // windows HANDLE type
#else
typedef int FileHandle; // posix file handle
#endif

static const int file_path_max = 256;

struct File {
    char path[file_path_max];
    FileHandle handle;
    FileMode mode;
    bool open;
};

bool open_file(File* file, FileType type, FileMode mode, const char* relative_path, Stack* stack);
void close_file(File* file);
bool write_file(File* file, const void* data, s64 size);
s64 read_file(File* file, void* data, s64 size);
s64 seek_file(File* file, s64 offset);
s64 seek_file_forward(File* file, s64 offset);
s64 seek_file_from_end(File* file, s64 offset);
s64 get_file_size(File* file);
s64 get_file_offset(File* file);

void print(const char* string, bool is_error);
void report_error_in_a_popup(const char* message, bool include_log_reminder = true);
