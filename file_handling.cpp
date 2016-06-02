#include "file_handling.h"

#include "logging.h"
#include "memory.h"

#if defined(_WIN32)
#define OS_WINDOWS
#elif defined(__unix__)
#define OS_POSIX
#endif

#if defined(OS_WINDOWS)
#include <Windows.h>
#elif defined(OS_POSIX)
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#endif

#include <cassert>
#include <cstring>

#if defined(OS_WINDOWS)

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
    assert(log_buffer); // stack allocation should never be null
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

    int wide_count = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wide_count == 0) {
        LOG_ERROR("The file path %s could not be converted to wide characters.", path);
        log_thread_error();
        return INVALID_HANDLE_VALUE;
    }
    assert(wide_count > 0);
    wchar_t* wide_path = STACK_ALLOCATE(wchar_t, wide_count);
    assert(wide_path); // stack allocation should never be null
    int chars_converted = MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, wide_count);
    if (chars_converted != wide_count) {
        LOG_ERROR("The file path %s could not be converted to wide characters", path);
        log_thread_error();
        return INVALID_HANDLE_VALUE;
    }

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
    if (file == INVALID_FILE_HANDLE) {
        LOG_ERROR("The file %s could not be opened.", path);
        log_thread_error();
    }
    return file;
}

static bool close_file(HANDLE file) {
    BOOL closed = CloseHandle(file);
    if (closed == FALSE) {
        LOG_ERROR("The file %s could not be closed.", path);
        log_thread_error();
        return false;
    }
    return true;
}

bool load_whole_file(const char* path, void** data, s64* size) {
    assert(path);
    assert(data);
    assert(size);
    HANDLE file = open_file(path, OpenMode::Read);
    if (file == INVALID_FILE_HANDLE) {
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
    void* memory = ALLOCATE(u8, byte_count);
    if (!memory) {
        LOG_ERROR("The memory needed to store the file %s could not be allocated.", path);
        goto failure_before_read;
    }
    DWORD bytes_read;
    BOOL read = ReadFile(file->handle, memory, byte_count, &bytes_read, nullptr);
    DWORD read_error = GetLastError();
    bool closed = close_file(file);
    if (!closed) {
        goto failure_after_close;
    }
    if (read == FALSE) {
        LOG_ERROR("The file %s could not be read.", path);
        log_thread_error(read_error);
        goto failure_after_close;
    } else if (bytes_read != bytes_count) {
        LOG_ERROR("The file %s was only partially read.", path);
        goto failure_after_close;
    }

    *data = memory;
    *size = byte_count;
    return true;

failure_before_read:
    close_file(file);
    return false;

failure_after_close:
    DEALLOCATE(memory);
    return false;
}

bool save_whole_file(const char* path, const void* data, s64 size) {
    assert(path);
    assert(data);
    HANDLE file = open_file(path, OpenMode::Write);
    if (file == INVALID_FILE_HANDLE) {
        return false;
    }
    DWORD bytes_written;
    BOOL writ = WriteFile(file, data, size, &bytes_written, nullptr);
    DWORD write_error = GetLastError();
    close_file(file);
    if (writ == FALSE) {
        LOG_ERROR("The file %s could not be written to.", path);
        log_thread_error(write_error);
        return false;
    } else if (bytes_written != size) {
        LOG_ERROR("The file %s was not completely written.", path);
        return false;
    }
    assert(bytes_written > 0 && bytes_written <= size);
    return true;
}

#elif defined(OS_POSIX)

static bool close_file(int file, const char* path) {
    int closed = close(file);
    if (closed == -1) {
        LOG_ERROR("The file %s could not be closed. %s", path, strerror(errno));
        return false;
    }
    assert(closed == 0);
    return true;
}

bool load_whole_file(const char* path, void** data, s64* size) {
    assert(path);
    assert(data);
    assert(size);
    int flags = O_RDONLY;
    mode_t mode = S_IRUSR | S_IRGRP | S_IROTH;
    int file = open(path, flags, mode);
    if (file == -1) {
        LOG_ERROR("The file %s could not be opened. %s", path, strerror(errno));
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
    assert(bytes_read > 0 && bytes_read <= byte_count);
    *size = byte_count;
    *data = memory;
    return true;
}

bool save_whole_file(const char* path, const void* data, s64 size) {
    assert(path);
    assert(data);
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    mode_t mode = S_IWUSR | S_IWGRP | S_IWOTH;
    int file = open(path, flags, mode);
    if (file == -1) {
        LOG_ERROR("The file %s could not be opened. %s", path, strerror(errno));
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
    assert(written > 0 && written <= size);
    return closed;
}

#endif // defined(OS_POSIX)
