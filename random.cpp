#include "random.h"

namespace arandom {

// Linear Congruential Generator
namespace lcg {

namespace {
    unsigned long current_seed = 0;
}

static unsigned long seed(unsigned long next) {
    unsigned long previous = current_seed;
    current_seed = next;
    return previous;
}

static unsigned long generate() {
    // BCPL generator
    current_seed = current_seed * 2147001325 + 715136305;
    // Shuffle non-random bits to the middle, and xor to decorrelate with seed.
    return 0x31415926 ^ ((current_seed >> 16) + (current_seed << 16));
}

} // namespace lcg

// Mersenne Twister
namespace mt {

#define BUFFER_LENGTH 624
#define IA            379
#define IB            (BUFFER_LENGTH - IA)
#define TWIST(b,i,j)  ((b)[i] & 0x80000000ul) | ((b)[j] & 0x7FFFFFFFul)
#define MAGIC(s)      (((s) & 1) * 0x9908B0DFul)

namespace {
    unsigned long buffer[BUFFER_LENGTH];
    int index = BUFFER_LENGTH + 1;
}

static void seed(unsigned long next) {
    unsigned long previous = lcg::seed(next);
    for (int i = 0; i < BUFFER_LENGTH; ++i) {
        buffer[i] = lcg::generate();
    }
    lcg::seed(previous);
    index = BUFFER_LENGTH;
}

static unsigned long generate() {
    // a random number to be generated on the interval [0,0xffffffff]
    unsigned long r;

    // Generate a whole buffer-full of numbers at a time.
    if (index >= BUFFER_LENGTH) {
        if (index > BUFFER_LENGTH) {
            seed(0ul);
        }

        unsigned long s;
        int i;
        for (i = 0; i < IB; ++i) {
            s = TWIST(buffer, i, i + 1);
            buffer[i] = buffer[i + IA] ^ (s >> 1) ^ MAGIC(s);
        }
        for (; i < BUFFER_LENGTH - 1; ++i) {
            s = TWIST(buffer, i, i + 1);
            buffer[i] = buffer[i - IB] ^ (s >> 1) ^ MAGIC(s);
        }
        s = TWIST(buffer, BUFFER_LENGTH - 1, 0);
        buffer[BUFFER_LENGTH - 1] = buffer[IA - 1] ^ (s >> 1) ^ MAGIC(s);

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

void seed(unsigned long next) {
    mt::seed(next);
}

unsigned long generate() {
    return mt::generate();
}

int int_range(int min, int max) {
    return min + static_cast<int>(generate() % static_cast<unsigned long>(max - min + 1));
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
