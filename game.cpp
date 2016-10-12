#include "game.h"

#include "draw.h"
#include "input.h"
#include "audio.h"
#include "stb_image.h"
#include "random.h"
#include "string_utilities.h"
#include "array_macros.h"
#include "memory.h"
#include "logging.h"
#include "profile.h"
#include "ani_file.h"
#include "wor_file.h"
#include "asset_handling.h"
#include "simplex_noise.h"
#include "perlin_noise.h"
#include "cellular_automata.h"
#include "assert.h"

#include <cmath>
#include <cfloat>
#include <cstdio>
#include <ctime>

using std::abs;
using std::fmax;
using std::fmin;
using std::isless;
using std::snprintf;
using std::time;
using std::sin;
using std::cos;

static const float pi = 3.141592358979323f;
static const float tau = 6.2831853071f;

static bool load_atlas(Atlas* atlas, const char* name, Heap* heap,
                       Stack* stack) {
    void* data;
    s64 data_size;
    bool loaded = load_whole_file(FileType::Asset_Image, name, &data,
                                  &data_size, heap, stack);
    if (!loaded) {
        return false;
    }
    atlas->data = stbi_load_from_memory(static_cast<u8*>(data), data_size,
                                        &atlas->width, &atlas->height,
                                        &atlas->bytes_per_pixel, 0);
    if (!atlas->data) {
        LOG_ERROR("Could not decode image %s. STBI reason %s", name,
                  stbi_failure_reason());
        return false;
    }
    DEALLOCATE(heap, data);
    return atlas->data;
}

static void unload_atlas(Atlas* atlas) {
    stbi_image_free(atlas->data);
}

// @Incomplete: This needs to be checked for robustness.
// Also check the effects of /fp:fast (MSVC) and -funsafe-math-optimizations (GCC).
bool doubles_equal(double x, double y) {
    double fx = abs(x);
    double fy = abs(y);
    double max_value = fmax(fmax(1.0, fx), fy);
    double difference = abs(x - y);
    double epsilon = DBL_EPSILON * max_value;
    return isless(difference, epsilon);
}

bool double_is_zero(double x) {
    return doubles_equal(x, 0.0);
}

enum Facing {
    FACING_NORTH,
    FACING_SOUTH,
    FACING_EAST,
    FACING_WEST
};

struct AnimationState {
    Facing facing;
    int ticks;
    int frame_index;
};

static void reset(AnimationState* state) {
    state->frame_index = 0;
    state->ticks = 0;
}

static void set_facing(AnimationState* state, Facing facing) {
    if (state->facing != facing) {
        reset(state);
    }
    state->facing = facing;
}

static void cycle_increment(int* s, int n) {
    *s = (*s + 1) % n;
}

static void cycle_decrement(int* s, int n) {
    *s = (*s + (n - 1)) % n;
}

// Diamond Square Functions....................................................

namespace diamond_square {

struct Lattice {
    float points[128][128];
    int side;
};

static void generate(Lattice* lattice, float scale) {
    for (int step = lattice->side; step >= 1; step /= 2, scale /= 2.0f) {
        // diamonds
        for (int i = step; i < lattice->side; i += step) {
            for (int j = step; j < lattice->side; j += step) {
                float a = lattice->points[i - step][j - step];
                float b = lattice->points[i       ][j - step];
                float c = lattice->points[i - step][j       ];
                float d = lattice->points[i       ][j       ];
                float e = (a + b + c + d) / 4.0f + random::float_range(-scale, scale);
                lattice->points[i - step / 2][j - step / 2] = e;
            }
        }
        // squares
        for (int i = 2 * step; i < lattice->side; i += step) {
            for (int j = 2 * step; j < lattice->side; j += step) {
                float a = lattice->points[i -     step    ][j -     step    ];
                float b = lattice->points[i               ][j -     step    ];
                float c = lattice->points[i -     step    ][j               ];
                float d = lattice->points[i               ][j               ];
                float e = lattice->points[i -     step / 2][j -     step / 2];
                float f = lattice->points[i - 3 * step / 2][j -     step / 2];
                float g = lattice->points[i -     step / 2][j - 3 * step / 2];
                float h = (a + c + e + f) / 4.0f + random::float_range(-scale, scale);
                float k = (a + b + e + g) / 4.0f + random::float_range(-scale, scale);
                lattice->points[i - step    ][j - step / 2] = h;
                lattice->points[i - step / 2][j - step    ] = k;
            }
        }
    }
}

static void draw(Canvas* canvas, Lattice* lattice, int cx, int cy,
                 float frequency, float phase) {
    FOR_N (y, lattice->side) {
        FOR_N (x, lattice->side) {
            float v = frequency * lattice->points[y][x] + phase;
            #if 0
            u8 r = (sin(pi * v                   ) + 1.0f) * 127.5f;
            u8 g = (sin(pi * v + 2.0f * pi / 3.0f) + 1.0f) * 127.5f;
            u8 b = (sin(pi * v + 4.0f * pi / 3.0f) + 1.0f) * 127.5f;
            #else
            u8 r = (sin(pi * v) + 1.0f) * 127.5f;
            u8 g = r;
            u8 b = r;
            #endif
            u32 colour = b << 16 | g << 8 | r;
            draw_rectangle(canvas, cx + x, cy + y, 1, 1, colour);
        }
    }
}

} // namespace diamond_square

