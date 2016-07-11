#include "asset_handling.h"

#include "logging.h"
#include "memory.h"
#include "string_utilities.h"
#include "assert.h"

#if defined(__linux__)
#define PLATFORM_LINUX
#elif defined(_WIN32)
#define PLATFORM_WINDOWS
#endif

#if defined(PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#elif defined(PLATFORM_LINUX)
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <pwd.h>
#include <cstring>

#endif // defined(PLATFORM_LINUX)

// "Portable" here refers to a portable application, one that is not installed
// and keeps all of its files in one directory. Not portable as in
// cross-platform or having to do with porting.
#define PORTABLE_BUILD
// During development it's easiest to use a portable build so everything is
// in one directory and can be put in source-control together. For deployment,
// removing PORTABLE_BUILD allows an installed build which uses the standard
// directories for application data as per the XDG Base Directory Specification
// on Linux and on Windows, the application data and program files directories.

#define APPLICATION_FOLDER "/mandible"

enum class BaseType { Assets, Config, Saves };

#if defined(PORTABLE_BUILD)

static char* get_executable_folder(int extra, int* buffer_size);

static char* get_base_and_extension(BaseType type, int extra, int* size) {
    const char* folder;
    switch (type) {
        case BaseType::Assets: folder = "/Assets"; break;
        case BaseType::Config: folder = "/Config"; break;
        case BaseType::Saves:  folder = "/Saves";  break;
    }
    extra += string_size(folder);
    char* buffer = get_executable_folder(extra, size);
    if (buffer) {
        append_string(buffer, *size, folder);
    }
    return buffer;
}

#else // !defined(PORTABLE_BUILD)

static char* get_base_folder(BaseType type, int extra, int* size);

static char* get_base_and_extension(BaseType type, int extra, int* size) {
    const char* folder = APPLICATION_FOLDER;
    extra += string_size(folder);
    char* buffer = get_base_folder(type, extra, size);
    if (buffer) {
        append_string(buffer, *size, folder);
    }
    return buffer;
}

#endif // !defined(PORTABLE_BUILD)

static char* resolve_asset_path(const char* section, const char* path) {
    int extra = 1 + string_size(path) + 1;
    if (section) {
        extra += string_size(section) + 1;
    }
    char* buffer;
    int size;
    buffer = get_base_and_extension(BaseType::Assets, extra, &size);
    if (!buffer) {
        return nullptr;
    }
    append_string(buffer, size, "/");
    if (section) {
        append_string(buffer, size, section);
        append_string(buffer, size, "/");
    }
    append_string(buffer, size, path);
    return buffer;
}

static char* resolve_config_path(const char* path) {
    int extra = 1 + string_size(path) + 1;
    char* buffer;
    int size;
    buffer = buffer = get_base_and_extension(BaseType::Config, extra, &size);
    if (!buffer) {
        return nullptr;
    }
    append_string(buffer, size, "/");
    append_string(buffer, size, path);
    return buffer;
}

static char* resolve_saved_game_path(const char* path) {
    int extra = 1 + string_size(path) + 1;
    char* buffer;
    int size;
    buffer = buffer = get_base_and_extension(BaseType::Saves, extra, &size);
    if (!buffer) {
        return nullptr;
    }
    append_string(buffer, size, "/");
    append_string(buffer, size, path);
    return buffer;
}

#if defined(PLATFORM_WINDOWS)

#if defined(PORTABLE_BUILD)

static char* get_executable_folder(int extra, int* size) {
    // @Incomplete: this should use GetModuleFileNameW and convert from wide
    // characters to UTF-8 to preserve Unicode characters in the path.
    int buffer_size = 256;
    char* buffer = ALLOCATE(char, buffer_size + extra);
    if (!buffer) {
        return nullptr;
    }
    for (;;) {
        DWORD bytes = GetModuleFileNameA(nullptr, buffer, buffer_size);
        if (bytes == 0) {
            return nullptr;
        }
        if (bytes < buffer_size) {
            break;
        }
        buffer_size *= 2;
        buffer = static_cast<char*>(heap_reallocate(buffer, buffer_size + extra));
        if (!buffer) {
            return nullptr;
        }
    }
    char* backslash = find_last_char(buffer, '\\');
    *backslash = '\0';
    *size = buffer_size + extra;
    return buffer;
}

