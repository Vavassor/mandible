#include "logging.h"

#include <cstdarg>
#include <cstdio>

namespace logging {

void add_message(Level level, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	std::vprintf(format, arguments);
	va_end(arguments);
	std::putchar('\n');
}

} // namespace logging