struct Colour {
    float r;
    float g;
    float b;
};

float unlerp(float a, float b, float t) {
    ASSERT(t >= a && t <= b && a != b);
    return (t - a) / (b - a);
}

float lerp(float a, float b, float t) {
    ASSERT(t >= 0.0f && t <= 1.0f);
    return (1.0f - t) * a + t * b;
}

// Based on Dave Green's public domain (Unlicense license) Fortran 77
// implementation for cube helix colour table generation.
static void cube_helix(Colour* colours, int levels,
                       float start_hue, float rotations,
                       float min_saturation, float max_saturation,
                       float min_lightness, float max_lightness, float gamma) {
    int low = 0;
    int high = 0;

    FOR_N (i, levels) {
        float fraction = static_cast<float>(i) / static_cast<float>(levels);
        fraction = lerp(min_lightness, max_lightness, fraction);
        float saturation = lerp(min_saturation, max_saturation, fraction);
        float angle = tau * (start_hue / 3.0f + 1.0f + rotations * fraction);
        fraction = pow(fraction, gamma);
        float amplitude = saturation * fraction * (1.0f - fraction) / 2.0f;
        float r = -0.14861f * cos(angle) + 1.78277f * sin(angle);
        float g = -0.29227f * cos(angle) - 0.90649f * sin(angle);
        float b = 1.97294f * cos(angle);
        r = fraction + amplitude * r;
        g = fraction + amplitude * g;
        b = fraction + amplitude * b;

        if (r < 0.0f) {
            r = 0.0f;
            low += 1;
        }
        if (g < 0.0f) {
            g = 0.0f;
            low += 1;
        }
        if (b < 0.0) {
            b = 0.0f;
            low += 1;
        }

        if (r > 1.0f) {
            r = 1.0f;
            high += 1;
        }
        if (g > 1.0f) {
            g = 1.0f;
            high += 1;
        }
        if (b > 1.0f) {
            b = 1.0f;
            high += 1;
        }

        colours[i].r = r;
        colours[i].g = g;
        colours[i].b = b;
    }
}

static u32 fetch_colour(Colour* colours, int count, int value, int low, int high) {
    float u = unlerp(low, high, value);
    float v = lerp(0.0f, count, u);
    float v_truncated = floor(v);
    float t = v - v_truncated;
    int index = v_truncated;
    Colour* c0 = colours + index;
    Colour* c1 = colours + index + 1;
    u8 r = 255.0f * lerp(c0->r, c1->r, t);
    u8 g = 255.0f * lerp(c0->g, c1->g, t);
    u8 b = 255.0f * lerp(c0->b, c1->b, t);
    return b << 16 | g << 8 | r;
}

static float square_wave(float x) {
    return 4.0f * floor(x) - 2.0f * floor(2.0f * x) + 1.0f;
}

static float triangle_wave(float x) {
    return abs(4.0f * (x - floor(x)) - 2.0f) - 1.0f;
}

static float sawtooth_wave(float x) {
    return 2.0f * (x - floor(0.5f + x));
}

static void draw_diamond_square(Canvas* canvas,
                                diamond_square::Lattice* lattice,
                                int cx, int cy, float frequency, float phase,
                                Colour* colours, int colours_count) {
    FOR_N (y, lattice->side) {
        FOR_N (x, lattice->side) {
            float v = frequency * lattice->points[y][x] + phase;
            u8 d = 127.5f * (triangle_wave(v) + 1.0f);
            u32 colour = fetch_colour(colours, colours_count, d, 0, 256);
            draw_rectangle(canvas, cx + x, cy + y, 1, 1, colour);
        }
    }
}

#if 0
// @Unused: Kind of works?
static void draw_isolines(Canvas* canvas, diamond_square::Lattice* lattice,
                          int cx, int cy) {
    FOR_N (y, lattice->side) {
        FOR_N (x, lattice->side) {
            float t = lattice->points[y][x];
            if (triangle_wave(10.0f * t) < -0.6f) {
                draw_rectangle(canvas, cx + x, cy + y, 1, 1, 0xFFFFFF);
            }
        }
    }
}
#endif

// Verlet Integration Test Stuff??.............................................

struct Vector2 {
    float x;
    float y;
};

Vector2 operator + (const Vector2& a, const Vector2& b) {
    return { a.x + b.x, a.y + b.y };
}

Vector2 operator - (const Vector2& a, const Vector2& b) {
    return { a.x - b.x, a.y - b.y };
}

Vector2 operator * (float scalar, const Vector2& v) {
    return { scalar * v.x, scalar * v.y };
}

Vector2 operator * (const Vector2& v, float scalar) {
    return { scalar * v.x, scalar * v.y };
}

Vector2 operator / (const Vector2& v, float scalar) {
    return { v.x / scalar, v.y / scalar };
}

Vector2& operator += (Vector2& a, const Vector2& b) {
    a.x += b.x;
    a.y += b.y;
    return a;
}

