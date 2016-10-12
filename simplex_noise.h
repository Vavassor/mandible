#pragma once

#include "sized_types.h"

namespace simplex_noise {

struct Source {
    s16 perm[256];
    s16 permGradIndex3D[256];
};

void seed(Source* s, s64 seed);
double generate2D(Source* s, double x, double y);
double generate3D(Source* s, double x, double y, double z);

} // namespace simplex_noise
