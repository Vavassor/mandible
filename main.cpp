#include "sized_types.h"
#include "logging.h"
#include "snes_ntsc.h"
#include "input.h"
#include "audio.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/shm.h>

#include <sys/shm.h>

#include <time.h>

#include <cstdlib>
#include <cstring>
#include <cmath>

#define ARRAY_COUNT(a) \
    ((sizeof(a) / sizeof(*(a))) / static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))

struct Atlas {
    u8 *data;
    int width;
    int height;
    int bytes_per_pixel;
};

static void load_atlas(Atlas *atlas, const char *name) {
    atlas->data = stbi_load(name, &atlas->width, &atlas->height, &atlas->bytes_per_pixel, 0);
}

static void unload_atlas(Atlas *atlas) {
    stbi_image_free(atlas->data);
}

struct Image {
    u8 *data;
    int width;
    int height;
    int bytes_per_pixel;
};

static void load_image(Image *image, const char *name) {
    image->data = stbi_load(name, &image->width, &image->height,
                            &image->bytes_per_pixel, 0);
}

static void unload_image(Image *image) {
    stbi_image_free(image->data);
}

// Canvas Functions............................................................

struct Canvas {
    u8 *buffer;
    int width;
    int height;
};

static void canvas_fill(Canvas *canvas, u32 colour) {
    int pixel_count = canvas->width * canvas->height;
    u16 *buffer = reinterpret_cast<u16 *>(canvas->buffer);
    u16 colour16 = (colour >> 3 & 0x1F) |
                   (colour >> 6 & 0x3E0) |
                   (colour >> 9 & 0x7C00);
    for (int i = 0; i < pixel_count; ++i) {
        buffer[i] = colour16;
    }
}

static int mod(int x, int m) {
    return (x % m + m) % m;
}

static void draw_rectangle(Canvas *canvas, Atlas *atlas, int cx, int cy,
                           int tx, int ty, int width, int height) {
    if (cx < 0) {
        tx -= cx;
        width += cx;
        if (width > 0) {
            cx = 0;
        }
    }

    int extra_width = (cx + width) - canvas->width;
    if (extra_width > 0) {
        width -= extra_width;
    }

    if (cy < 0) {
        ty -= cy;
        height += cy;
        if (height > 0) {
            cy = 0;
        }
    }

    int extra_height = (cy + height) - canvas->height;
    if (extra_height > 0) {
        height -= extra_height;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int atlas_x = mod(tx + x, atlas->width);
            int atlas_y = mod(ty + y, atlas->height);
            int ai = (atlas_y * atlas->width + atlas_x) * 4;
            int ci = ((cy + y) * canvas->width + (cx + x)) * 2; // * 4
            if (atlas->data[ai+3]) {
                u16 *buffer = reinterpret_cast<u16 *>(canvas->buffer + ci);
                *buffer = (static_cast<u16>(atlas->data[ai+0]) >> 3 & 0x1F)
                        | (static_cast<u16>(atlas->data[ai+1]) << 2 & 0x3E0)
                        | (static_cast<u16>(atlas->data[ai+2]) << 7 & 0x7C00);
            }
        }
    }
}

// @Unused
#if 0
static void scale_whole_canvas(Canvas *to, Canvas* from, int scale) {
    int w = scale * from->width;
    int h = scale * from->height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int ti = 4 * (y * to->width + x);
            int fi = 4 * ((y / scale) * from->width + (x / scale));
            to->buffer[ti+0] = from->buffer[fi+0];
            to->buffer[ti+1] = from->buffer[fi+1];
            to->buffer[ti+2] = from->buffer[fi+2];
            to->buffer[ti+3] = from->buffer[fi+3];
        }
    }
}
#endif

// Framebuffer Functions.......................................................

struct Framebuffer {
    u8 *pixels;
    int width;
    int height;
    int shmid;
    xcb_shm_seg_t segment;
};