#else // !defined(PORTABLE_BUILD)

static char* get_base_folder(BaseType type, int extra, int* size) {
    char* buffer;
    int buffer_size;
    const char* base;
    switch (type) {
        case BaseType::Assets: { base = getenv("ProgramFiles"); break; }
        case BaseType::Config: { base = getenv("LOCALAPPDATA"); break; }
        case BaseType::Saves:  { base = getenv("APPDATA");      break; }
    }
    if (!(base && *base)) {
        return nullptr;
    }
    buffer_size = string_size(base) + extra;
    buffer = ALLOCATE(char, buffer_size);
    if (!buffer) {
        return nullptr;
    }
    copy_string(buffer, buffer_size, base);
    *size = buffer_size;
    return buffer;
}

#endif // !defined(PORTABLE_BUILD)

static bool is_letter(char c) {
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z');
}

static bool is_slash(char c) {
    return c == '\\' || c == '/';
}

static bool is_absolute_path(const char* path) {
    int bytes = string_size(path);
    if (bytes < 2) {
        // a 1 character path is a relative filename
        return false;
    }
    return (is_slash(path[0]) && is_slash(path[1])) // a UNC name
        || (bytes >= 3 && is_letter(path[0]) && path[1] == ':' && is_slash(path[2])) // starts with a disk designator
        || (is_slash(path[0]) && !is_slash(path[1])); // an explicit absolute path
}

static void convert_to_all_backslashes(wchar_t* path) {
    while (*path) {
        if (*path == L'/') {
            *path = L'\\';
        }
        path += 1;
    }
}

static int wide_string_count(const wchar_t* s) {
    const wchar_t* start = s;
    while (*s++);
    return s - start - 1;
}

static void log_thread_error(DWORD error = 0) {
    LPVOID* system_buffer = nullptr;

    if (error == 0) {
        error = GetLastError();
    }

    // Fetch the error description formatted as a UTF-16 string to preserve
    // any non-ASCII characters in the text.

    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
                | FORMAT_MESSAGE_FROM_SYSTEM
                | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD char_count = FormatMessageW(flags, nullptr, error, 0, reinterpret_cast<LPWSTR>(&system_buffer), 0, nullptr);
    if (char_count == 0) {
        LOG_ERROR("code: 0x%x", error);
        return;
    }

    // Since the log assumes UTF-8, the returned message must be converted.

    // Determine the number of bytes needed to hold the UTF-8 string.

    LPWSTR wide_string = reinterpret_cast<LPWSTR>(system_buffer);
    int byte_count = WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, nullptr, 0, nullptr, nullptr);
    if (byte_count == 0) {
        LOG_ERROR("code: 0x%x", error);
        return;
    }

    // Allocate a buffer for the UTF-8 string and do the conversion.

    LPVOID log_buffer = STACK_ALLOCATE(char, byte_count);
    ASSERT(log_buffer); // stack allocation should never be null
    LPSTR utf8_string = reinterpret_cast<LPSTR>(log_buffer);
    DWORD utf8_count = WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, utf8_string, byte_count, nullptr, nullptr);
    if (utf8_count == 0) {
        LOG_ERROR("code: 0x%x", error);
        return;
    }

    LocalFree(system_buffer);

    LOG_ERROR("%s", utf8_string);
}

enum class OpenMode { Write, Read };

