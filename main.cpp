#include "sized_types.h"
#include "logging.h"
#include "snes_ntsc.h"
#include "input.h"
#include "audio.h"
#include "monitoring.h"
#include "font.h"
#include "string_utilities.h"
#include "unicode.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/glx.h>

#include "glx_extensions.h"

#include <time.h>

#include <cstdlib>
#include <cmath>

#if defined(__GNUC__)
#define RESTRICT __restrict__
#elif defined(_MSC_VER)
#define RESTRICT __declspec(restrict)
#endif

#define ARRAY_COUNT(a) \
    ((sizeof(a) / sizeof(*(a))) / static_cast<std::size_t>(!(sizeof(a) % sizeof(*(a)))))

#define FOR_N(index, n) \
    for (auto (index) = 0; (index) < (n); ++(index))

#define ALLOCATE_ARRAY(type, count) \
    static_cast<type*>(std::malloc(sizeof(type) * (count)))

#define ALLOCATE_STRUCT(type) \
    static_cast<type*>(std::malloc(sizeof(type)))

#define DEALLOCATE(a) \
    std::free(a)

struct Atlas {
    u8* data;
    int width;
    int height;
    int bytes_per_pixel;
};

static void load_atlas(Atlas* atlas, const char* name) {
    char path[256];
    copy_string(path, "Assets/", sizeof path);
    concatenate(path, name, sizeof path);
    atlas->data = stbi_load(path, &atlas->width, &atlas->height,
                            &atlas->bytes_per_pixel, 0);
}

static void unload_atlas(Atlas* atlas) {
    stbi_image_free(atlas->data);
}

struct Image {
    u8* data;
    int width;
    int height;
    int bytes_per_pixel;
};

static void load_image(Image* image, const char* name) {
    image->data = stbi_load(name, &image->width, &image->height,
                            &image->bytes_per_pixel, 0);
}

static void unload_image(Image* image) {
    stbi_image_free(image->data);
}

// Canvas Functions............................................................

struct Canvas {
    u16* buffer;
    int width;
    int height;
};

static bool canvas_create(Canvas* canvas, int width, int height) {
    canvas->width = width;
    canvas->height = height;
    canvas->buffer = ALLOCATE_ARRAY(u16, width * height);
    return canvas->buffer;
}

static void canvas_destroy(Canvas* canvas) {
    if (canvas->buffer) {
        DEALLOCATE(canvas->buffer);
    }
}

#define PACK16(x)        \
    (((x) >> 3 & 0x1F) | \
    ((x) >> 6 & 0x3E0) | \
    ((x) >> 9 & 0x7C00))

// @Incomplete: untested
#define UNPACK16(x)        \
    ((((x) & 0x1F) << 3) | \
    (((x) & 0x3E0) << 6) | \
    (((x) & 0x7C00) << 9))

static inline void set_pixel(Canvas* canvas, int x, int y, u16 value) {
    assert(x >= 0 && x < canvas->width);
    assert(y >= 0 && y < canvas->height);
    canvas->buffer[y * canvas->width + x] = value;
}

static void canvas_fill(Canvas* canvas, u32 colour) {
    int pixel_count = canvas->width * canvas->height;
    u16 colour16 = PACK16(colour);
    for (int i = 0; i < pixel_count; ++i) {
        canvas->buffer[i] = colour16;
    }
}

static int mod(int x, int m) {
    return (x % m + m) % m;
}

static void draw_rectangle(Canvas* canvas, Atlas* atlas, int cx, int cy,
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
            int ai = atlas_y * atlas->width + atlas_x;
            u32 c = reinterpret_cast<u32*>(atlas->data)[ai];
            if (c & 0xFF000000) {
                set_pixel(canvas, cx + x, cy + y, PACK16(c));
            }
        }
    }
}