Vector2& operator -= (Vector2& a, const Vector2& b) {
    a.x -= b.x;
    a.y -= b.y;
    return a;
}

Vector2& operator *= (Vector2& v, float scalar) {
    v.x *= scalar;
    v.y *= scalar;
    return v;
}

Vector2& operator /= (Vector2& v, float scalar) {
    v.x /= scalar;
    v.y /= scalar;
    return v;
}

float length(const Vector2& v) {
    return sqrt(v.x * v.x + v.y * v.y);
}

float square_length(const Vector2& v) {
    return (v.x * v.x) + (v.y * v.y);
}

Vector2 normalize(Vector2 v) {
    float d = length(v);
    ASSERT(d != 0.0f);
    return v / d;
}

Vector2 lerp(Vector2 a, Vector2 b, float t) {
    ASSERT(t >= 0.0f && t <= 1.0f);
    return (1.0f - t) * a + t * b;
}

static Vector2 limit_length(Vector2 v, float limit) {
    float d2 = square_length(v);
    if (d2 > (limit * limit)) {
        float d = sqrt(d2);
        v /= d;
        return limit * v;
    } else {
        return v;
    }
}

static const Vector2 vector2_zero = { 0.0f, 0.0f };

struct Particle {
    Vector2 position;
    Vector2 prior_position;
};

static void simulate_chain(Particle* particles, int count,
                           float resting_distance, float friction) {
    const int solve_steps = 6;

    FOR_N (i, solve_steps) {
        FOR_N (j, count - 1) {
            Particle* p1 = particles + j;
            Particle* p2 = particles + j + 1;
            Vector2 diff = p1->position - p2->position;
            float distance = length(diff);
            float push = (resting_distance - distance) / distance;
            Vector2 translate = 0.5f * push * diff;
            p1->position += translate;
            p2->position -= translate;
        }
    }

    // integrate by verlet
    FOR_N (i, count) {
        Particle* particle = particles + i;
        Vector2 velocity = particle->position - particle->prior_position;
        velocity *= friction;
        Vector2 next = particle->position + velocity;
        particle->prior_position = particle->position;
        particle->position = next;
    }
}

// Axis-Aligned Bounding Box
struct AABB {
    Vector2 center;
    Vector2 extents;
};

static AABB compute_bounds(Particle* particles, int count, int radius) {
    Vector2 min = {  FLT_MAX,  FLT_MAX };
    Vector2 max = { -FLT_MAX, -FLT_MAX };
    FOR_N (i, count) {
        Vector2 position = particles[i].position;
        min.x = fmin(min.x, position.x - radius);
        min.y = fmin(min.y, position.y - radius);
        max.x = fmax(max.x, position.x + radius);
        max.y = fmax(max.y, position.y + radius);
    }
    AABB box;
    box.extents = 0.5f * (max - min);
    box.center = min + box.extents;
    return box;
}

// Tests For Drawing Tiled Things..............................................

static void draw_wavy_tiles(Canvas* canvas, Atlas* atlas, int cx, int cy) {
    const int columns = 64;
    const int rows = 64;
    const double k = 15; // angular frequency
    const double w = 15; // wavenumber
    static double t = 0.0; // time
    t += 0.01;
    FOR_N (i, columns) {
        FOR_N (j, rows) {
            int x = j - rows / 2;
            int y = i - columns / 2;
            double r = sqrt((x * x) + (y * y));
            double u = cos(k * r + w * t);
            u8 c = u > 0.0 ? 255 : 0;
            int px = cx + j * 3;
            int py = cy + i * 3;
            #if 0
            int tx = c > 127 ? 0 : 3;
            draw_image(canvas, atlas, px, py, tx, 0, 3, 3);
            #else
            u32 colour = c << 16 | c << 8 | c;
            draw_rectangle(canvas, px, py, 3, 3, colour);
            #endif
        }
    }
}

struct VagueGrid {
    u8 cells[128][128];
    int side;
};

struct RandomWalker {
    int x;
    int y;
    int steps;
    Facing facing;
};

static void walk(VagueGrid* grid, RandomWalker* walkers, int count) {
    int offsets[4][2] = {
        { 0,-1 },
        { 0, 1 },
        { 1, 0 },
        {-1, 0 },
    };
    int mask = grid->side - 1;
    FOR_N (i, count) {
        RandomWalker* walker = walkers + i;
        if (walker->steps <= 0) {
            walker->facing = static_cast<Facing>(random::int_range(0, 3));
            walker->steps = random::int_range(4, 8);
        } else {
            walker->steps -= 1;
        }
        walker->x = (walker->x + offsets[walker->facing][0]) & mask;
        walker->y = (walker->y + offsets[walker->facing][1]) & mask;
        grid->cells[walker->y][walker->x] ^= 0xFF;
    }
}

static void trace_trochoid(VagueGrid* grid, Vector2* pen) {
    static double t = 0.0;
    t += 0.01;
    float theta = t;
    float R = 20.0f; // outer circle radius
    float r = 3.0f;  // inner circle radius
    float d = 28.0f; // distance
    float dr = R - r;
    float s = dr * theta / r;
    pen->x = dr * cos(theta) + d * cos(s);
    pen->y = dr * sin(theta) - d * sin(s);
    int x = pen->x + grid->side / 2;
    int y = pen->y + grid->side / 2;
    grid->cells[y][x] ^= 0xFF;
}