static void double_vertically(Framebuffer *to, Canvas *from) {
    int w = from->width;
    int h = 2 * from->height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int ti = 4 * (y * to->width + x);
            int fi = 4 * ((y / 2) * from->width + x);
            for (int i = 0; i < 4; ++i) {
                to->pixels[ti+i] = from->buffer[fi+i];
            }
        }
    }
}

// Clock Functions.............................................................

struct Clock {
    double frequency;
};

static void initialise_clock(Clock *clock) {
    timespec resolution;
    clock_getres(CLOCK_MONOTONIC, &resolution);
    s64 nanoseconds = resolution.tv_nsec + resolution.tv_sec * 1e9;
    clock->frequency = static_cast<double>(nanoseconds) / 1.0e9;
}

static double get_time(Clock *clock) {
    timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    s64 nanoseconds = timestamp.tv_nsec + timestamp.tv_sec * 1e9;
    return static_cast<double>(nanoseconds) * clock->frequency;
}

static void go_to_sleep(Clock *clock, double amount_to_sleep) {
    timespec requested_time;
    requested_time.tv_sec = 0;
    requested_time.tv_nsec = static_cast<s64>(1.0e9 * amount_to_sleep);
    clock_nanosleep(CLOCK_MONOTONIC, 0, &requested_time, NULL);
}

// Icon Loading Functions......................................................

static void swap_red_and_blue_in_place(u32 *pixels, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        pixels[i] = (pixels[i] & 0xFF00FF00) |
                    (pixels[i] & 0xFF0000) >> 16 |
                    (pixels[i] & 0xFF) << 16;
    }
}

#if defined(__GNUC__)
#define RESTRICT __restrict__
#elif defined(_MSC_VER)
#define RESTRICT __declspec(restrict)
#endif

static void swap_red_and_blue(u32 *out, const u32 *RESTRICT in, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        out[i] = (in[i] & 0xFF00FF00) |
                 (in[i] & 0xFF0000) >> 16 |
                 (in[i] & 0xFF) << 16;
    }
}

static xcb_pixmap_t load_pixmap(xcb_connection_t *connection,
                                xcb_drawable_t drawable,
                                xcb_gcontext_t graphics_context,
                                const char *name) {
    int width;
    int height;
    int bytes_per_pixel;
    u8 *pixel_data = stbi_load(name, &width, &height, &bytes_per_pixel, 0);

    swap_red_and_blue_in_place(reinterpret_cast<u32 *>(pixel_data), width * height);

    xcb_pixmap_t pixmap = xcb_generate_id(connection);
    xcb_create_pixmap(connection, 24, pixmap, drawable, width, height);
    xcb_put_image(connection, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap,
                  graphics_context, width, height, 0, 0, 0, 24,
                  4 * width * height, pixel_data);

    stbi_image_free(pixel_data);
    return pixmap;
}

static void unload_pixmap(xcb_connection_t *connection, xcb_pixmap_t pixmap) {
    xcb_free_pixmap(connection, pixmap);
}

static void set_icons(xcb_connection_t *connection, xcb_window_t window,
                      xcb_atom_t net_wm_icon, Image *icons, int icon_count) {
    int total_size = 0;
    for (int i = 0; i < icon_count; ++i) {
        total_size += 2 + icons[i].width * icons[i].height;
    }
    u32 *icon_buffer = static_cast<u32 *>(std::malloc(sizeof(u32) * total_size));
    u32 *buffer = icon_buffer;
    for (int i = 0; i < icon_count; ++i) {
        *buffer++ = icons[i].width;
        *buffer++ = icons[i].height;
        u32 *data = reinterpret_cast<u32 *>(icons[i].data);
        int pixel_count = icons[i].width * icons[i].height;
        swap_red_and_blue(buffer, data, pixel_count);
        buffer += pixel_count;
    }
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, net_wm_icon,
                        XCB_ATOM_CARDINAL, 32, total_size, icon_buffer);
    std::free(icon_buffer);
}

