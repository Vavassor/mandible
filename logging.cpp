#include "logging.h"

#include "asset_handling.h"
#include "string_utilities.h"
#include "assert.h"

#include <pthread.h>

#include <cstdarg>

using std::vsnprintf;

namespace logging {

#define LOG_FILE_NAME "/mandible.log"
#define MAX_LOG_SIZE  32768

namespace {
    pthread_mutex_t mutex;
    File* file;
}

static void lock() {
    pthread_mutex_lock(&mutex);
}

static void unlock() {
    pthread_mutex_unlock(&mutex);
}

static void get_time_as_text(char* string, int size) {
    time_t raw_time;
    time(&raw_time);
    tm* now = localtime(&raw_time);
    strftime(string, size, "%Y-%m-%d %H:%M:%S%z", now);
}

static void log_session_started_marker() {
    char time_text[25];
    get_time_as_text(time_text, sizeof time_text);
    LOG_INFO("Session Started. %s\n", time_text);
}

static void log_session_ended_marker() {
    char time_text[25];
    get_time_as_text(time_text, sizeof time_text);
    LOG_INFO("\nSession Ended. %s\n", time_text);
}

bool startup() {
    pthread_mutex_init(&mutex, nullptr);

    lock();
    delete_config_file_if_too_large(LOG_FILE_NAME, MAX_LOG_SIZE);
    file = open_file(FileType::Config, FileMode::Write, LOG_FILE_NAME);
    bool result = file;
    unlock();

    log_session_started_marker();

    return result;
}

void shutdown() {
    log_session_ended_marker();
    lock();
    close_file(file);
    unlock();
    pthread_mutex_destroy(&mutex);
}

void add_message(Level level, const char* format, ...) {
    char message[256];
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(message, sizeof message - 1, format, arguments);
    ASSERT(written > 0 && written < sizeof message - 1);
    va_end(arguments);
    if (written > 0) {
        message[written] = '\n';
        message[written + 1] = '\0';
        print(message, level == Level::Error);
        if (file) {
            lock();
            write_file(file, message, written + 1);
            unlock();
        }
    }
}

} // namespace logging