static void draw_vague_grid(Canvas* canvas, VagueGrid* grid, int cx, int cy) {
    FOR_N (i, grid->side) {
        FOR_N (j, grid->side) {
            u8 c = grid->cells[i][j];
            u32 colour = c << 16 | c << 8 | c;
            draw_rectangle(canvas, cx + j, cy + i, 1, 1, colour);
        }
    }
}

struct SmudgeBrush {
    u8 paint[9][9];
    Vector2 prior_point;
    int radius;
    int strength;
};

static void begin_smudge(VagueGrid* grid, SmudgeBrush* brush, Vector2 point,
                         int radius) {
    brush->radius = radius;
    int side = 2 * radius + 1;
    FOR_N (i, side) {
        FOR_N (j, side) {
            int x = point.x + (j - radius);
            int y = point.y + (i - radius);
            brush->paint[i][j] = grid->cells[y][x];
        }
    }
    brush->prior_point = point;
}

static void smudge_at_point(VagueGrid* grid, SmudgeBrush* brush, Vector2 point,
                            float strength) {
    int radius = brush->radius;
    int side = 2 * radius + 1;
    FOR_N (i, side) {
        FOR_N (j, side) {
            int u = j - radius;
            int v = i - radius;
            if ((u * u) + (v * v) <= (radius * radius)) {
                u8 b = brush->paint[i][j];
                int x = point.x + u;
                int y = point.y + v;
                u8 g = grid->cells[y][x];
                float value = lerp(b, g, strength);
                brush->paint[i][j] = value;
                grid->cells[y][x] = value;
            }
        }
    }
}

static void stroke_smudge(VagueGrid* grid, SmudgeBrush* brush, Vector2 center,
                          float strength) {
    Vector2 translation = center - brush->prior_point;
    float d = length(translation);
    if (d == 0.0f) {
        return; // if the brush hasn't moved
    }
    Vector2 v = translation / d;
    float spacing = d / brush->radius;
    FOR_N (i, spacing) {
        smudge_at_point(grid, brush, brush->prior_point + i * v, strength);
    }
    smudge_at_point(grid, brush, center, strength);
    brush->prior_point = center;
}

// Boids Functions.............................................................

struct Boid {
    Vector2 position;
    Vector2 velocity;
};

static bool within_circle(Vector2 point, Vector2 center, float radius) {
    Vector2 v = point - center;
    return square_length(v) <= (radius * radius);
}

static Vector2 cohere(Boid* boids, int count, int index, float factor,
                      float radius, float force_limit) {
    Vector2 percieved_center = vector2_zero;
    int neighbors = 0;
    FOR_N (i, count) {
        if (i != index &&
            within_circle(boids[i].position, boids[index].position, radius)) {
            percieved_center += boids[i].position;
            neighbors += 1;
        }
    }
    if (neighbors <= 0) {
        return vector2_zero;
    } else {
        percieved_center /= neighbors;
        Vector2 result = (percieved_center - boids[index].position) / factor;
        result = limit_length(result, force_limit);
        return result;
    }
}

static Vector2 separate(Boid* boids, int count, int index, float distance,
                        float force_limit) {
    Vector2 result = vector2_zero;
    float ds = distance * distance;
    FOR_N (i, count) {
        if (i != index) {
            Vector2 p = boids[i].position - boids[index].position;
            if (square_length(p) < ds) {
                result -= p / ds;
            }
        }
    }
    result = limit_length(result, force_limit);
    return result;
}

static Vector2 align(Boid* boids, int count, int index, float factor,
                     float radius, float force_limit) {
    Vector2 percieved_velocity = vector2_zero;
    int neighbors = 0;
    FOR_N (i, count) {
        if (i != index &&
            within_circle(boids[i].position, boids[index].position, radius)) {
            percieved_velocity += boids[i].velocity;
            neighbors += 1;
        }
    }
    if (neighbors <= 0) {
        return vector2_zero;
    } else {
        percieved_velocity /= neighbors;
        Vector2 result = (percieved_velocity - boids[index].velocity) / factor;
        result = limit_length(result, force_limit);
        return result;
    }
}

static Vector2 attract(Boid* boid, Vector2 goal, float factor, float radius) {
    Vector2 p = goal - boid->position;
    float distance = length(p);
    if (distance < radius) {
        return vector2_zero;
    } else {
        return p / (distance * factor);
    }
}

static void flock_the_boids(Boid* boids, int count, Vector2 goal) {
    FOR_N (i, count) {
        Boid* boid = boids + i;
        Vector2 v1 = cohere(boids, count, i,   200.0f, 20.0f, 2.0f);
        Vector2 v2 = separate(boids, count, i,         10.0f, 2.0f);
        Vector2 v3 = align(boids, count, i,    100.0f, 20.0f, 2.0f);
        Vector2 v4 = attract(boid, goal,       5.0f,  0.0f);
        boid->velocity += v1 + v2 + v3 + v4;
        boid->velocity = limit_length(boid->velocity, 3.0f);
        boid->position += boid->velocity;
    }
}