static HANDLE open_file(const char* path, OpenMode open_mode) {
    // Convert the UTF-8 file path to UTF-16.

    // Determine how many bytes the path will take in UTF-16.

    int wide_count = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wide_count == 0) {
        LOG_ERROR("The file path %s could not be converted to wide characters.", path);
        log_thread_error();
        return INVALID_HANDLE_VALUE;
    }
    ASSERT(wide_count > 0);

    // Add the appropriate path prefixes for extended-length paths if needed.

    const wchar_t* prefix = nullptr;
    int prefix_chars = 0;
    if (wide_count > MAX_PATH - 1 && is_absolute_path(path)) {
        // An extended-length path requires a "\\?\" prefix.
        if (is_slash(path[0]) && is_slash(path[1])) {
            // A UNC name as an extended-length path requires an additional
            // "UNC\."
            prefix = L"\\\\?\\UNC\\";
        } else {
            prefix = L"\\\\?\\";
        }
        prefix_chars = wide_string_count(prefix);
    }

    // Allocate an appropriately sized buffer to store the UTF-16.

    wchar_t* wide_path;
    bool wide_path_on_heap;
    if (prefix) {
        wide_path = ALLOCATE(wchar_t, wide_count + prefix_chars);
        wide_path_on_heap = true;
    } else {
        wide_path = STACK_ALLOCATE(wchar_t, wide_count);
        ASSERT(wide_path); // stack allocation should never be null
        wide_path_on_heap = false;
    }

    // Do the actual UTF-8 to UTF-16 conversion.

    int chars_converted = MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path + prefix_chars, wide_count);
    if (chars_converted != wide_count) {
        LOG_ERROR("The file path %s could not be converted to wide characters", path);
        log_thread_error();
        if (wide_path_on_heap) {
            DEALLOCATE(wide_path);
        }
        return INVALID_HANDLE_VALUE;
    }
    if (prefix) {
        CopyMemory(wide_path, prefix, sizeof(wchar_t) * prefix_chars);

        // Extended-length paths do not undergo the normalisation used for
        // Nt-style paths, so slashes have to be converted here manually.
        convert_to_all_backslashes(wide_path);
    }

    // Determine necessary opening parameters.

    DWORD access;
    DWORD share_mode;
    DWORD creation_disposition;
    switch (open_mode) {
        case OpenMode::Read: {
            access = GENERIC_READ;
            share_mode = FILE_SHARE_READ;
            creation_disposition = OPEN_EXISTING;
            break;
        }
        case OpenMode::Write: {
            access = GENERIC_WRITE;
            share_mode = 0;
            creation_disposition = OPEN_ALWAYS;
            break;
        }
    }

    // Open the file and return its handle.

    HANDLE file = CreateFileW(wide_path, access, share_mode, nullptr, creation_disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (wide_path_on_heap) {
        DEALLOCATE(wide_path);
    }
    if (file == INVALID_HANDLE_VALUE) {
        LOG_ERROR("The file %s could not be opened.", path);
        log_thread_error();
    }
    return file;
}

static bool close_file(HANDLE file, const char* path) {
    BOOL closed = CloseHandle(file);
    if (closed == FALSE) {
        LOG_ERROR("The file %s could not be closed.", path);
        log_thread_error();
        return false;
    }
    return true;
}

bool load_file(const char* path, void** data, s64* size) {
    ASSERT(path);
    ASSERT(data);
    ASSERT(size);
    HANDLE file = open_file(path, OpenMode::Read);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info = {};
    BOOL got_info = GetFileInformationByHandle(file, &info);
    if (got_info == FALSE) {
        LOG_ERROR("The size of the file %s could not be obtained.", path);
        log_thread_error();
        goto failure_before_read;
    }
    s64 byte_count = static_cast<s64>(info.nFileSizeHigh) << 32
                   | static_cast<s64>(info.nFileSizeLow);
    void* memory = ALLOCATE(uint8_t, byte_count);
    if (!memory) {
        LOG_ERROR("The memory needed to store the file %s could not be allocated.", path);
        goto failure_before_read;
    }
    DWORD bytes_read;
    BOOL read = ReadFile(file, memory, byte_count, &bytes_read, nullptr);
    DWORD read_error = GetLastError();
    bool closed = close_file(file, path);
    if (!closed) {
        goto failure_after_close;
    }
    if (read == FALSE) {
        LOG_ERROR("The file %s could not be read.", path);
        log_thread_error(read_error);
        goto failure_after_close;
    } else if (bytes_read != byte_count) {
        LOG_ERROR("The file %s was only partially read.", path);
        goto failure_after_close;
    }

    *data = memory;
    *size = byte_count;
    return true;

failure_before_read:
    close_file(file, path);
    return false;

failure_after_close:
    DEALLOCATE(memory);
    return false;
}

