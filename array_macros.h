#pragma once

#include <cstddef>

#define ARRAY_COUNT(a) \
    ((sizeof(a) / sizeof(*(a))) / static_cast<std::size_t>(!(sizeof(a) % sizeof(*(a)))))

#define FOR_N(index, n) \
    for (auto (index) = 0; (index) < (n); ++(index))