namespace game {

struct Entity {
    struct { int x, y; } center;
    struct { int x, y; } extents;
    struct { int x, y, width, height; } texcoord;
};

struct Player {
    AnimationState animation_state;
    struct { double x, y; } position;
};

static bool overlap_entity(Entity* entity, int x, int y) {
    return abs(x - entity->center.x) <= entity->extents.x
        && abs(y - entity->center.y) <= entity->extents.y;
}

static bool entities_overlap(Entity* a, Entity* b) {
    return abs(a->center.x - b->center.x) < a->extents.x + b->extents.x
        && abs(a->center.y - b->center.y) < a->extents.y + b->extents.y;
}

enum class Mode { Play, Edit, };

namespace {
    Entity entities[10];
    Atlas atlas;
    BmFont test_font;
    Atlas test_font_atlas;
    Atlas experiment_truchet_atlas;
    ani::Asset test_animations;
    audio::StreamId test_music;

    struct {
        Entity* entities[10];
        struct { int x, y; } offsets[10];
        int entities_count;
    } grabbed;

    struct {
        Entity* entities[10];
        int entities_count;
    } hovered;

    Player player;
    Mode mode = Mode::Play;
    bool show_profiling_overlay = false;
    bool show_fps_counter = true;
    int fps_counter;

    const int player_index = 0;

    perlin_noise::Source test_noise;
    ca::Grid test_grid;
    ca::CyclicPresetName test_preset = ca::CYCLIC_PRESET_SQUARISH_SPIRALS;
    ca::Grid test_grid_b;
    ca::LifePresetName test_preset_b = ca::LIFE_PRESET_BOMBERS;
    diamond_square::Lattice test_lattice;
    Colour cube_helix_map[256];

    VagueGrid experiment_walk_grid;
    const int experiment_walkers_count = 6;
    RandomWalker experiment_walkers[experiment_walkers_count];

    VagueGrid experiment_trochoid_grid;
    Vector2 experiment_trochoid_pen;
    SmudgeBrush experiment_smudge_brush;
    Vector2 experiment_smudge_pen;

    const int test_particle_count = 12;
    Particle test_particles[test_particle_count];