bool save_file(const char* path, const void* data, s64 size) {
    ASSERT(path);
    ASSERT(data);
    HANDLE file = open_file(path, OpenMode::Write);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD bytes_written;
    BOOL writ = WriteFile(file, data, size, &bytes_written, nullptr);
    DWORD write_error = GetLastError();
    close_file(file, path);
    if (writ == FALSE) {
        LOG_ERROR("The file %s could not be written to.", path);
        log_thread_error(write_error);
        return false;
    } else if (bytes_written != size) {
        LOG_ERROR("The file %s was not completely written.", path);
        return false;
    }
    ASSERT(bytes_written > 0 && bytes_written <= size);
    return true;
}

void print(const char* string, bool is_error) {
    static_cast<void>(is_error);
    OutputDebugStringA(string);
}

#elif defined(PLATFORM_LINUX)

#if defined(PORTABLE_BUILD)

static char* find_last_char(const char* s, char c) {
    char* result = nullptr;
    do {
        if (*s == c) {
            result = const_cast<char*>(s);
        }
    } while(*s++);
    return result;
}

static char* get_executable_folder(int extra, int* size) {
    ASSERT(size);
    int buffer_size = 1024;
    char* buffer = ALLOCATE(char, buffer_size + extra);
    if (!buffer) {
        return nullptr;
    }
    ssize_t bytes;
    for (;;) {
        bytes = readlink("/proc/self/exe", buffer, buffer_size - 1);
        if (bytes < 0) {
            DEALLOCATE(buffer);
            return nullptr;
        }
        if (bytes < buffer_size - 1) {
            break;
        }
        buffer_size *= 2;
        buffer = static_cast<char*>(heap_reallocate(buffer, buffer_size + extra));
        if (!buffer) {
            return nullptr;
        }
    }
    buffer[bytes] = '\0';
    char* slash = find_last_char(buffer, '/');
    *slash = '\0';
    *size = buffer_size + extra;
    return buffer;
}

#else // !defined(PORTABLE_BUILD)

static char* get_xdg_base_folder(const char* env, const char* xdg_default_place, int extra, int* size) {
    char* buffer;
    int buffer_size = extra;
    const char* env_folder = getenv(env);
    if (env_folder && *env_folder) {
        buffer_size += string_size(env_folder);
        buffer = ALLOCATE(char, buffer_size);
        if (!buffer) {
            return nullptr;
        }
        copy_string(buffer, buffer_size, env_folder);
    } else {
        const char* home_folder;
        home_folder = getenv("HOME");
        ASSERT(home_folder);
        int word_size = sysconf(_SC_GETPW_R_SIZE_MAX);
        if (word_size < 0) {
            word_size = 1024;
        }
        char* word_storage = nullptr;
        passwd word;
        if (!home_folder) {
            word_storage = ALLOCATE(char, word_size);
            if (!word_storage) {
                return nullptr;
            }
            passwd* pw;
            int result = getpwuid_r(getuid(), &word, word_storage, word_size, &pw);
            if (result == 0) {
                home_folder = pw->pw_dir;
            } else {
                DEALLOCATE(word_storage);
                return nullptr;
            }
        }
        buffer_size += string_size(home_folder) + string_size(xdg_default_place);
        buffer = ALLOCATE(char, buffer_size);
        if (!buffer) {
            SAFE_DEALLOCATE(word_storage);
            return nullptr;
        }
        copy_string(buffer, buffer_size, home_folder);
        append_string(buffer, buffer_size, xdg_default_place);
        SAFE_DEALLOCATE(word_storage);
    }
    *size = buffer_size;
    return buffer;
}

static char* get_base_folder(BaseType type, int extra, int* size) {
    char* buffer;
    int buffer_size = extra;
    switch (type) {
        case BaseType::Assets: {
            const char* usr_share = "/usr/share";
            buffer_size += string_size(usr_share);
            buffer = ALLOCATE(char, buffer_size);
            if (!buffer) {
                return nullptr;
            }
            copy_string(buffer, buffer_size, usr_share);
            break;
        }
        case BaseType::Config: {
            buffer = get_xdg_base_folder("XDG_CONFIG_HOME", "/.config", extra, &buffer_size);
            break;
        }
        case BaseType::Saves: {
            buffer = get_xdg_base_folder("XDG_DATA_HOME", "/.local/share", extra, &buffer_size);
            break;
        }
    }
    *size = buffer_size;
    return buffer;
}