static int clip_test(int q, int p, double* te, double* tl) {
    if (p == 0) {
        return q < 0;
    }
    double t = static_cast<double>(q) / p;
    if (p > 0) {
        if (t > *tl) {
            return 0;
        }
        if (t > *te) {
            *te = t;
        }
    } else {
        if (t < *te) {
            return 0;
        }
        if (t < *tl) {
            *tl = t;
        }
    }
    return 1;
}

static int sign(int x) {
    return (x > 0) - (x < 0);
}

static void draw_line(Canvas* canvas, int x1, int y1, int x2, int y2,
                      u32 colour) {

    // Clip the line to the canvas rectangle.
    // Uses the Liang–Barsky line clipping algorithm.
    {
        // the rectangle's boundaries
        int x_min = 0;
        int x_max = canvas->width - 1;
        int y_min = 0;
        int y_max = canvas->height - 1;

        // for the line segment (x1, y1) to (x2, y2), derive the parametric form
        // of its line:
        // x = x1 + t * (x2 - x1)
        // y = y1 + t * (y2 - y1)

        int dx = x2 - x1;
        int dy = y2 - y1;

        double te = 0.0; // entering
        double tl = 1.0; // leaving
        if (clip_test(x_min - x1,  dx, &te, &tl) &&
            clip_test(x1 - x_max, -dx, &te, &tl) &&
            clip_test(y_min - y1,  dy, &te, &tl) &&
            clip_test(y1 - y_max, -dy, &te, &tl)) {
            if (tl < 1.0) {
                x2 = static_cast<int>(static_cast<double>(x1) + tl * dx);
                y2 = static_cast<int>(static_cast<double>(y1) + tl * dy);
            }
            if (te > 0.0) {
                x1 += te * dx;
                y1 += te * dy;
            }
        }
    }

    // Draw the clipped line segment to the canvas.
    {
        u16 colour16 = PACK16(colour);
        int adx = std::abs(x2 - x1);  // absolute value of delta x
        int ady = std::abs(y2 - y1);  // absolute value of delta y
        int sdx = sign(x2 - x1); // sign of delta x
        int sdy = sign(y2 - y1); // sign of delta y
        int x = adx / 2;
        int y = ady / 2;
        int px = x1;             // plot x
        int py = y1;             // plot y

        set_pixel(canvas, px, py, colour16);

        if (adx >= ady) {
            for (int i = 0; i < adx; ++i) {
                y += ady;
                if (y >= adx) {
                    y -= adx;
                    py += sdy;
                }
                px += sdx;
                set_pixel(canvas, px, py, colour16);
            }
        } else {
            for (int i = 0; i < ady; ++i) {
                x += adx;
                if (x >= ady) {
                    x -= ady;
                    px += sdx;
                }
                py += sdy;
                set_pixel(canvas, px, py, colour16);
            }
        }
    }
}

