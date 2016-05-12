#include "game.h"

#include "input.h"
#include "audio.h"
#include "stb_image.h"
#include "random.h"
#include "string_utilities.h"
#include "memory.h"
#include "logging.h"
#include "ani_file.h"

#include <cmath>
#include <cfloat>

#define ARRAY_COUNT(a) \
    ((sizeof(a) / sizeof(*(a))) / static_cast<std::size_t>(!(sizeof(a) % sizeof(*(a)))))

#define FOR_N(index, n) \
    for (auto (index) = 0; (index) < (n); ++(index))

static void load_atlas(Atlas* atlas, const char* name) {
    char path[256];
    copy_string(path, "Assets/", sizeof path);
    append_string(path, name, sizeof path);
    atlas->data = stbi_load(path, &atlas->width, &atlas->height,
                            &atlas->bytes_per_pixel, 0);
}

static void unload_atlas(Atlas* atlas) {
    stbi_image_free(atlas->data);
}

// @Incomplete: This needs to be checked for robustness.
// Also check the effects of /fp:fast (MSVC) and -funsafe-math-optimizations (GCC).
bool floats_equal(float x, float y) {
    float fx = std::abs(x);
    float fy = std::abs(y);
    float max_value = std::fmax(std::fmax(1.0f, fx), fy);
    float difference = std::abs(x - y);
    float epsilon = FLT_EPSILON * max_value;
    return std::isless(difference, epsilon);
}

bool float_is_zero(float x) {
    return floats_equal(x, 0.0f);
}

enum class Facing { North, South, East, West, };

struct AnimationState {
    Facing facing;
    int ticks;
    int frame_index;
};

static void set_facing(AnimationState* state, Facing facing) {
    if (state->facing != facing) {
        state->frame_index = 0;
        state->ticks = 0;
    }
    state->facing = facing;
}

static void cycle_increment(int* s, int n) {
    *s = (*s + 1) % n;
}

namespace game {

struct Entity {
    struct { int x, y; } position;
    struct { int x, y; } extents;
    struct { int x, y, width, height; } texcoord;
};

static bool overlap_entity(Entity* entity, int x, int y) {
    return std::abs(x - entity->position.x) <= entity->extents.x
        && std::abs(y - entity->position.y) <= entity->extents.y;
}

static bool entities_overlap(Entity* a, Entity* b) {
    return std::abs(a->position.x - b->position.x) < a->extents.x + b->extents.x
        && std::abs(a->position.y - b->position.y) < a->extents.y + b->extents.y;
}

namespace {
    Entity entities[10];
    Atlas atlas;
    BmFont test_font;
    Atlas test_font_atlas;
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

    enum class Mode {
        Play,
        Edit,
    } mode;