#endif // !defined(PORTABLE_BUILD)

static bool close_file(int file, const char* path) {
    int closed = close(file);
    if (closed == -1) {
        LOG_ERROR("The file %s could not be closed. %s", path, strerror(errno));
        return false;
    }
    ASSERT(closed == 0);
    return true;
}

enum class OpenMode { Read, Write, Append };

static int open_file(const char* path, OpenMode open_mode) {
    int flags;
    switch (open_mode) {
        case OpenMode::Read: {
            flags = O_RDONLY;
            break;
        }
        case OpenMode::Write: {
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        }
        case OpenMode::Append: {
            flags = O_WRONLY | O_CREAT | O_APPEND;
            break;
        }
    }
    mode_t mode = S_IRUSR | S_IRGRP | S_IROTH
                | S_IWUSR | S_IWGRP | S_IWOTH;
    int file = open(path, flags, mode);
    if (file == -1) {
        LOG_ERROR("The file %s could not be opened. %s", path, strerror(errno));
    }
    return file;
}

static int open_file_by_type(FileType type, const char* path, OpenMode open_mode) {
    char* full_path;
    switch (type) {
        default: {
            full_path = resolve_asset_path(nullptr, path);
            break;
        }
        case FileType::Asset_Shader: {
            full_path = resolve_asset_path("Shaders", path);
            break;
        }
        case FileType::Config: {
            full_path = resolve_config_path(path);
            break;
        }
        case FileType::Saved_Game: {
            full_path = resolve_saved_game_path(path);
            break;
        }
    }
    if (!full_path) {
        LOG_ERROR("The file name %s could not be resolved to a full path.", path);
        return -1;
    }
    int file = open_file(full_path, open_mode);
    DEALLOCATE(full_path);
    return file;
}

bool load_whole_file(FileType type, const char* path, void** data, s64* size) {
    ASSERT(path);
    ASSERT(data);
    ASSERT(size);
    int file = open_file_by_type(type, path, OpenMode::Read);
    if (file == -1) {
        return false;
    }
    struct stat status;
    int status_obtained = fstat(file, &status);
    if (status_obtained == -1) {
        LOG_ERROR("The size of the file %s could not be determined. %s", path, strerror(errno));
        close_file(file, path);
        return false;
    }
    s64 byte_count = status.st_size;
    void* memory = ALLOCATE(u8, byte_count + 1);
    if (!memory) {
        LOG_ERROR("The memory needed to store the file %s could not be allocated.", path);
        close_file(file, path);
        return false;
    }
    ssize_t bytes_read = read(file, memory, byte_count);
    static_cast<u8*>(memory)[byte_count] = 0; // null-terminate whether it needs to be or not
    int read_error = errno;
    bool closed = close_file(file, path);
    if (!closed) {
        DEALLOCATE(memory);
        return false;
    }
    if (bytes_read == -1) {
        LOG_ERROR("The file %s could not be read. %s", path, strerror(read_error));
        DEALLOCATE(memory);
        return false;
    } else if (bytes_read < byte_count) {
        LOG_ERROR("The file %s was only partially read.", path);
        DEALLOCATE(memory);
        return false;
    }
    ASSERT(bytes_read > 0 && bytes_read <= byte_count);
    *size = byte_count;
    *data = memory;
    return true;
}

bool save_whole_file(FileType type, const char* path, const void* data, s64 size) {
    ASSERT(path);
    ASSERT(data);
    int file = open_file_by_type(type, path, OpenMode::Write);
    if (file == -1) {
        return false;
    }
    ssize_t written = write(file, data, size);
    int write_error = errno;
    bool closed = close_file(file, path);
    if (written == -1) {
        LOG_ERROR("The file %s could not be written to. %s", path, strerror(write_error));
        return false;
    } else if (written < size) {
        LOG_ERROR("The file %s was not completely written.", path);
        return false;
    }
    ASSERT(written > 0 && written <= size);
    return closed;
}

