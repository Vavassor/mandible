#pragma once

namespace logging {
	enum class Level { Error, Info, Debug, };
	void add_message(Level level, const char* format, ...);
}

#ifdef NDEBUG
#define LOG_DEBUG(format, ...) // do nothing
#else
#define LOG_DEBUG(format, ...) logging::add_message(logging::Level::Debug, (format), ##__VA_ARGS__)
#endif

#define	LOG_ERROR(format, ...) logging::add_message(logging::Level::Error, (format), ##__VA_ARGS__)