    const int experiment_boid_count = 30;
    Boid experiment_boids[experiment_boid_count];
}

void startup(Heap* heap, Stack* stack) {
    bm_font_load(&test_font, "droid_12.fnt", heap, stack);
    load_atlas(&test_font_atlas, test_font.image.filename, heap, stack);
    load_atlas(&atlas, "player.png", heap, stack);
    load_atlas(&experiment_truchet_atlas, "Quarter Circles.png", heap, stack);
    ani::load_asset(&test_animations, "player.ani", heap, stack);
    audio::start_stream("grass.ogg", 0.0f, &test_music);

    // test world loading
    {
        const char* wor_filename = "test.wor";
        // wor::save_chunk(wor_filename);
        wor::load_chunk(wor_filename, stack);
    }

    random::seed(time(nullptr));

    // random entities
    {
        int extents_x = 8;
        int extents_y = 8;
        FOR_N (i, ARRAY_COUNT(entities)) {
            Entity* entity = entities + i;
            entity->center.x = random::int_range(extents_x, 480 - extents_x);
            entity->center.y = random::int_range(extents_y, 270 - extents_y);
            entity->extents.x = extents_x;
            entity->extents.y = extents_y;
            entity->texcoord.x = 32;
            entity->texcoord.y = 32;
            entity->texcoord.width = 2 * extents_x;
            entity->texcoord.height = 2 * extents_y;
        }
    }

    // create test for perlin noise
    {
        FOR_N (y, 32) {
            FOR_N (x, 32) {
                double vx = random::double_range(-1.0, 1.0);
                double vy = random::double_range(-1.0, 1.0);
                perlin_noise::set_gradient(&test_noise, x, y, vx, vy);
            }
        }
    }

    // Initialise the random walker and grid.
    {
        int side = 128;
        experiment_walk_grid.side = side;
        FOR_N (i, experiment_walkers_count) {
            experiment_walkers[i].x = side / 2;
            experiment_walkers[i].y = side / 2;
        }
    }

    // Initialise the centered trochoid grid.
    {
        int side = 128;
        experiment_trochoid_grid.side = side;
        experiment_trochoid_grid.side = side;

        Vector2 s = {
            65.0f + 32.0f,
            65.0f
        };
        begin_smudge(&experiment_trochoid_grid, &experiment_smudge_brush, s, 4);
    }

    // Initialise the cellular automata grids.
    {
        {
            ca::CyclicPreset preset = ca::cyclic_presets[test_preset];
            ca::initialise(&test_grid, preset.states);
            ca::fill_with_randomness(&test_grid);
        }

        {
            ca::LifePreset preset = ca::life_presets[test_preset_b];
            ca::initialise(&test_grid_b, preset.states);
            ca::fill(&test_grid_b, preset.fill_style);
        }
    }

    // Initialise the diamond-square lattice.
    {
        test_lattice.side = 128;
        diamond_square::generate(&test_lattice, 1.0f);
    }

    {
        // default: 0.5f, 1.5f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f
        // perceptual rainbow: 1.5f, -1.3f, 1.0f, 2.5f, 0.3f, 0.8f, 0.9f
        // red15: 0.0f, 0.5
        // alt: 1.5f, -1.0f, 1.5f, 1.5f
        cube_helix(cube_helix_map, 256,
                   0.0f, 0.5,
                   1.0f, 1.0f,
                   0.0f, 1.0f, 1.0f);
    }

    // Initialise a chain of test particles.
    {
        int base_x = 160;
        int base_y = 50;
        FOR_N (i, test_particle_count) {
            Particle* particle = test_particles + i;
            particle->position.x = base_x;
            particle->position.y = base_y + i * 4;
            particle->prior_position = particle->position;
        }
    }

    // Initialise a random boid flock.
    {
        player.position.x = 300.0;
        player.position.y = 100.0;
        FOR_N (i, experiment_boid_count) {
            Boid* boid = experiment_boids + i;
            boid->position.x = random::float_range(250.0f, 350.0f);
            boid->position.y = random::float_range(50.0f, 150.0f);
        }
    }
}

void shutdown(Heap* heap) {
    unload_atlas(&experiment_truchet_atlas);
    unload_atlas(&atlas);
    unload_atlas(&test_font_atlas);
    ani::unload_asset(&test_animations, heap);
    bm_font_unload(&test_font, heap);
    audio::stop_stream(test_music);
}

void switch_mode(Mode to) {
    // Clean up the mode that is being switched from.
    switch (mode) {
        case Mode::Play: {
            reset(&player.animation_state);
            break;
        }
        case Mode::Edit: {
            grabbed.entities_count = 0;
            break;
        }
    }
    mode = to;
}

void update(Stack* stack) {
    PROFILE_SCOPED();

    struct { int x, y; } mouse;
    input::get_mouse_position(&mouse.x, &mouse.y);

    input::Controller* controller = input::get_controller();
    if (input::is_button_tapped(controller, input::USER_BUTTON_TAB)) {
        switch (mode) {
            case Mode::Play: { switch_mode(Mode::Edit); break; }
            case Mode::Edit: { switch_mode(Mode::Play); break; }
        }
    }

    switch (mode) {
        case Mode::Edit: {
            // entity grabbing
            if (input::get_mouse_clicked()) {
                FOR_N (i, ARRAY_COUNT(entities)) {
                    Entity* entity = entities + i;
                    if (overlap_entity(entity, mouse.x, mouse.y)) {
                        int j = grabbed.entities_count;
                        grabbed.entities[j] = entity;
                        grabbed.offsets[j].x = entity->center.x - mouse.x;
                        grabbed.offsets[j].y = entity->center.y - mouse.y;
                        grabbed.entities_count += 1;
                    }
                }
            } else if (!input::get_mouse_pressed()) {
                grabbed.entities_count = 0;
            }

            // Move all the entities that are grabbed.
            FOR_N (i, grabbed.entities_count) {
                Entity* entity = grabbed.entities[i];
                entity->center.x = mouse.x + grabbed.offsets[i].x;
                entity->center.y = mouse.y + grabbed.offsets[i].y;
            }

            // Track which entities are being hovered over.
            hovered.entities_count = 0;
            FOR_N (i, ARRAY_COUNT(entities)) {
                Entity* entity = entities + i;
                if (overlap_entity(entity, mouse.x, mouse.y)) {
                    int j = hovered.entities_count;
                    hovered.entities[j] = entity;
                    hovered.entities_count += 1;
                }
            }
            break;
        }
        case Mode::Play: {
            // Update the player movement state.

            double move_x = input::get_axis(controller, input::USER_AXIS_HORIZONTAL);
            double move_y = input::get_axis(controller, input::USER_AXIS_VERTICAL);
            player.position.x += 0.9 * move_x;
            player.position.y -= 0.9 * move_y;
            int x = player.position.x;
            int y = player.position.y;
            if (input::is_button_tapped(controller, input::USER_BUTTON_A)) {
                y -= 10;
                audio::play_once("Jump.wav", 0.5f);
            }

            // Update the animation state and choose the appropriate frame to draw.

            bool moving = !double_is_zero(move_x) || !double_is_zero(move_y);
            if (moving) {
                if (double_is_zero(move_x)) {
                    if (move_y < 0.0f) {
                        set_facing(&player.animation_state, FACING_SOUTH);
                    } else {
                        set_facing(&player.animation_state, FACING_NORTH);
                    }
                } else if (move_x > 0.0f) {
                    set_facing(&player.animation_state, FACING_EAST);
                } else {
                    set_facing(&player.animation_state, FACING_WEST);
                }
            }

            int animation_index = player.animation_state.facing;
            ani::Sequence* animation = test_animations.sequences + animation_index;
            ani::Frame* frame = animation->frames + player.animation_state.frame_index;

            if (!moving) {
                reset(&player.animation_state);
            } else {
                player.animation_state.ticks += 1;
                if (player.animation_state.ticks >= frame->ticks) {
                    player.animation_state.ticks = 0;
                    cycle_increment(&player.animation_state.frame_index, animation->frames_count);
                }
            }

            Entity* player_entity = entities + player_index;
            player_entity->center.x = x + frame->origin_x;
            player_entity->center.y = y + frame->origin_y;
            player_entity->texcoord.x = frame->x;
            player_entity->texcoord.y = frame->y;
            player_entity->texcoord.width = frame->width;
            player_entity->texcoord.height = frame->height;

            break;
        }
    }

    // particle chain test
    {
        // wiggle one end of the chain around
        {
            static float t = 0.0f;
            t += 0.02f;
            Particle* first = test_particles;
            // Move the end along a centered trochoid.
            float r = 32.0f + 4.0f * sin(10.0f * t);
            first->position.x = 160.0f + r * cos(t);
            first->position.y = 50.0f + r * sin(t);
        }

        simulate_chain(test_particles, test_particle_count, 4.0f, 0.94f);
    }

    // Test the random walker.
    walk(&experiment_walk_grid, experiment_walkers, experiment_walkers_count);

    // Test the vague grid.
    {
        trace_trochoid(&experiment_trochoid_grid, &experiment_trochoid_pen);

        // Smear in an elliptical shape.
        {
            static float time = 0.0f;
            time += 0.05f;
            experiment_smudge_pen.x = 65.0f + 32.0f * cos(time);
            experiment_smudge_pen.y = 65.0f + 16.0f * sin(time);
        }
    }

    // Test the boid flocking.
    {
        Vector2 target = { player.position.x, player.position.y };
        flock_the_boids(experiment_boids, experiment_boid_count, target);
    }

    // Test the cyclic cellular automaton.
    {
        if (input::is_button_tapped(controller, input::USER_BUTTON_A)) {
            ca::fill_with_randomness(&test_grid);
        }

        static int simulation_step = 0;
        cycle_increment(&simulation_step, 4);
        if (simulation_step == 0) {
            ca::CyclicPreset preset = ca::cyclic_presets[test_preset];
            ca::simulate_cyclic(&test_grid, preset.neighborhood, preset.range, preset.threshold);
        }
    }

    // Test the binary cellular automaton.
    {
        ca::LifePreset preset = ca::life_presets[test_preset_b];
        if (input::is_button_tapped(controller, input::USER_BUTTON_A)) {
            ca::fill(&test_grid_b, preset.fill_style);
        }
        ca::simulate_life(&test_grid_b, preset.survive, preset.survive_count, preset.born, preset.born_count);
    }
}

void draw(Canvas* canvas, Stack* stack) {
    PROFILE_SCOPED();

    canvas_fill(canvas, 0x000044);

    // random entities
    FOR_N (i, ARRAY_COUNT(entities)) {
        Entity* entity = entities + i;
        int x = entity->center.x - entity->extents.x;
        int y = entity->center.y - entity->extents.y;
        draw_image(canvas, &atlas, x, y,
                   entity->texcoord.x, entity->texcoord.y,
                   entity->texcoord.width, entity->texcoord.height);
    }

    #if 1
    // particle chain test
    {
        FOR_N (i, test_particle_count) {
            Particle* particle = test_particles + i;
            draw_circle(canvas, particle->position.x, particle->position.y, 2, 0xFFFF00);
        }

        // draw a bounding box
        {
            AABB box = compute_bounds(test_particles, test_particle_count, 2);
            int x = box.center.x - box.extents.x;
            int y = box.center.y - box.extents.y;
            int width = 2 * box.extents.x;
            int height = 2 * box.extents.y;
            draw_rectangle_outline(canvas, x, y, width, height, 0x00FFFF);
        }
    }
    #endif

    #if 1
    // noise test
    {
        double speed = 0.1;
        double scale = 1.0 / 16.0;
        int top = 128;
        int left = 128;
        int width = 128;
        int height = 128;

        #if 1
        simplex_noise::Source noise;
        simplex_noise::seed(&noise, 0);
        #endif
        {
            Entity* p = entities + player_index;
            int x_extent = width / 2;
            int y_extent = height / 2;
            int x_center = left + x_extent;
            int y_center = top + y_extent;
            if (abs(p->center.x - x_center) < p->extents.x + y_extent &&
                abs(p->center.y - y_center) < p->extents.y + y_extent) {
                double x = scale * (player.position.x - left);
                double y = scale * (player.position.y - top);
                int px = ceil(x);
                int py = ceil(y);
                set_gradient(&test_noise, x  , y  , (x  )-px, (y  )-py);
                set_gradient(&test_noise, x+1, y  , (x+1)-px, (y  )-py);
                set_gradient(&test_noise, x-1, y  , (x-1)-px, (y  )-py);
                set_gradient(&test_noise, x  , y+1, (x  )-px, (y+1)-py);
                set_gradient(&test_noise, x  , y-1, (x  )-px, (y-1)-py);
            }
        }

        static int z = 0;
        z += 1;
        FOR_N (y, 128) {
            FOR_N (x, 128) {
                #if 0
                double value = simplex_noise::generate3D(&noise, scale * x, scale * y, speed * scale * z);
                #else
                // double value = perlin_noise_3d(x, y, speed * z, 1, 1.0, scale);
                double value = perlin_noise::generate_2d(&test_noise, scale * x, scale * y);
                #endif

                #if 0
                u8 q = 127.5 * 0.5 * fmod(9.0 * value, 2.0) + 127.5;
                u32 colour = q << 16 | q << 8 | q;
                draw_rectangle(canvas, left + x, top + y, 1, 1, colour);
                #else
                value = 0.5 * value + 0.5;
                double cutoff = 0.4;
                if (value >= cutoff) {
                    value = (value - cutoff) * 1.0 / (1.0 - cutoff);
                    u8 q = 255.0 * value;
                    u32 colour = q << 16 | q << 8 | q;
                    draw_rectangle(canvas, left + x, top + y, 1, 1, colour);
                }
                #endif
            }
        }
    }
    #endif

    #if 0
    // Test wave interference.
    draw_wavy_tiles(canvas, &experiment_truchet_atlas, 256, 0);
    #endif

    #if 0
    // Test the random walker.
    draw_vague_grid(canvas, &experiment_walk_grid, 256, 0);
    #endif

    #if 1
    // Test the vague grid.
    {
        draw_vague_grid(canvas, &experiment_trochoid_grid, 256, 128);

        const float strength = 0.5f;
        stroke_smudge(&experiment_trochoid_grid, &experiment_smudge_brush,
                      experiment_smudge_pen, strength);
    }
    #endif

    #if 1
    // Draw the boid flocking.
    FOR_N (i, experiment_boid_count) {
        Boid* boid = experiment_boids + i;
        draw_circle(canvas, boid->position.x, boid->position.y, 2, 0xFF00FF);
    }
    #endif

    #if 0
    // Test the cyclic cellular automaton.
    draw_cellular_automaton(canvas, &test_grid, 256, 128);
    #endif

    #if 0
    // Test the binary cellular automaton.
    draw_cellular_automaton(canvas, &test_grid_b, 128, 0);
    #endif

    #if 0
    // test diamond-square lattice
    {
        static float tim = 0.0f;
        tim += 0.01f;
        #if 1
        draw_diamond_square(canvas, &test_lattice, 256, 0, 4.0f, tim, cube_helix_map, 256);
        #else
        for (int i = 0; i < 256; ++i) {
            u32 colour = fetch_colour(cube_helix_map, 256, i, 0, 256);
            draw_rectangle(canvas, 128 + i, 0, 1, 10, colour);
        }
        #endif
    }
    #endif

    // foreground text
    {
        draw_text(canvas, &test_font_atlas, &test_font, "well, obviously we will leave", 10, 100, stack);
        draw_text(canvas, &test_font_atlas, &test_font, "our earthly containers", 10, 110, stack);
    }

    #if 0
    // do the text overlap experiment
    {
        FOR_N (i, 11) {
            int x = 10 + 2 * i + random::int_range(0, 3);
            int y = 10 + 2 * i + random::int_range(0, 3);
            draw_text(canvas, &test_font_atlas,
                      &test_font, "weird dadaist", x, y, stack);
        }
    }
    #endif

    switch (mode) {
        case Mode::Edit: {
            // hovered entity outlines
            FOR_N (i, hovered.entities_count) {
                Entity* entity = hovered.entities[i];
                int x = entity->center.x - entity->extents.x;
                int y = entity->center.y - entity->extents.y;
                int width = 2 * entity->extents.x;
                int height = 2 * entity->extents.y;
                draw_rectangle_outline(canvas, x, y, width, height, 0x00FFFF);
            }

            // mouse cursor line
            struct { int x, y; } mouse;
            input::get_mouse_position(&mouse.x, &mouse.y);
            draw_line(canvas, 40, 40, mouse.x, mouse.y, 0xFFFFFF);
            break;
        }
    }

    if (show_profiling_overlay) {
        int graph_x = 15;
        int graph_y = 15;
        int graph_height = 32;
        int max_slices = 128;

        // Draw the graph background.

        int box_width = max_slices;
        draw_rectangle_transparent(canvas, graph_x, graph_y, box_width, graph_height, 0x8F000000);

        // an index into the "distinct colour table"
        const int starting_colour_index = 14;
        int colour_index = starting_colour_index;

        #if 0
        // Pull the profiling data and draw lines on the graph.
        FOR_N (i, max_slices - 1) {
            int x1 = graph_x + i;
            int x2 = x1 + 1;
            FOR_N (j, periods) {
                int y1 = 0;
                int y2 = 0;
                u32 colour = distinct_colour_table[colour_index];
                draw_line(canvas, x1, y1, x2, y2, colour);
                cycle_increment(&colour_index, ARRAY_COUNT(distinct_colour_table));
            }
            colour_index = starting_colour_index;
        }
        #endif
    }

    if (show_fps_counter) {
        char text[32];
        snprintf(text, sizeof text, "fps: %i", fps_counter);
        draw_text(canvas, &test_font_atlas, &test_font, text, 5, 0, stack);
    }
}

void update_fps(int count) {
    fps_counter = count;
}

} // namespace game