void delete_config_file_if_too_large(const char* path, int limit) {
    char* full_path = resolve_config_path(path);
    if (!full_path) {
        LOG_ERROR("The file name %s could not be resolved to a full path.", path);
        return;
    }
    struct stat status;
    int got_status = stat(full_path, &status);
    if (got_status == -1) {
        LOG_ERROR("The file %s was not able to be queried for potential clearing.", full_path);
    } else if (status.st_size > limit) {
        int removed = unlink(full_path);
        if (removed == -1) {
            LOG_ERROR("The file %s was not removed as requested.", full_path);
        }
    }
    DEALLOCATE(full_path);
}

struct File {
    char* path;
    int handle;
    FileMode mode;
};

File* open_file(FileType type, FileMode mode, const char* path) {
    ASSERT(path);
    if (mode == FileMode::Write) {
        ASSERT(type == FileType::Config || type == FileType::Saved_Game);
    } else if (mode == FileMode::Read) {
        ASSERT(!(type == FileType::Config || type == FileType::Saved_Game));
    }
    File* file = ALLOCATE(File, 1);
    if (!file) {
        LOG_ERROR("The memory needed to store data for file %s could not be allocated.", path);
        return nullptr;
    }
    file->mode = mode;
    OpenMode open_mode;
    switch (mode) {
        case FileMode::Read:  { open_mode = OpenMode::Read;   break; }
        case FileMode::Write: { open_mode = OpenMode::Append; break; }
    }
    int handle = open_file_by_type(type, path, open_mode);
    if (handle == -1) {
        close_file(file);
        return nullptr;
    }
    file->handle = handle;
    int path_size = string_size(path);
    char* path_copy = ALLOCATE(char, path_size);
    if (!path_copy) {
        LOG_ERROR("The memory needed to store the path of file %s could not be allocated.", path);
        close_file(file);
        return nullptr;
    }
    file->path = path_copy;
    copy_string(file->path, path_size, path);
    return file;
}

void close_file(File* file) {
    if (file) {
        close_file(file->handle, file->path);
        SAFE_DEALLOCATE(file->path);
        DEALLOCATE(file);
    }
}

bool write_file(File* file, const void* data, s64 size) {
    ASSERT(file);
    ASSERT(file->handle);
    ASSERT(data);
    ASSERT(file->mode == FileMode::Write);
    ssize_t written = write(file->handle, data, size);
    if (written == -1) {
        LOG_ERROR("The file %s could not be written to. %s", file->path, strerror(errno));
        return false;
    } else if (written < size) {
        LOG_ERROR("The file %s was not completely written.", file->path);
        return false;
    }
    ASSERT(written > 0 && written <= size);
    return true;
}

s64 read_file(File* file, void* data, s64 size) {
    ASSERT(file);
    ASSERT(file->handle);
    ASSERT(data);
    ASSERT(file->mode == FileMode::Read);
    ssize_t bytes_read = read(file->handle, data, size);
    if (bytes_read == -1) {
        LOG_ERROR("The file %s could not be read from. %s", file->path, strerror(errno));
        return 0;
    }
    return bytes_read;
}

s64 seek_file(File* file, s64 offset) {
    ASSERT(file);
    ASSERT(file->handle);
    ASSERT(offset >= 0);
    s64 seeked_to = lseek64(file->handle, offset, SEEK_SET);
    ASSERT(seeked_to != -1);
    return seeked_to;
}

s64 seek_file_forward(File* file, s64 offset) {
    ASSERT(file);
    ASSERT(file->handle);
    s64 seeked_to = lseek64(file->handle, offset, SEEK_CUR);
    ASSERT(seeked_to != -1);
    return seeked_to;
}

s64 get_file_size(File* file) {
    ASSERT(file);
    ASSERT(file->handle);
    struct stat status;
    int got_status = fstat(file->handle, &status);
    ASSERT(got_status != -1);
    if (got_status == -1) {
        return 0;
    }
    return status.st_size;
}

s64 get_file_offset(File* file) {
    ASSERT(file);
    ASSERT(file->handle);
    s64 seeked_to = lseek64(file->handle, 0, SEEK_CUR);
    ASSERT(seeked_to != -1);
    return seeked_to;
}

void print(const char* string, bool is_error) {
    if (is_error) {
        fputs(string, stderr);
    } else {
        fputs(string, stdout);
    }
}

#endif // defined(PLATFORM_LINUX)