// @Unused
#if 0
static void scale_whole_canvas(Canvas* to, Canvas* from, int scale) {
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

// Text-rendering functions....................................................

/*
Distinguishes characters which have no visible mark or glyph. This is as
opposed to "whitespace" characters, which includes the ogham space mark
conditionally displayed as a glyph depending on context.
*/
static bool is_character_non_displayable(char32_t codepoint) {
    switch (codepoint) {
        case 0x9: // character tabulation
        case 0xA: // line feed
        case 0xB: // line tabulation
        case 0xC: // form feed
        case 0xD: // carriage return
        case 0x20: // space
        case 0x85: // next line
        case 0xA0: // no-break space
        case 0x2000: // en quad
        case 0x2001: // em quad
        case 0x2002: // en space
        case 0x2003: // em space
        case 0x2004: // three-per-em space
        case 0x2005: // four-per-em space
        case 0x2006: // six-per-em space
        case 0x2007: // figure space
        case 0x2008: // punctuation space
        case 0x2009: // thin space
        case 0x200A: // hair space
        case 0x2028: // line separator
        case 0x2029: // paragraph separator
        case 0x202F: // narrow no-break space
        case 0x205F: // medium mathematical space
        case 0x3000: // ideographic space
            return true;
    }
    return false;
}

// @Incomplete: This doesn't fully handle Unicode line breaks. If this turns
// out to be insufficient, check the Lattice project for how to do more
// complete line-break handling.
static bool is_line_break(char32_t codepoint) {
    switch (codepoint) {
        case 0xA: // line feed
        case 0xC: // form feed
        case 0xD: // carriage return
        case 0x85: // next line
        case 0x2028: // line separator
        case 0x2029: // paragraph separator
            return true;
    }
    return false;
}

#define STACK_ALLOCATE_ARRAY(count, type) \
    static_cast<type*>(alloca(sizeof(type) * (count)))

static void draw_text(Canvas* canvas, Atlas* atlas, BmFont* font,
                      const char* text, int cx, int cy) {

    int char_count = string_size(text);
    int codepoint_count = utf8_codepoint_count(text);
    char32_t* codepoints = STACK_ALLOCATE_ARRAY(codepoint_count, char32_t);
    utf8_to_utf32(text, char_count, codepoints, codepoint_count);

    struct { int x, y; } pen;
    pen.x = cx;
    pen.y = cy;

    char32_t prior_char = 0x0;
    for (int i = 0; i < char_count; ++i) {
        char32_t c = text[i];
        BmFont::Glyph* glyph = bm_font_get_character_mapping(font, c);

        if (is_line_break(c)) {
            pen.x = cx;
            pen.y += font->leading;
        } else {
            pen.x += bm_font_get_kerning(font, prior_char, c);

            if (is_character_non_displayable(c)) {
                pen.x += glyph->x_advance;
            } else {
                int x = pen.x + glyph->x_offset;
                int y = pen.y + glyph->y_offset;
                int tx = glyph->texcoord.left;
                int ty = glyph->texcoord.top;
                int tw = glyph->texcoord.width;
                int th = glyph->texcoord.height;
                draw_rectangle(canvas, atlas, x, y, tx, ty, tw, th);
                pen.x += font->tracking + glyph->x_advance;
            }
        }

        prior_char = c;
    }
}

// Framebuffer Functions.......................................................

struct Framebuffer {
    u32* pixels;
    int width;
    int height;
};

static bool framebuffer_create(Framebuffer* framebuffer,
                               int width, int height) {
    framebuffer->width = width;
    framebuffer->height = height;
    framebuffer->pixels = ALLOCATE_ARRAY(u32, width * height);
    return framebuffer->pixels;
}

static void framebuffer_destroy(Framebuffer* framebuffer) {
    if (framebuffer->pixels) {
        DEALLOCATE(framebuffer->pixels);
    }
}

static void double_vertically_and_flip(Framebuffer *RESTRICT to,
                                       Framebuffer *RESTRICT from) {
    int w = from->width;
    int h = 2 * from->height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int ti = (h - y) * to->width + x;
            int fi = (y / 2) * from->width + x;
            to->pixels[ti] = from->pixels[fi];
        }
    }
}

// Clock Functions.............................................................

struct Clock {
    double frequency;
};

static void initialise_clock(Clock* clock) {
    timespec resolution;
    clock_getres(CLOCK_MONOTONIC, &resolution);
    s64 nanoseconds = resolution.tv_nsec + resolution.tv_sec * 1e9;
    clock->frequency = static_cast<double>(nanoseconds) / 1.0e9;
}

static double get_time(Clock* clock) {
    timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    s64 nanoseconds = timestamp.tv_nsec + timestamp.tv_sec * 1e9;
    return static_cast<double>(nanoseconds) * clock->frequency;
}

static void go_to_sleep(Clock* clock, double amount_to_sleep) {
    timespec requested_time;
    requested_time.tv_sec = 0;
    requested_time.tv_nsec = static_cast<s64>(1.0e9 * amount_to_sleep);
    clock_nanosleep(CLOCK_MONOTONIC, 0, &requested_time, nullptr);
}