static const char *xcb_connection_error_description(int error) {
    switch (error) {
        case XCB_CONN_ERROR:                   return "Connection error because socket, pipe and other stream errors occurred.";
        case XCB_CONN_CLOSED_EXT_NOTSUPPORTED: return "Connection shutdown because extension was not supported.";
        case XCB_CONN_CLOSED_MEM_INSUFFICIENT: return "Connection closed because it ran out of memory.";
        case XCB_CONN_CLOSED_REQ_LEN_EXCEED:   return "Connection closed exceeding request length that the server accepts.";
        case XCB_CONN_CLOSED_PARSE_ERR:        return "Connection closed because an error occurred while parsing display string.";
        case XCB_CONN_CLOSED_INVALID_SCREEN:   return "Connection closed because the server does not have a screen matching the display.";
        case XCB_CONN_CLOSED_FDPASSING_FAILED: return "Connection closed because some FD passing operation failed.";
    }
    return "unknown error";
}

int main(int argc, char *argv[]) {
    const int canvas_width = 256;
    const int canvas_height = 240;
    const char *title = "mandible";
    const double frame_frequency = 1.0 / 60.0;
    const char *icon_names[] = {
        "Icon.png",
    };

    xcb_connection_t *connection;
    int default_screen_number;
    xcb_screen_t *default_screen;
    xcb_window_t window;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_atom_t atom_utf8_string;
    xcb_atom_t net_wm_name;
    xcb_atom_t net_wm_icon_name;
    xcb_atom_t net_wm_icon;
    xcb_gcontext_t graphics_context;
    xcb_pixmap_t icccm_icon;
    xcb_key_symbols_t *key_symbols;
    snes_ntsc_t *ntsc_scaler;
    input::System *input_system;
    audio::System *audio_system;
    Clock clock;
    Canvas canvas;
    Canvas wide_canvas;
    Framebuffer framebuffers[2];

    // Connect to the X server
    connection = xcb_connect(NULL, &default_screen_number);
    int connection_error;
    if ((connection_error = xcb_connection_has_error(connection))) {
        LOG_ERROR("xcb connection failure: %s",
                  xcb_connection_error_description(connection_error));
        xcb_disconnect(connection);
        return EXIT_FAILURE;
    }

    // Use the default screen number from the initial connection to find the
    // default screen (primary screen).
    default_screen = NULL;
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    for (int i = 0; iterator.rem; xcb_screen_next(&iterator), ++i) {
        if (i == default_screen_number) {
            default_screen = iterator.data;
        }
    }
    if (!default_screen) {
        LOG_ERROR("The default screen could not be found. "
                  "(looking for screen #%i)", default_screen_number);
        xcb_disconnect(connection);
        return EXIT_FAILURE;
    }

    // The dimensions of the final canvas after up-scaling.

    int scaled_width = SNES_NTSC_OUT_WIDTH(canvas_width);
    int scaled_height = 2 * canvas_height;

    // Create the window.
    window = xcb_generate_id(connection);
    u32 window_mask = XCB_CW_EVENT_MASK;
    u32 window_values[2] = {
        default_screen->white_pixel,
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_PROPERTY_CHANGE
    };
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window,
                      default_screen->root, 0, 0, scaled_width, scaled_height,
                      1, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      default_screen->root_visual, window_mask, window_values);

    // Make the window non-resizable.

    xcb_size_hints_t hints;
    xcb_icccm_size_hints_set_min_size(&hints, scaled_width, scaled_height);
    xcb_icccm_size_hints_set_max_size(&hints, scaled_width, scaled_height);
    xcb_icccm_set_wm_size_hints(connection, window, XCB_ATOM_WM_NORMAL_HINTS, &hints);

    // Load all atoms needed for further setup. These are for setting
    // various properties, telling the window manager what it needs to know.

    const char* atom_names[] = {
        "WM_PROTOCOLS",
        "WM_DELETE_WINDOW",
        "UTF8_STRING",
        "_NET_WM_NAME",
        "_NET_WM_ICON_NAME",
        "_NET_WM_ICON",
    };
    xcb_intern_atom_cookie_t cookies[ARRAY_COUNT(atom_names)];
    xcb_intern_atom_reply_t *replies[ARRAY_COUNT(atom_names)];
    for (std::size_t i = 0; i < ARRAY_COUNT(atom_names); ++i) {
        cookies[i] = xcb_intern_atom(connection, 1, std::strlen(atom_names[i]),
                                     atom_names[i]);
    }
    for (std::size_t i = 0; i < ARRAY_COUNT(atom_names); ++i) {
        replies[i] = xcb_intern_atom_reply(connection, cookies[i], NULL);
    }
    wm_protocols = replies[0]->atom;
    wm_delete_window = replies[1]->atom;
    atom_utf8_string = replies[2]->atom;
    net_wm_name = replies[3]->atom;
    net_wm_icon_name = replies[4]->atom;
    net_wm_icon = replies[5]->atom;

    // Register for window closing events (WM_DELETE_WINDOW).
    xcb_icccm_set_wm_protocols(connection, window, wm_protocols,
                               1, &wm_delete_window);

    // Set the window title.

    // Set the ICCCM version of the window name. The type atom STRING denotes
    // the string is encoded with only ISO Latin-1 characters, tab, and
    // line-feed.
    xcb_icccm_set_wm_name(connection, window, XCB_ATOM_STRING, 8,
                          std::strlen(title), title);
    xcb_icccm_set_wm_icon_name(connection, window, XCB_ATOM_STRING, 8,
                               std::strlen(title), title);

    // Set the Extended Window Manager Hints version of the window name, which
    // overrides the ICCCM one if it's supported. This allows specifying a
    // UTF-8 encoded title.
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
                        net_wm_name, atom_utf8_string,
                        8, std::strlen(title), title);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
                        net_wm_icon_name, atom_utf8_string,
                        8, std::strlen(title), title);

    // Create a graphics context to use for copying the canvas to the window
    // every frame.
    graphics_context = xcb_generate_id(connection);
    u32 gc_mask = XCB_GC_FOREGROUND;
    u32 gc_values[] = {
        default_screen->black_pixel
    };
    xcb_create_gc(connection, graphics_context, window, gc_mask, gc_values);

    // Set the window icons.

    // Set the ICCCM pixmap for the basic icon.
    icccm_icon = load_pixmap(connection, default_screen->root,
                             graphics_context, icon_names[0]);
    xcb_icccm_wm_hints_t wm_hints;
    xcb_icccm_wm_hints_set_icon_pixmap(&wm_hints, icccm_icon);
    xcb_icccm_set_wm_hints(connection, window, &wm_hints);

    // Set the Extended Window Manager Hints version of the icons, which will
    // override the ICCCM icon if the window manager supports it. This allows
    // for several sizes of icon to be specified as well as full transparency
    // in icons.

    Image icons[ARRAY_COUNT(icon_names)];
    for (std::size_t i = 0; i < ARRAY_COUNT(icons); ++i) {
        load_image(icons + i, icon_names[i]);
    }
    set_icons(connection, window, net_wm_icon, icons, ARRAY_COUNT(icons));
    for (std::size_t i = 0; i < ARRAY_COUNT(icons); ++i) {
        unload_image(icons + i);
    }

    // Make the window visible.
    xcb_map_window(connection, window);

    // Create the map needed for translating key codes.
    key_symbols = xcb_key_symbols_alloc(connection);

    xcb_shm_query_version_cookie_t shm_version_cookie = xcb_shm_query_version(connection);
    xcb_generic_error_t *error = NULL;
    xcb_shm_query_version_reply_t *shm_version_reply = xcb_shm_query_version_reply(connection, shm_version_cookie, &error);
    if (shm_version_reply->major_version == 0) {
        // @Incomplete: handle and log error
    }

    for (std::size_t i = 0; i < ARRAY_COUNT(framebuffers); ++i) {
        Framebuffer *f = framebuffers + i;
        f->width = scaled_width;
        f->height = scaled_height;

        f->shmid = shmget(IPC_PRIVATE, 4 * f->width * f->height, IPC_CREAT | 0777);
        if (f->shmid <= 0) {
            // @Incomplete: handle and log error
        }
        f->pixels = static_cast<u8 *>(shmat(f->shmid, 0, 0));
        if (f->pixels == static_cast<u8 *>(f->pixels)) {
            // @Incomplete: handle and log error
        }
        shmctl(f->shmid, IPC_RMID, NULL);

        f->segment = xcb_generate_id(connection);
        xcb_shm_attach(connection, f->segment, f->shmid, false);
    }

    // Create the scaler used to up-scale the canvas and simulate NTSC cable
    // colour-bleeding/artifacts.
    ntsc_scaler = static_cast<snes_ntsc_t *>(std::malloc(sizeof(snes_ntsc_t)));
    snes_ntsc_init(ntsc_scaler, &snes_ntsc_composite);

    // Initialise any other resources needed before the main loop starts.
    input_system = input::startup();
    audio_system = audio::startup();

    initialise_clock(&clock);

    canvas.width = canvas_width;
    canvas.height = canvas_height;
    canvas.buffer = static_cast<u8 *>(std::malloc(2 * canvas.width * canvas.height));

    wide_canvas.width = scaled_width;
    wide_canvas.height = canvas_height;
    wide_canvas.buffer = static_cast<u8 *>(std::malloc(4 * wide_canvas.width * wide_canvas.height));

    Atlas atlas;
    load_atlas(&atlas, "Assets/player.png");

    // Begin the main loop after flushing the connection.
    xcb_flush(connection);

    // frames-per-second
    struct {
        double total_time;
        int frame_count;
    } fps;

    int frame_flip = 0;

    bool quit = false;
    while (!quit) {
        // Record when the frame starts.
        double frame_start_time = get_time(&clock);

        // Push the last frame as soon as possible.
        Framebuffer *framebuffer = framebuffers + frame_flip;
        frame_flip ^= 1;
        xcb_shm_put_image(connection, window, graphics_context,
                          framebuffer->width, framebuffer->height,
                          0, 0, framebuffer->width, framebuffer->height,
                          0, 0, 24, XCB_IMAGE_FORMAT_Z_PIXMAP, 0,
                          framebuffer->segment, 0);
        xcb_flush(connection);

        // Then draw the next frame.
        canvas_fill(&canvas, 0x00ff00);

        {
            static float position_x = 0.0f;
            static float position_y = 0.0f;
            input::Controller *controller = input::get_controller(input_system);
            position_x += 0.9f * input::get_axis(controller, input::USER_AXIS_HORIZONTAL);
            position_y -= 0.9f * input::get_axis(controller, input::USER_AXIS_VERTICAL);
            int x = position_x;
            int y = position_y;
            if (input::is_button_tapped(controller, input::USER_BUTTON_A)) {
                y += 10;
            }
            draw_rectangle(&canvas, &atlas, x, y, 0, 0, 128, 128);
        }

        snes_ntsc_blit(ntsc_scaler, reinterpret_cast<SNES_NTSC_IN_T *>(canvas.buffer),
                       canvas.width, frame_flip, canvas.width, canvas.height,
                       wide_canvas.buffer, 4 * wide_canvas.width);

        framebuffer = framebuffers + frame_flip;
        double_vertically(framebuffer, &wide_canvas);

        input::poll(input_system);

        // Flush the events queue and respond to any pertinent events.
        xcb_generic_event_t *event;
        xcb_generic_event_t *next_event = NULL;
        while ((event = (next_event) ? next_event : xcb_poll_for_event(connection))) {
            next_event = NULL;
            switch (event->response_type & ~0x80) {
                case 0: {
                    xcb_generic_error_t *error = reinterpret_cast<xcb_generic_error_t *>(event);
                    LOG_ERROR("error code: %X", error->error_code);
                    quit = true;
                } break;

                case XCB_KEY_PRESS: {
                    xcb_key_press_event_t *key_press = reinterpret_cast<xcb_key_press_event_t *>(event);
                    xcb_keysym_t keysym = xcb_key_press_lookup_keysym(key_symbols, key_press, 0);
                    input::on_key_press(input_system, keysym);
                } break;

                case XCB_KEY_RELEASE: {
                    xcb_key_release_event_t *key_release = reinterpret_cast<xcb_key_release_event_t *>(event);
                    bool auto_repeated = false;

                    // Examine the next event in the queue and if it's a
                    // key-press generated by auto-repeating, discard it and
                    // ignore this key release.

                    xcb_generic_event_t *lookahead = xcb_poll_for_event(connection);
                    if (lookahead && (lookahead->response_type & ~0x80) == XCB_KEY_PRESS) {
                        xcb_key_press_event_t *next_press = reinterpret_cast<xcb_key_press_event_t *>(lookahead);
                        if (key_release->time == next_press->time &&
                            key_release->detail == next_press->detail) {
                            auto_repeated = true;
                        }
                    }

                    if (!auto_repeated) {
                        xcb_keysym_t keysym = xcb_key_release_lookup_keysym(key_symbols, key_release, 0);
                        input::on_key_release(input_system, keysym);

                        // Since the key release was not generated for
                        // auto-repeating purposes, the event removed for
                        // looking ahead was also genuine and needs to be saved
                        // so that it can be processed normally next cycle.
                        next_event = lookahead;
                    }
                } break;

                case XCB_MAPPING_NOTIFY: {
                    xcb_mapping_notify_event_t *mapping_notify = reinterpret_cast<xcb_mapping_notify_event_t *>(event);
                    xcb_refresh_keyboard_mapping(key_symbols, mapping_notify);
                } break;

                case XCB_CLIENT_MESSAGE: {
                    xcb_client_message_event_t *client_message = reinterpret_cast<xcb_client_message_event_t *>(event);
                    if (client_message->data.data32[0] == wm_delete_window) {
                        xcb_destroy_window(connection, client_message->window);
                        quit = true;
                    }
                } break;
            }
            std::free(event);
        }

        // Sleep off the remaining time until the next frame.

        double frame_thusfar = get_time(&clock) - frame_start_time;
        if (frame_thusfar < frame_frequency) {
            go_to_sleep(&clock, frame_frequency - frame_thusfar);
        }

        // Update the frames_per_second counter.

        double frame_end_time = get_time(&clock);
        fps.total_time += frame_end_time - frame_start_time;
        fps.frame_count += 1;
        if (fps.total_time >= 1.0) {
            LOG_DEBUG("fps: %i", fps.frame_count);
            fps.total_time = 0.0;
            fps.frame_count = 0;
        }
    }

    // Unload all assets.
    unload_atlas(&atlas);
    unload_pixmap(connection, icccm_icon);

    // Free the memory used by the key map.
    xcb_key_symbols_free(key_symbols);

    // Shutdown all systems.
    audio::shutdown(audio_system);
    input::shutdown(input_system);

    // Free and destroy any system resources.
    for (std::size_t i = 0; i < ARRAY_COUNT(framebuffers); ++i) {
        xcb_shm_detach(connection, framebuffers[i].segment);
        shmdt(framebuffers[i].pixels);
    }
    std::free(wide_canvas.buffer);
    std::free(canvas.buffer);
    std::free(ntsc_scaler);
    xcb_free_gc(connection, graphics_context);
    xcb_disconnect(connection);

    return 0;
}
