// This is based on Sean Barrett's public domain stb_perlin.h
// with revisions by Andrew Dawson and added octave noise functions

#include "perlin_noise.h"

#include <cmath>

using std::floor;

// @OPTIMIZE: should this be unsigned char instead of int for cache?
static const int table[512] = {
    23, 125, 161, 52, 103, 117, 70, 37, 247, 101, 203, 169, 124, 126, 44, 123,
    152, 238, 145, 45, 171, 114, 253, 10, 192, 136, 4, 157, 249, 30, 35, 72,
    175, 63, 77, 90, 181, 16, 96, 111, 133, 104, 75, 162, 93, 56, 66, 240,
    8, 50, 84, 229, 49, 210, 173, 239, 141, 1, 87, 18, 2, 198, 143, 57,
    225, 160, 58, 217, 168, 206, 245, 204, 199, 6, 73, 60, 20, 230, 211, 233,
    94, 200, 88, 9, 74, 155, 33, 15, 219, 130, 226, 202, 83, 236, 42, 172,
    165, 218, 55, 222, 46, 107, 98, 154, 109, 67, 196, 178, 127, 158, 13, 243,
    65, 79, 166, 248, 25, 224, 115, 80, 68, 51, 184, 128, 232, 208, 151, 122,
    26, 212, 105, 43, 179, 213, 235, 148, 146, 89, 14, 195, 28, 78, 112, 76,
    250, 47, 24, 251, 140, 108, 186, 190, 228, 170, 183, 139, 39, 188, 244, 246,
    132, 48, 119, 144, 180, 138, 134, 193, 82, 182, 120, 121, 86, 220, 209, 3,
    91, 241, 149, 85, 205, 150, 113, 216, 31, 100, 41, 164, 177, 214, 153, 231,
    38, 71, 185, 174, 97, 201, 29, 95, 7, 92, 54, 254, 191, 118, 34, 221,
    131, 11, 163, 99, 234, 81, 227, 147, 156, 176, 17, 142, 69, 12, 110, 62,
    27, 255, 0, 194, 59, 116, 242, 252, 19, 21, 187, 53, 207, 129, 64, 135,
    61, 40, 167, 237, 102, 223, 106, 159, 197, 189, 215, 137, 36, 32, 22, 5,

    // and a second copy so we don't need an extra mask or static initializer
    23, 125, 161, 52, 103, 117, 70, 37, 247, 101, 203, 169, 124, 126, 44, 123,
    152, 238, 145, 45, 171, 114, 253, 10, 192, 136, 4, 157, 249, 30, 35, 72,
    175, 63, 77, 90, 181, 16, 96, 111, 133, 104, 75, 162, 93, 56, 66, 240,
    8, 50, 84, 229, 49, 210, 173, 239, 141, 1, 87, 18, 2, 198, 143, 57,
    225, 160, 58, 217, 168, 206, 245, 204, 199, 6, 73, 60, 20, 230, 211, 233,
    94, 200, 88, 9, 74, 155, 33, 15, 219, 130, 226, 202, 83, 236, 42, 172,
    165, 218, 55, 222, 46, 107, 98, 154, 109, 67, 196, 178, 127, 158, 13, 243,
    65, 79, 166, 248, 25, 224, 115, 80, 68, 51, 184, 128, 232, 208, 151, 122,
    26, 212, 105, 43, 179, 213, 235, 148, 146, 89, 14, 195, 28, 78, 112, 76,
    250, 47, 24, 251, 140, 108, 186, 190, 228, 170, 183, 139, 39, 188, 244, 246,
    132, 48, 119, 144, 180, 138, 134, 193, 82, 182, 120, 121, 86, 220, 209, 3,
    91, 241, 149, 85, 205, 150, 113, 216, 31, 100, 41, 164, 177, 214, 153, 231,
    38, 71, 185, 174, 97, 201, 29, 95, 7, 92, 54, 254, 191, 118, 34, 221,
    131, 11, 163, 99, 234, 81, 227, 147, 156, 176, 17, 142, 69, 12, 110, 62,
    27, 255, 0, 194, 59, 116, 242, 252, 19, 21, 187, 53, 207, 129, 64, 135,
    61, 40, 167, 237, 102, 223, 106, 159, 197, 189, 215, 137, 36, 32, 22, 5,
};