// Icon Loading Functions......................................................

static void swap_red_and_blue_in_place(u32* pixels, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        pixels[i] = (pixels[i] & 0xFF00FF00) |
                    (pixels[i] & 0xFF0000) >> 16 |
                    (pixels[i] & 0xFF) << 16;
    }
}

static void swap_red_and_blue(unsigned long* out, u32* in, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        out[i] = (in[i] & 0xFF00FF00) |
                 (in[i] & 0xFF0000) >> 16 |
                 (in[i] & 0xFF) << 16;
    }
}

static bool load_pixmap(Pixmap* out_pixmap, Display* display,
                        const char* name) {
    int width;
    int height;
    int bytes_per_pixel;
    u8* pixel_data = stbi_load(name, &width, &height, &bytes_per_pixel, 0);
    if (!pixel_data) {
        return false;
    }

    swap_red_and_blue_in_place(reinterpret_cast<u32*>(pixel_data),
                               width * height);

    XImage* image;
    int depth = 8 * bytes_per_pixel;
    int bitmap_pad; // XCreateImage only accepts values of 8, 16, or 32
    switch (depth) {
        case 8:  bitmap_pad = 8;  break;
        case 16: bitmap_pad = 16; break;
        default: bitmap_pad = 32; break;
    }
    image = XCreateImage(display, CopyFromParent, depth, ZPixmap, 0,
                         reinterpret_cast<char*>(pixel_data),
                         width, height, bitmap_pad, 0);
    if (!image) {
        stbi_image_free(pixel_data);
        return false;
    }

    Pixmap pixmap = XCreatePixmap(display, DefaultRootWindow(display),
                                  width, height, depth);
    GC graphics_context = XCreateGC(display, pixmap, 0, nullptr);
    XPutImage(display, pixmap, graphics_context, image,
              0, 0, 0, 0, width, height);
    XFreeGC(display, graphics_context);

    // XDestroyImage actually deallocates not only the image but the pixel_data
    // memory passed into XCreateImage, so there's no need to call
    // stbi_image_free(pixel_data) and doing so would be "freeing freed
    // memory".
    XDestroyImage(image);

    *out_pixmap = pixmap;
    return true;
}

static void unload_pixmap(Display* display, Pixmap pixmap) {
    XFreePixmap(display, pixmap);
}

static void set_icons(Display* display, Window window, Atom net_wm_icon,
                      Atom cardinal, Image* icons, int icon_count) {

    int total_pixels = 0;
    for (int i = 0; i < icon_count; ++i) {
        total_pixels += 2 + icons[i].width * icons[i].height;
    }

    unsigned long* icon_buffer;
    std::size_t total_size = sizeof(unsigned long) * total_pixels;
    icon_buffer = static_cast<unsigned long*>(std::malloc(total_size));
    unsigned long* buffer = icon_buffer;
    for (int i = 0; i < icon_count; ++i) {
        *buffer++ = icons[i].width;
        *buffer++ = icons[i].height;
        int pixel_count = icons[i].width * icons[i].height;
        u32* data = reinterpret_cast<u32*>(icons[i].data);
        swap_red_and_blue(buffer, data, pixel_count);
        buffer += pixel_count;
    }

    // The buffer passed to XChangeProperty must be of type long when passing
    // a format value of 32, EVEN IF the size of a long is not 32-bits.
    XChangeProperty(display, window, net_wm_icon, cardinal, 32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(icon_buffer),
                    total_pixels);

    std::free(icon_buffer);
}

static int error_handler(Display* display, XErrorEvent* event) {
    char text[128];
    XGetErrorText(display, event->error_code, text, sizeof text);
    LOG_ERROR("%s", text);
    return 0;
}

