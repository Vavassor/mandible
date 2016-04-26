#include "game.h"

#include "input.h"
#include "audio.h"
#include "stb_image.h"
#include "random.h"
#include "string_utilities.h"

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

namespace game {

struct Entity {
    struct {
        int x, y;
    } position;

    struct {
        int x, y, width, height;
    } texcoord;
};

namespace {
    Entity entities[10];
    Atlas atlas;
    BmFont test_font;
    Atlas test_font_atlas;
    audio::StreamId test_music;
}

void startup() {
    bm_font_load(&test_font, "Assets/droid_12.fnt");
    load_atlas(&test_font_atlas, test_font.image.filename);
    load_atlas(&atlas, "player.png");
    audio::start_stream("grass.ogg", 1.0f, &test_music);
}

void shutdown() {
    unload_atlas(&atlas);
    unload_atlas(&test_font_atlas);
    bm_font_unload(&test_font);
    audio::stop_stream(test_music);
}

void update_and_draw(Canvas* canvas) {
    canvas_fill(canvas, 0x00FF00);

    // random entities
    {
        random::seed(120);
        for(int i = 0; i < 10; ++i) {
            Entity* entity = entities + i;
            entity->position.x = random::int_range(0, 480 - 16);
            entity->position.y = random::int_range(0, 270 - 16);
            entity->texcoord.x = 32;
            entity->texcoord.y = 32;
            entity->texcoord.width = 16;
            entity->texcoord.height = 16;
            draw_subimage(canvas, &atlas, entity->position.x, entity->position.y,
                          entity->texcoord.x, entity->texcoord.y,
                          entity->texcoord.width, entity->texcoord.height);
        }
    }

    // player
    {
        static float position_x = 0.0f;
        static float position_y = 0.0f;
        input::Controller* controller = input::get_controller();
        position_x += 0.9f * input::get_axis(controller, input::USER_AXIS_HORIZONTAL);
        position_y -= 0.9f * input::get_axis(controller, input::USER_AXIS_VERTICAL);
        int x = position_x;
        int y = position_y;
        if (input::is_button_tapped(controller, input::USER_BUTTON_A)) {
            y -= 10;
            audio::play_once("Jump.wav", 0.5f);
        }
        draw_subimage(canvas, &atlas, x, y, 0, 0, 16, 16);
    }

    // foreground text
    {
        draw_text(canvas, &test_font_atlas, &test_font, "well, obviously we will leave", 10, 100);
        draw_text(canvas, &test_font_atlas, &test_font, "our earthly containers", 10, 110);
    }
}

} // namespace game
