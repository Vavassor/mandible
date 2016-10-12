#include "random.h"

namespace random {

// Linear Congruential Generator
namespace lcg {

namespace {
    u32 current_seed = 0;
}

static u32 seed(u32 next) {
    u32 previous = current_seed;
    current_seed = next;
    return previous;
}

static u32 generate() {
    // BCPL generator
    current_seed = current_seed * 2147001325 + 715136305;
    // Shuffle non-random bits to the middle, and xor to decorrelate with seed.
    return 0x31415926 ^ ((current_seed >> 16) + (current_seed << 16));
}

} // namespace lcg

// Mersenne Twister random number generator
// adapted from a public domain implementation by Michael Brundage
namespace mt {

#define TWIST(b,i,j) ((b)[i] & 0x80000000ul) | ((b)[j] & 0x7FFFFFFFul)
#define MAGIC(s)     (((s) & 1) * 0x9908B0DFul)

namespace {
    const int buffer_length = 624;
    const int IA            = 379;
    const int IB            = buffer_length - IA;
    u32 buffer[buffer_length];
    int index = buffer_length + 1;
}

static void seed(u32 next) {
    u32 previous = lcg::seed(next);
    for (int i = 0; i < buffer_length; ++i) {
        buffer[i] = lcg::generate();
    }
    lcg::seed(previous);
    index = buffer_length;
}

static u32 generate() {
    // a random number to be generated on the interval [0,0xffffffff]
    u32 r;

    // Generate a whole buffer-full of numbers at a time.
    if (index >= buffer_length) {
        if (index > buffer_length) {
            seed(0ul);
        }

        u32 s;
        int i;
        for (i = 0; i < IB; ++i) {
            s = TWIST(buffer, i, i + 1);
            buffer[i] = buffer[i + IA] ^ (s >> 1) ^ MAGIC(s);
        }
        for (; i < buffer_length - 1; ++i) {
            s = TWIST(buffer, i, i + 1);
            buffer[i] = buffer[i - IB] ^ (s >> 1) ^ MAGIC(s);
        }
        s = TWIST(buffer, buffer_length - 1, 0);
        buffer[buffer_length - 1] = buffer[IA - 1] ^ (s >> 1) ^ MAGIC(s);

        index = 0;
    }

    // Take the next number out of the buffer, and swizzle it before returning.
    r = buffer[index++];

    r ^= (r >> 11);
    r ^= (r << 7) & 0x9D2C5680ul;
    r ^= (r << 15) & 0xEFC60000ul;
    r ^= (r >> 18);

    return r;
}

} // namespace mt

void seed(u32 next) {
    mt::seed(next);
}

u32 generate() {
    return mt::generate();
}

int int_range(int min, int max) {
    return min + static_cast<int>(generate() % static_cast<u32>(max - min + 1));
}

float float_range(float min, float max) {
    float f = generate() * (1.0f / 4294967295.0f);
    return min + f * (max - min);
}

double double_range(double min, double max) {
    double d = generate() * (1.0 / 4294967295.0);
    return min + d * (max - min);
}

} // namespace random
