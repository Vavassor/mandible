#pragma once

#include "sized_types.h"

namespace random {

void seed(u32 seed);
u32 generate();
int int_range(int min, int max);
float float_range(float min, float max);
double double_range(double min, double max);

} // namespace random
