#pragma once

namespace perlin_noise {

struct Vector2 { double x, y; };

struct Source {
    Vector2 field[32][32];
};

void set_gradient(Source* source, int i, int j, double x, double y);
double generate_2d(Source* source, double x, double y);

} // namespace perlin_noise

double perlin_noise_3d(double x, double y, double z, int octaves, double persistence, double scale);
