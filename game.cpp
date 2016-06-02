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
#include "monitoring.h"
#include "ani_file.h"
#include "wld_file.h"

#include <cmath>
#include <cfloat>
#include <cstdio>

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
bool doubles_equal(double x, double y) {
    double fx = std::abs(x);
    double fy = std::abs(y);
    double max_value = std::fmax(std::fmax(1.0, fx), fy);
    double difference = std::abs(x - y);
    double epsilon = DBL_EPSILON * max_value;
    return std::isless(difference, epsilon);
}

bool double_is_zero(double x) {
    return doubles_equal(x, 0.0);
}

enum class Facing { North, South, East, West, };

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
    return std::abs(x - entity->center.x) <= entity->extents.x
        && std::abs(y - entity->center.y) <= entity->extents.y;
}

static bool entities_overlap(Entity* a, Entity* b) {
    return std::abs(a->center.x - b->center.x) < a->extents.x + b->extents.x
        && std::abs(a->center.y - b->center.y) < a->extents.y + b->extents.y;
}

enum class Mode { Play, Edit, };

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

    Player player;
    Mode mode = Mode::Play;
    bool show_monitoring_overlay = false;
    bool show_fps_counter = true;
    int fps_counter;

    const int player_index = 0;
}

void startup() {
    bm_font_load(&test_font, "Assets/droid_12.fnt");
    load_atlas(&test_font_atlas, test_font.image.filename);
    load_atlas(&atlas, "player.png");
    ani::load_asset(&test_animations, "Assets/player.ani");
    audio::start_stream("grass.ogg", 0.0f, &test_music);

    const char* wld_filename = "Assets/test.wld";
    wld::save_chunk(wld_filename);
    wld::load_chunk(wld_filename);

    // random entities
    {
        int extents_x = 8;
        int extents_y = 8;
        random::seed(120);
        FOR_N(i, ARRAY_COUNT(entities)) {
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
}

void shutdown() {
    unload_atlas(&atlas);
    unload_atlas(&test_font_atlas);
    ani::unload_asset(&test_animations);
    bm_font_unload(&test_font);
    audio::stop_stream(test_music);
}

void switch_mode(Mode to) {
    // Cleanup the mode that is being switched from.
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

void update_and_draw(Canvas* canvas) {
    struct { int x, y; } mouse;
    input::get_mouse_position(&mouse.x, &mouse.y);

    input::Controller* controller = input::get_controller();
    if (input::is_button_tapped(controller, input::USER_BUTTON_TAB)) {
             if (mode == Mode::Play) { switch_mode(Mode::Edit); }
        else if (mode == Mode::Edit) { switch_mode(Mode::Play); }
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
                        set_facing(&player.animation_state, Facing::South);
                    } else {
                        set_facing(&player.animation_state, Facing::North);
                    }
                } else if (move_x > 0.0f) {
                    set_facing(&player.animation_state, Facing::East);
                } else {
                    set_facing(&player.animation_state, Facing::West);
                }
            }

            int animation_index = 0;
            switch (player.animation_state.facing) {
                case Facing::South: animation_index = 0; break;
                case Facing::North: animation_index = 1; break;
                case Facing::West:  animation_index = 2; break;
                case Facing::East:  animation_index = 3; break;
            }

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

    // Draw everything-----

    canvas_fill(canvas, 0x00FF00);

    // random entities
    FOR_N (i, ARRAY_COUNT(entities)) {
        Entity* entity = entities + i;
        int x = entity->center.x - entity->extents.x;
        int y = entity->center.y - entity->extents.y;
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
            draw_line(canvas, 40, 40, mouse.x, mouse.y, 0xFFFFFF);
            break;
        }
    }

    if (show_monitoring_overlay) {
        int graph_x = 15;
        int graph_y = 15;
        int graph_height = 32;
        int bar_width = 1;

        // Draw the graph background.

        int box_width = bar_width * monitoring::MAX_SLICES;
        draw_rectangle_transparent(canvas, graph_x, graph_y,
                                   box_width, graph_height, 0x8F000000);

        // These variables relate to how much of a bar to fill for a
        // particular reading.
        double nanoseconds_per_pixel = 5.0e6;
        double base = 0.0;
        double filled = 0.0;

        // an index into the "distinct colour table"
        const int starting_colour_index = 14;
        int colour_index = starting_colour_index;

        // Pull the monitoring data and draw bars on the graph.

        monitoring::lock();
        monitoring::Chart* chart = monitoring::get_chart();
        FOR_N(i, monitoring::MAX_SLICES) {
            int bar_x = graph_x + bar_width * i;

            if (i == chart->current_slice) {
                // The current slice is always going to have empty or old
                // information, so a timer marker is drawn in its place.
                draw_rectangle(canvas, bar_x, graph_y, bar_width,
                               graph_height, 0xFF00FFFF);
            } else {
                // Fill the current slice with a striped bar of colours,
                // where the colours denote which readings contributes to
                // that much of the bar.

                monitoring::Chart::Slice* slice = chart->slices + i;
                FOR_N(j, slice->total_readings) {
                    monitoring::Reading* reading = slice->readings + j;

                    filled += static_cast<double>(reading->elapsed_total) /
                              nanoseconds_per_pixel;

                    if (filled - base >= 1) {
                        int y_bottom = base;
                        int y_top = filled;
                        int bar_height = y_top - y_bottom;
                        u32 colour = distinct_colour_table[colour_index];
                        draw_rectangle(canvas, bar_x, graph_y + y_bottom,
                                       bar_width, bar_height, colour);
                        base = filled;
                    }

                    cycle_increment(&colour_index, ARRAY_COUNT(distinct_colour_table));
                }
            }

            base = 0.0;
            filled = 0.0;
            colour_index = starting_colour_index;
        }
        monitoring::unlock();
    }

    if (show_fps_counter) {
        char text[32];
        std::snprintf(text, sizeof text, "fps: %i", fps_counter);
        draw_text(canvas, &test_font_atlas, &test_font, text, 5, 0);
    }
}

void update_fps(int count) {
    fps_counter = count;
}

} // namespace game