int main(int argc, char* argv[]) {
    const int canvas_width = 256;
    const int canvas_height = 240;
    const char* title = "mandible";
    const double frame_frequency = 1.0 / 60.0;
    const char* icon_names[] = {
        "Icon.png",
    };

    bool vertical_synchronization = true;
    bool show_monitoring_overlay = false;

    Display* display; // the connection to the X server
    Colormap colormap;
    Window window;
    Atom wm_delete_window;
    Atom atom_utf8_string;
    Atom net_wm_name;
    Atom net_wm_icon_name;
    Atom net_wm_icon;
    Atom cardinal;
    XSizeHints* size_hints;
    XWMHints* wm_hints;
    Pixmap icccm_icon;
    GLXContext rendering_context;
    snes_ntsc_t* ntsc_scaler;
    input::System* input_system;
    audio::System* audio_system;
    Clock clock;
    Canvas canvas;
    Framebuffer wide_framebuffer;
    Framebuffer framebuffer;
    Atlas atlas;
    BmFont test_font;
    Atlas test_font_atlas;
    audio::StreamId test_music;

    XSetErrorHandler(error_handler);

    // Connect to the X server
    display = XOpenDisplay(nullptr);
    if (!display) {
        LOG_ERROR("Cannot connect to X server");
        return EXIT_FAILURE;
    }

    // The dimensions of the final canvas after up-scaling.

    int scaled_width = SNES_NTSC_OUT_WIDTH(canvas_width);
    int scaled_height = 2 * canvas_height;

    // Choose the abstract "Visual" type that will be used to describe both
    // the window and the OpenGL rendering context.

    GLint visual_attributes[] = {
        GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None
    };
    XVisualInfo* visual = glXChooseVisual(display, DefaultScreen(display),
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
    // space, tab, and line-feed.
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
                    string_size(title));
    XChangeProperty(display, window, net_wm_icon_name, atom_utf8_string,
                    8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title),
                    string_size(title));

    // Set the window icons.

    // Set the Pixmap for the ICCCM version of the application icon.
    bool icccm_icon_loaded = load_pixmap(&icccm_icon, display, icon_names[0]);
    if (!icccm_icon_loaded) {
        LOG_ERROR("Failed to load the ICCCM version of the application icon.");
    }
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
    FOR_N(i, ARRAY_COUNT(icons)) {
        load_image(icons + i, icon_names[i]);
    }
    set_icons(display, window, net_wm_icon, cardinal, icons, ARRAY_COUNT(icons));
    FOR_N(i, ARRAY_COUNT(icons)) {
        unload_image(icons + i);
    }

    // Make the window visible.
    XMapWindow(display, window);

    // Create the rendering context for OpenGL. The rendering context can only
    // be "made current" after the window is mapped (with XMapWindow).
    rendering_context = glXCreateContext(display, visual, nullptr, True);
    if (!rendering_context) {
        LOG_ERROR("Couldn't create a GLX rendering context.");
    }

    Bool made_current = glXMakeCurrent(display, window, rendering_context);
    if (!made_current) {
        LOG_ERROR("Failed to attach the GLX context to the window.");
    }

    load_glx_extensions(display, DefaultScreen(display));

    // Initialise the framebuffer.
    framebuffer_create(&framebuffer, scaled_width, scaled_height);
    framebuffer_create(&wide_framebuffer, scaled_width, canvas_height);

    // Create the scaler used to up-scale the canvas and simulate NTSC cable
    // colour-bleeding/artifacts.
    ntsc_scaler = ALLOCATE_STRUCT(snes_ntsc_t);
    snes_ntsc_init(ntsc_scaler, &snes_ntsc_composite);

    // Initialise any other resources needed before the main loop starts.
    monitoring::startup();
    input_system = input::startup();
    audio_system = audio::startup();

    initialise_clock(&clock);

    canvas_create(&canvas, canvas_width, canvas_height);

    load_atlas(&atlas, "player.png");
    audio::start_stream(audio_system, "grass.ogg", 1.0f, &test_music);

    bm_font_load(&test_font, "Assets/droid_12.fnt");
    load_atlas(&test_font_atlas, test_font.image.filename);

    // Enable Vertical Synchronisation.
    if (!have_ext_swap_control) {
        glXSwapIntervalEXT(display, window, 1);
    } else {
        vertical_synchronization = false;
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

        BEGIN_MONITORING(rendering);

        // Push the last frame as soon as possible.
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawPixels(framebuffer.width, framebuffer.height, GL_BGRA,
                     GL_UNSIGNED_INT_8_8_8_8_REV, framebuffer.pixels);
        glXSwapBuffers(display, window);

        END_MONITORING(rendering);

        BEGIN_MONITORING(drawing);

        // Then draw the next frame.
        canvas_fill(&canvas, 0x00ff00);

        {
            static float position_x = 0.0f;
            static float position_y = 0.0f;
            input::Controller* controller = input::get_controller(input_system);
            position_x += 0.9f * input::get_axis(controller, input::USER_AXIS_HORIZONTAL);
            position_y -= 0.9f * input::get_axis(controller, input::USER_AXIS_VERTICAL);
            int x = position_x;
            int y = position_y;
            if (input::is_button_tapped(controller, input::USER_BUTTON_A)) {
                y += 10;
                audio::play_once(audio_system, "Jump.wav", 0.5f);
            }
            draw_rectangle(&canvas, &atlas, x, y, 0, 0, 128, 128);

            draw_line(&canvas, x, y, 150, 150, 0xFFFFFF);
        }

        if (show_monitoring_overlay) {
            monitoring::lock();
            monitoring::sort_readings();
            int y = 0;
            const char* text;
            while ((text = monitoring::pull_reading())) {
                draw_text(&canvas, &test_font_atlas, &test_font, text, 0, y);
                y += 14;
            }
            monitoring::unlock();
        }
        monitoring::flush_readings();

        frame_flip ^= 1;
        snes_ntsc_blit(ntsc_scaler, reinterpret_cast<SNES_NTSC_IN_T*>(canvas.buffer),
                       canvas.width, frame_flip, canvas.width, canvas.height,
                       wide_framebuffer.pixels, 4 * wide_framebuffer.width);

        double_vertically_and_flip(&framebuffer, &wide_framebuffer);

        END_MONITORING(drawing);

        input::poll(input_system);

        // Flush the events queue and respond to any pertinent events.
        while (XPending(display) > 0) {
            XEvent event = {};
            XNextEvent(display, &event);
            switch (event.type) {
                case KeyPress: {
                    XKeyEvent key_press = event.xkey;
                    KeySym keysym = XLookupKeysym(&key_press, 0);
                    input::on_key_press(input_system, keysym);
                    break;
                }
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
                    break;
                }
                case ClientMessage: {
                    XClientMessageEvent client_message = event.xclient;
                    if (client_message.data.l[0] == wm_delete_window) {
                        XDestroyWindow(display, window);
                        quit = true;
                    }
                    break;
                }
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
    audio::stop_stream(audio_system, test_music);
    unload_atlas(&test_font_atlas);
    bm_font_unload(&test_font);
    unload_atlas(&atlas);
    unload_pixmap(display, icccm_icon);

    // Shutdown all systems.
    audio::shutdown(audio_system);
    input::shutdown(input_system);
    monitoring::shutdown();

    // Free and destroy any system resources.
    framebuffer_destroy(&framebuffer);
    framebuffer_destroy(&wide_framebuffer);
    canvas_destroy(&canvas);
    DEALLOCATE(ntsc_scaler);

    glXDestroyContext(display, rendering_context);
    XFree(wm_hints);
    XFree(size_hints);
    XFreeColormap(display, colormap);
    XCloseDisplay(display);

    return 0;
}
