#pragma once

namespace arandom {

void seed(unsigned long seed);
unsigned long generate();
int int_range(int min, int max);
float float_range(float min, float max);
double double_range(double min, double max);

} // namespace random