    const int player_index = 0;
}

void startup() {
    bm_font_load(&test_font, "Assets/droid_12.fnt");
    load_atlas(&test_font_atlas, test_font.image.filename);
    load_atlas(&atlas, "player.png");
    ani::load_asset(&test_animations, "Assets/player.ani");
    audio::start_stream("grass.ogg", 0.0f, &test_music);

    // random entities
    {
        int extents_x = 8;
        int extents_y = 8;
        random::seed(120);
        FOR_N(i, ARRAY_COUNT(entities)) {
            Entity* entity = entities + i;
            entity->position.x = random::int_range(extents_x, 480 - extents_x);
            entity->position.y = random::int_range(extents_y, 270 - extents_y);
            entity->extents.x = extents_x;
            entity->extents.y = extents_y;
            entity->texcoord.x = 32;
            entity->texcoord.y = 32;
            entity->texcoord.width = 2 * extents_x;
            entity->texcoord.height = 2 * extents_y;
        }
    }
}

void shutdown() {
    unload_atlas(&atlas);
    unload_atlas(&test_font_atlas);
    ani::unload_asset(&test_animations);
    bm_font_unload(&test_font);
    audio::stop_stream(test_music);
}

void update_and_draw(Canvas* canvas) {
    struct { int x, y; } mouse;
    input::get_mouse_position(&mouse.x, &mouse.y);

    switch (mode) {
        case Mode::Edit: {
            // entity grabbing
            if (input::get_mouse_clicked()) {
                FOR_N(i, ARRAY_COUNT(entities)) {
                    Entity* entity = entities + i;
                    if (overlap_entity(entity, mouse.x, mouse.y)) {
                        int j = grabbed.entities_count;
                        grabbed.entities[j] = entity;
                        grabbed.offsets[j].x = entity->position.x - mouse.x;
                        grabbed.offsets[j].y = entity->position.y - mouse.y;
                        grabbed.entities_count += 1;
                    }
                }
            } else if (!input::get_mouse_pressed()) {
                grabbed.entities_count = 0;
            }

            // Move all the entities that are grabbed.
            FOR_N(i, grabbed.entities_count) {
                Entity* entity = grabbed.entities[i];
                entity->position.x = mouse.x + grabbed.offsets[i].x;
                entity->position.y = mouse.y + grabbed.offsets[i].y;
            }

            // Track which entities are being hovered over.
            hovered.entities_count = 0;
            FOR_N(i, ARRAY_COUNT(entities)) {
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

            static float position_x = 40.0f;
            static float position_y = 50.0f;

            input::Controller* controller = input::get_controller();
            float move_x = input::get_axis(controller, input::USER_AXIS_HORIZONTAL);
            float move_y = input::get_axis(controller, input::USER_AXIS_VERTICAL);
            position_x += 0.9f * move_x;
            position_y -= 0.9f * move_y;
            int x = position_x;
            int y = position_y;
            if (input::is_button_tapped(controller, input::USER_BUTTON_A)) {
                y -= 10;
                audio::play_once("Jump.wav", 0.5f);
            }

            // Update the animation state and choose the appropriate frame to draw.

            static AnimationState animation_state = {};

            bool moving = !float_is_zero(move_x) || !float_is_zero(move_y);
            if (moving) {
                if (float_is_zero(move_x)) {
                    if (move_y < 0.0f) {
                        set_facing(&animation_state, Facing::South);
                    } else {
                        set_facing(&animation_state, Facing::North);
                    }
                } else if (move_x > 0.0f) {
                    set_facing(&animation_state, Facing::East);
                } else {
                    set_facing(&animation_state, Facing::West);
                }
            }

            int animation_index = 0;
            switch (animation_state.facing) {
                case Facing::South: animation_index = 0; break;
                case Facing::North: animation_index = 1; break;
                case Facing::West:  animation_index = 2; break;
                case Facing::East:  animation_index = 3; break;
            }

            ani::Sequence* animation = test_animations.sequences + animation_index;
            ani::Frame* frame = animation->frames + animation_state.frame_index;

            if (!moving) {
                animation_state.frame_index = 0;
                animation_state.ticks = 0;
            } else {
                animation_state.ticks += 1;
                if (animation_state.ticks >= frame->ticks) {
                    animation_state.ticks = 0;
                    cycle_increment(&animation_state.frame_index, animation->frames_count);
                }
            }

            Entity* player = entities + player_index;
            player->position.x = x;
            player->position.y = y;
            player->texcoord.x = frame->x;
            player->texcoord.y = frame->y;
            player->texcoord.width = frame->width;
            player->texcoord.height = frame->height;

            break;
        }
    }

    // Draw everything-----

    canvas_fill(canvas, 0x00FF00);

    // hovered entity outlines
    FOR_N(i, hovered.entities_count) {
        Entity* entity = hovered.entities[i];
        int x = entity->position.x - entity->extents.x;
        int y = entity->position.y - entity->extents.y;
        int width = 2 * entity->extents.x;
        int height = 2 * entity->extents.y;
        draw_rectangle_outline(canvas, x, y, width, height, 0x00FFFF);
    }

    // random entities
    FOR_N(i, ARRAY_COUNT(entities)) {
        Entity* entity = entities + i;
        int x = entity->position.x - entity->extents.x;
        int y = entity->position.y - entity->extents.y;
        draw_subimage(canvas, &atlas, x, y,
                      entity->texcoord.x, entity->texcoord.y,
                      entity->texcoord.width, entity->texcoord.height);
    }

    // foreground text
    {
        draw_text(canvas, &test_font_atlas, &test_font, "well, obviously we will leave", 10, 100);
        draw_text(canvas, &test_font_atlas, &test_font, "our earthly containers", 10, 110);
    }

    switch (mode) {
        case Mode::Edit: {
            // mouse cursor line
            draw_line(canvas, 40, 40, mouse.x, mouse.y, 0xFFFFFF);
            break;
        }
    }
}

} // namespace game
