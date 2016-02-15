#include "sized_types.h"
#include "logging.h"
#include "snes_ntsc.h"
#include "input.h"
#include "audio.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/glx.h>

#include "glx_extensions.h"

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
};

static void double_vertically_and_flip(Framebuffer *to, Canvas *from) {
    int w = from->width;
    int h = 2 * from->height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int ti = 4 * ((h - y) * to->width + x);
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

static Pixmap load_pixmap(Display *display, Drawable drawable,
                          const char *name) {
    Pixmap pixmap;

    int width;
    int height;
    int bytes_per_pixel;
    u8 *pixel_data = stbi_load(name, &width, &height, &bytes_per_pixel, 0);

    swap_red_and_blue_in_place(reinterpret_cast<u32 *>(pixel_data), width * height);

    pixmap = XCreatePixmap(display, drawable, width, height, 32);
#if 0
    XImage *image = XCreateImage(display, CopyFromParent, 32, ZPixmap, 0,
                                 reinterpret_cast<char *>(pixel_data),
                                 width, height, 32, 0);
    GC graphics_context = XCreateGC(display, pixmap, 0, NULL);
    XPutImage(display, pixmap, graphics_context, image, 0, 0, 0, 0, width, height);
    XFreeGC(display, graphics_context);
    XDestroyImage(image);
#endif

    stbi_image_free(pixel_data);

    return pixmap;
}

static void unload_pixmap(Display *display, Pixmap pixmap) {
    XFreePixmap(display, pixmap);
}

static void set_icons(Display *display, Window window, Atom net_wm_icon,
                      Atom cardinal, Image *icons, int icon_count) {
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
    XChangeProperty(display, window, net_wm_icon, cardinal, 32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char *>(icon_buffer),
                    total_size);
    std::free(icon_buffer);
}

static int error_handler(Display *display, XErrorEvent *event) {
    char text[128];
    XGetErrorText(display, event->error_code, text, sizeof text);
    LOG_ERROR("%s", text);
    return 0;
}

int main(int argc, char *argv[]) {
    const int canvas_width = 256;
    const int canvas_height = 240;
    const char *title = "mandible";
    const double frame_frequency = 1.0 / 60.0;
    const char *icon_names[] = {
        "Icon.png",
    };

    bool vertical_synchronization = false;

    Display *display; // the connection to the X server
    int default_screen;
    Colormap colormap;
    Window window;
    Atom wm_delete_window;
    Atom atom_utf8_string;
    Atom net_wm_name;
    Atom net_wm_icon_name;
    Atom net_wm_icon;
    Atom cardinal;
    XSizeHints *size_hints;
    XWMHints *wm_hints;
    Pixmap icccm_icon;
    GLXContext rendering_context;
    snes_ntsc_t *ntsc_scaler;
    input::System *input_system;
    audio::System *audio_system;
    Clock clock;
    Canvas canvas;
    Canvas wide_canvas;
    Framebuffer framebuffer;

    XSetErrorHandler(error_handler);

    // Connect to the X server
    display = XOpenDisplay(NULL);
    if (!display) {
        LOG_ERROR("Cannot connect to X server");
        return EXIT_FAILURE;
    }
    default_screen = DefaultScreen(display);

    // The dimensions of the final canvas after up-scaling.

    int scaled_width = SNES_NTSC_OUT_WIDTH(canvas_width);
    int scaled_height = 2 * canvas_height;

    // Choose the abstract "Visual" type that will be used to describe both
    // the window and the OpenGL rendering context.

    GLint visual_attributes[] = {
        GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None
    };
    XVisualInfo *visual = glXChooseVisual(display, default_screen,
                                          visual_attributes);
    if (!visual) {
        LOG_ERROR("Wasn't able to choose an appropriate Visual type given the"
                  "requested attributes. [The Visual type contains information"
                  "on color mappings for the display hardware]");
    }

    // Create the window.
    colormap = XCreateColormap(display, DefaultRootWindow(display),
                               visual->visual, AllocNone);
    XSetWindowAttributes window_attributes = {};
    window_attributes.colormap = colormap;
    window_attributes.event_mask = KeyPressMask | KeyReleaseMask;
    window = XCreateWindow(display, DefaultRootWindow(display),
                           0, 0, scaled_width, scaled_height, 0, visual->depth,
                           InputOutput, visual->visual,
                           CWColormap | CWEventMask, &window_attributes);

    // Register to recieve window close messages.
    wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete_window, 1);

    // Make the window non-resizable.
    size_hints = XAllocSizeHints();
    if (!size_hints) {
        LOG_ERROR("Insufficient memory was available to allocate XSizeHints"
                  "which is used for making the window non-resizable.");
    }
    size_hints->min_width = scaled_width;
    size_hints->min_height = scaled_height;
    size_hints->max_width = scaled_width;
    size_hints->max_height = scaled_height;
    size_hints->flags = PMinSize | PMaxSize;
    XSetWMNormalHints(display, window, size_hints);

    // Set the window title.

    // Set the ICCCM version of the window name. This string is encoded with
    // the Host Portable Character Encoding which is ISO Latin-1 characters,
    // tab, and line-feed.
    XStoreName(display, window, title);
    XSetIconName(display, window, title);

    // Set the Extended Window Manager Hints version of the window name, which
    // overrides the previous if supported. This allows specifying a UTF-8
    // encoded name.
    net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    net_wm_icon_name = XInternAtom(display, "_NET_WM_ICON_NAME", False);
    atom_utf8_string = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display, window, net_wm_name, atom_utf8_string, 8,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title),
                    std::strlen(title));
    XChangeProperty(display, window, net_wm_icon_name, atom_utf8_string,
                    8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title),
                    std::strlen(title));

    // Set the window icons.

    // Set the Pixmap for the ICCCM version of the application icon.
    icccm_icon = load_pixmap(display, DefaultRootWindow(display),
                             icon_names[0]);
    wm_hints = XAllocWMHints();
    if (!wm_hints) {
        LOG_ERROR("Insufficient memory available to allocate the XWMHints"
                  "structure, which is needed for setting the ICCCM version"
                  "of the application icon.");
    }
    wm_hints->icon_pixmap = icccm_icon;
    wm_hints->flags = IconPixmapHint;
    XSetWMHints(display, window, wm_hints);

    // Set the Extended Window Manager Hints version of the icons, which will
    // override the basic icon if the window manager supports it. This allows
    // for several sizes of icon to be specified as well as full transparency
    // in icons.
    net_wm_icon = XInternAtom(display, "_NET_WM_ICON", False);
    cardinal = XInternAtom(display, "CARDINAL", False);
    Image icons[ARRAY_COUNT(icon_names)];
    for (std::size_t i = 0; i < ARRAY_COUNT(icons); ++i) {
        load_image(icons + i, icon_names[i]);
    }
    set_icons(display, window, net_wm_icon, cardinal, icons, ARRAY_COUNT(icons));
    for (std::size_t i = 0; i < ARRAY_COUNT(icons); ++i) {
        unload_image(icons + i);
    }

    // Make the window visible.
    XMapWindow(display, window);

    // Create the rendering context for OpenGL. The rendering context can only
    // be "made current" after the window is mapped (with XMapWindow).
    rendering_context = glXCreateContext(display, visual, NULL, True);
    if (!rendering_context) {
        LOG_ERROR("Couldn't create a GLX rendering context.");
    }

    Bool made_current = glXMakeCurrent(display, window, rendering_context);
    if (!made_current) {
        LOG_ERROR("Failed to attach the GLX context to the window.");
    }

    load_glx_extensions(display, default_screen);

    // Initialise the framebuffer.
    framebuffer.width = scaled_width;
    framebuffer.height = scaled_height;
    framebuffer.pixels = static_cast<u8 *>(std::malloc(4 * framebuffer.width * framebuffer.height));

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

    // Enable Vertical Synchronisation.
    if (!have_ext_swap_control) {
        glXSwapIntervalEXT(display, window, 1);
        vertical_synchronization = true;
    }

    LOG_DEBUG("vertical synchronization: %s",
              (vertical_synchronization) ? "true" : "false");

    // Flush the connection to the display before starting the main loop.
    XSync(display, False);

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
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawPixels(framebuffer.width, framebuffer.height, GL_BGRA,
                     GL_UNSIGNED_INT_8_8_8_8_REV, framebuffer.pixels);
        glXSwapBuffers(display, window);

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
                audio::play_once(audio_system, "Assets/Jump.wav");
            }
            draw_rectangle(&canvas, &atlas, x, y, 0, 0, 128, 128);
        }

        frame_flip ^= 1;
        snes_ntsc_blit(ntsc_scaler, reinterpret_cast<SNES_NTSC_IN_T *>(canvas.buffer),
                       canvas.width, frame_flip, canvas.width, canvas.height,
                       wide_canvas.buffer, 4 * wide_canvas.width);

        double_vertically_and_flip(&framebuffer, &wide_canvas);

        input::poll(input_system);

        // Flush the events queue and respond to any pertinent events.
        XEvent event = {};
        while (XPending(display) > 0) {
            XNextEvent(display, &event);
            switch (event.type) {
                case KeyPress: {
                    XKeyEvent key_press = event.xkey;
                    KeySym keysym = XLookupKeysym(&key_press, 0);
                    input::on_key_press(input_system, keysym);
                } break;

                case KeyRelease: {
                    XKeyEvent key_release = event.xkey;
                    bool auto_repeated = false;

                    // Examine the next event in the queue and if it's a
                    // key-press generated by auto-repeating, discard it and
                    // ignore this key release.

                    XEvent lookahead = {};
                    if (XPending(display) > 0 && XPeekEvent(display, &lookahead)) {
                        XKeyEvent next_press = lookahead.xkey;
                        if (key_release.time == next_press.time &&
                            key_release.keycode == next_press.keycode) {
                            // Remove the lookahead event.
                            XNextEvent(display, &lookahead);
                            auto_repeated = true;
                        }
                    }

                    if (!auto_repeated) {
                        KeySym keysym = XLookupKeysym(&key_release, 0);
                        input::on_key_release(input_system, keysym);
                    }
                } break;

                case ClientMessage: {
                    XClientMessageEvent client_message = event.xclient;
                    if (client_message.data.l[0] == wm_delete_window) {
                        XDestroyWindow(display, window);
                        quit = true;
                    }
                } break;
            }
        }

        // If the swap-buffer call isn't set to wait for the vertical retrace,
        // the remaining time needs to be waited off here until the next frame.

        if (!vertical_synchronization) {
            double frame_thusfar = get_time(&clock) - frame_start_time;
            if (frame_thusfar < frame_frequency) {
                go_to_sleep(&clock, frame_frequency - frame_thusfar);
            }
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
    unload_pixmap(display, icccm_icon);

    // Shutdown all systems.
    audio::shutdown(audio_system);
    input::shutdown(input_system);

    // Free and destroy any system resources.
    std::free(framebuffer.pixels);
    std::free(wide_canvas.buffer);
    std::free(canvas.buffer);
    std::free(ntsc_scaler);

    glXDestroyContext(display, rendering_context);
    XFree(wm_hints);
    XFree(size_hints);
    XFreeColormap(display, colormap);
    XCloseDisplay(display);

    return 0;
}