static double ease(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

static double grad(int hash, double x, double y, double z) {
    static const double basis[12][4] = {
        {  1, 1, 0 },
        { -1, 1, 0 },
        {  1,-1, 0 },
        { -1,-1, 0 },
        {  1, 0, 1 },
        { -1, 0, 1 },
        {  1, 0,-1 },
        { -1, 0,-1 },
        {  0, 1, 1 },
        {  0,-1, 1 },
        {  0, 1,-1 },
        {  0,-1,-1 },
    };

    // perlin's gradient has 12 cases so some get used 1/16th of the time
    // and some 2/16ths. We reduce bias by changing those fractions
    // to 5/16ths and 6/16ths, and the same 4 cases get the extra weight.
    static const unsigned char indices[64] = {
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,9,1,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
    };

    const double* grad = basis[indices[hash & 63]];
    return grad[0] * x + grad[1] * y + grad[2] * z;
}

// This function computes a random value at the coordinate (x,y,z).
// Adjacent random values are continuous but the noise fluctuates
// its randomness with period 1, i.e. takes on wholly unrelated values
// at integer points. Specifically, this implements Ken Perlin's
// revised noise function from 2002.
double noise(double x, double y, double z) {
    int px = floor(x);
    int py = floor(y);
    int pz = floor(z);
    int x0 = px & 0xFF, x1 = (px+1) & 0xFF;
    int y0 = py & 0xFF, y1 = (py+1) & 0xFF;
    int z0 = pz & 0xFF, z1 = (pz+1) & 0xFF;

    x -= px; double u = ease(x);
    y -= py; double v = ease(y);
    z -= pz; double w = ease(z);

    int r0 = table[x0];
    int r1 = table[x1];

    int r00 = table[r0+y0];
    int r01 = table[r0+y1];
    int r10 = table[r1+y0];
    int r11 = table[r1+y1];

    double n000 = grad(table[r00+z0], x  , y  , z   );
    double n001 = grad(table[r00+z1], x  , y  , z-1 );
    double n010 = grad(table[r01+z0], x  , y-1, z   );
    double n011 = grad(table[r01+z1], x  , y-1, z-1 );
    double n100 = grad(table[r10+z0], x-1, y  , z   );
    double n101 = grad(table[r10+z1], x-1, y  , z-1 );
    double n110 = grad(table[r11+z0], x-1, y-1, z   );
    double n111 = grad(table[r11+z1], x-1, y-1, z-1 );

    double n00 = lerp(n000, n001, w);
    double n01 = lerp(n010, n011, w);
    double n10 = lerp(n100, n101, w);
    double n11 = lerp(n110, n111, w);

    double n0 = lerp(n00, n01, v);
    double n1 = lerp(n10, n11, v);

    return lerp(n0, n1, u);
}

double perlin_noise_3d(double x, double y, double z, int octaves, double persistence, double scale) {
    double total = 0.0;
    double frequency = scale;
    double amplitude = 1.0;
    double combined_amplitude = 0.0;
    for (int i = 0; i < octaves; ++i) {
        total += amplitude * noise(frequency * x, frequency * y, frequency * z);
        combined_amplitude += amplitude;
        amplitude *= persistence;
        frequency *= 2.0; // 2 is the lacunarity
    }
    return total / combined_amplitude;
}

namespace perlin_noise {

void set_gradient(Source* source, int i, int j, double x, double y) {
    if (i < 0 || i >= 32 || j < 0 || j >= 32) {
        return;
    }
    double length = sqrt(x * x + y * y);
    if (length != 0.0) {
        x /= length;
        y /= length;
        source->field[i][j].x = x;
        source->field[i][j].y = y;
    } else {
        source->field[i][j].x = 1.0;
        source->field[i][j].y = 0.0;
    }
}

static double dot(Vector2 v, double x, double y) {
    return v.x * x + v.y * y;
}

double generate_2d(Source* source, double x, double y) {
    int px = floor(x);
    int py = floor(y);
    int x0 = px & 0x1F;
    int y0 = py & 0x1F;

    x -= px;
    y -= py;
    double u = ease(x);
    double v = ease(y);

    Vector2 a00 = source->field[x0  ][y0  ];
    Vector2 a01 = source->field[x0  ][y0+1];
    Vector2 a10 = source->field[x0+1][y0  ];
    Vector2 a11 = source->field[x0+1][y0+1];

    double n00 = dot(a00, x,   y  );
    double n01 = dot(a01, x,   y-1);
    double n10 = dot(a10, x-1, y  );
    double n11 = dot(a11, x-1, y-1);

    double n0 = lerp(n00, n01, v);
    double n1 = lerp(n10, n11, v);

    return lerp(n0, n1, u);
}

} // namespace perlin_noise
