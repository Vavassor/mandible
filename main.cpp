#include "sized_types.h"
#include "logging.h"
#include "input.h"
#include "audio.h"
#include "monitoring.h"
#include "game.h"
#include "draw.h"
#include "string_utilities.h"
#include "posix_errors.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <X11/X.h>
#include <X11/Xlib.h>

#include "gl_core_3_3.h"
#include "glx_extensions.h"

#include "gl_shader.h"

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

static bool canvas_create(Canvas* canvas, int width, int height) {
    canvas->width = width;
    canvas->height = height;
    canvas->pixels = ALLOCATE_ARRAY(u32, width * height);
    return canvas->pixels;
}

static void canvas_destroy(Canvas* canvas) {
    if (canvas->pixels) {
        DEALLOCATE(canvas->pixels);
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

    int total_longs = 0;
    for (int i = 0; i < icon_count; ++i) {
        total_longs += 2 + icons[i].width * icons[i].height;
    }

    // Pack the icons into a buffer that contains the dimensions of each icon,
    // width then height, followed by pixel data for width * height pixels.
    // Repeat this for every icon; the order is unimportant.

    unsigned long* icon_buffer;
    std::size_t total_size = sizeof(unsigned long) * total_longs;
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
                    total_longs);

    std::free(icon_buffer);
}

struct Mesh {
    GLuint buffers[2];
    GLuint vertex_array;
    GLsizei num_indices;
};

static void draw_mesh(Mesh* mesh) {
    glBindVertexArray(mesh->vertex_array);
    glDrawElements(GL_TRIANGLES, mesh->num_indices, GL_UNSIGNED_SHORT, nullptr);
}

static void destroy_mesh(Mesh* mesh) {
    glDeleteBuffers(ARRAY_COUNT(mesh->buffers), mesh->buffers);
    glDeleteVertexArrays(1, &mesh->vertex_array);
}

static void resize_framebuffer(GLuint framebuffer, GLuint target_texture, int width, int height, bool is_float = false) {
    glBindTexture(GL_TEXTURE_2D, target_texture);
    if (is_float) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);

    GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(ARRAY_COUNT(draw_buffers), draw_buffers);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("The framebuffer is incomplete.");
    }
}

static inline void cycle_increment(int* s, int n) {
    *s = (*s + 1) % n;
}

#if 0
// code saved for reference
static inline void cycle_decrement(int* s, int n) {
    *s = (*s + (n - 1)) % n;
}
#endif

static int handle_x_error(Display* display, XErrorEvent* event) {
    char text[128];
    XGetErrorText(display, event->error_code, text, sizeof text);
    LOG_ERROR("%s", text);
    return 0;
}

int main(int argc, char** argv) {
    const int canvas_width = 480;
    const int canvas_height = 270;
    const int pixel_scale = 3;
    const char* title = "mandible";
    const double frame_frequency = 1.0 / 60.0;
    const char* icon_names[] = { "Icon.png", };

    bool vertical_synchronization = true;
    bool show_monitoring_overlay = true;
    bool disable_ntsc_style_rendering = false;

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
    Mesh canvas_mesh;
    GLuint canvas_shader;
    GLuint pass1_shader;
    GLuint pass2_shader;
    GLuint pass3_shader;
    GLuint canvas_texture;
    GLuint ntsc_dot_crawl;
    union {
        struct {
            GLuint array[2];
        };
        struct {
            GLuint nearest;
            GLuint linear;
        };
    } samplers;
    GLuint framebuffers[3];
    GLuint target_textures[3];
    Clock clock;
    Canvas canvas;

    if (!register_posix_signal_handlers()) {
        LOG_ERROR("Was not able to set the POSIX signal handlers.");
        return EXIT_FAILURE;
    }

    XSetErrorHandler(handle_x_error);

    // Connect to the X server
    display = XOpenDisplay(nullptr);
    if (!display) {
        LOG_ERROR("Cannot connect to X server");
        return EXIT_FAILURE;
    }

    // The dimensions of the final canvas after up-scaling.

    int scaled_width = pixel_scale * canvas_width;
    int scaled_height = pixel_scale * canvas_height;

    int pass1_width = canvas_width;
    int pass1_height = canvas_height;

    int pass2_width = scaled_width;
    int pass2_height = canvas_height;

    int pass3_width = scaled_width;
    int pass3_height = scaled_height;

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
    window_attributes.event_mask = KeyPressMask | KeyReleaseMask |
                                   ButtonPressMask | ButtonReleaseMask |
                                   PointerMotionMask;
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

    if (ogl_LoadFunctions() == ogl_LOAD_FAILED) {
        LOG_ERROR("Failed to load opengl extensions.");
    }

    // Initialise global opengl values.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glDisable(GL_DEPTH_TEST);

    // Setup the canvas.

    canvas_create(&canvas, canvas_width, canvas_height);

    // Create the rectangle mesh for drawing the canvas.
    {
        glGenVertexArrays(1, &canvas_mesh.vertex_array);
        glBindVertexArray(canvas_mesh.vertex_array);

        glGenBuffers(ARRAY_COUNT(canvas_mesh.buffers), canvas_mesh.buffers);

        const int num_vertices = 4;
        const float vertices[16] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
        };

        canvas_mesh.num_indices = 6;
        const GLushort elements[6] = { 0, 3, 1, 1, 3, 2 };

        int vertex_size = (2 + 2) * sizeof(float);

        glBindBuffer(GL_ARRAY_BUFFER, canvas_mesh.buffers[0]);
        glBufferData(GL_ARRAY_BUFFER, num_vertices * vertex_size, vertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, vertex_size, 0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vertex_size, reinterpret_cast<GLvoid*>(sizeof(float) * 2));

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, canvas_mesh.buffers[1]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLushort) * canvas_mesh.num_indices, elements, GL_STATIC_DRAW);

        glBindVertexArray(0);
    }

    // shader uniform setup for samplers
    {
        canvas_shader = load_shader_program(nullptr, nullptr);
        pass1_shader = load_shader_program(nullptr, "Assets/Shaders/yiq.fs");
        pass2_shader = load_shader_program(nullptr, "Assets/Shaders/composite.fs");
        pass3_shader = load_shader_program(nullptr, "Assets/Shaders/fringing.fs");

        glUseProgram(canvas_shader);
        glUniform1i(glGetUniformLocation(canvas_shader, "texture"), 0);

        glUseProgram(pass1_shader);
        glUniform1i(glGetUniformLocation(pass1_shader, "texture"), 0);

        glUseProgram(pass2_shader);
        glUniform1i(glGetUniformLocation(pass2_shader, "texture"), 0);
        glUniform1i(glGetUniformLocation(pass2_shader, "dot_crawl_texture"), 1);

        glUseProgram(pass3_shader);
        glUniform1i(glGetUniformLocation(pass3_shader, "texture"), 0);
    }

    {
        glGenTextures(1, &ntsc_dot_crawl);
        glBindTexture(GL_TEXTURE_2D, ntsc_dot_crawl);
        float data[3 * 9] = {
            1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,
        };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 3, 3, 0, GL_RGB, GL_FLOAT, data);
    }

    glGenTextures(1, &canvas_texture);
    glBindTexture(GL_TEXTURE_2D, canvas_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas.width, canvas.height, 0,
                 GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);

    glGenSamplers(ARRAY_COUNT(samplers.array), samplers.array);

    glSamplerParameteri(samplers.nearest, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(samplers.nearest, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glSamplerParameteri(samplers.linear, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(samplers.linear, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindSampler(0, samplers.nearest);
    glBindSampler(1, samplers.nearest);

    // Initialise the framebuffers and their associated textures.
    {
        glGenTextures(ARRAY_COUNT(target_textures), target_textures);
        glGenFramebuffers(ARRAY_COUNT(framebuffers), framebuffers);
        resize_framebuffer(framebuffers[0], target_textures[0], pass1_width, pass1_height, true);
        resize_framebuffer(framebuffers[1], target_textures[1], pass2_width, pass2_height, true);
        resize_framebuffer(framebuffers[2], target_textures[2], pass3_width, pass3_height, true);
    }

    // Initialise any other resources needed before the main loop starts.
    monitoring::startup();
    input::startup();
    audio::startup();
    game::startup();

    // Enable Vertical Synchronisation.
    if (!have_ext_swap_control) {
        glXSwapIntervalEXT(display, window, 1);
    } else {
        vertical_synchronization = false;
    }

    LOG_DEBUG("vertical synchronization: %s",
              (vertical_synchronization) ? "true" : "false");

    initialise_clock(&clock);

    // Flush the connection to the display before starting the main loop.
    XSync(display, False);

    // frames-per-second
    struct {
        double total_time;
        int frame_count;
    } fps;

    bool quit = false;
    while (!quit) {
        // Record when the frame starts.
        double frame_start_time = get_time(&clock);

        BEGIN_MONITORING(rendering);

        // Push the last frame as soon as possible.

        const float identity_matrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };

        const float upside_down_matrix[16] = {
            1.0f,  0.0f, 0.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            0.0f,  0.0f, 1.0f, 0.0f,
            0.0f,  0.0f, 0.0f, 1.0f,
        };

        if (disable_ntsc_style_rendering) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glViewport(0, 0, scaled_width, scaled_height);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(canvas_shader);
            glUniformMatrix4fv(glGetUniformLocation(canvas_shader, "model_view_projection"), 1, GL_FALSE, upside_down_matrix);
            glBindTexture(GL_TEXTURE_2D, canvas_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, canvas.width, canvas.height, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, canvas.pixels);
            draw_mesh(&canvas_mesh);

        } else {
            static int frame_count = 0;
            cycle_increment(&frame_count, 3);

            // 1st pass
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[0]);
                glViewport(0, 0, pass1_width, pass1_height);
                const GLfloat clear_color[4] = { 0.0f, 1.0f, 1.0f, 1.0f };
                glClearBufferfv(GL_COLOR, 0, clear_color);
                glUseProgram(pass1_shader);
                glUniformMatrix4fv(glGetUniformLocation(pass1_shader, "model_view_projection"), 1, GL_FALSE, identity_matrix);
                glUniform2f(glGetUniformLocation(pass1_shader, "texture_size"), canvas.width, canvas.height);
                glBindTexture(GL_TEXTURE_2D, canvas_texture);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, canvas.width, canvas.height, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, canvas.pixels);
                draw_mesh(&canvas_mesh);
            }

            // 2nd pass
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
                glViewport(0, 0, pass2_width, pass2_height);
                const GLfloat clear_color[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
                glClearBufferfv(GL_COLOR, 0, clear_color);
                glUseProgram(pass2_shader);
                glUniformMatrix4fv(glGetUniformLocation(pass2_shader, "model_view_projection"), 1, GL_FALSE, identity_matrix);
                glUniform2f(glGetUniformLocation(pass2_shader, "texture_size"), pass1_width, pass1_height);
                glUniform2f(glGetUniformLocation(pass2_shader, "input_size"), pass1_width, pass1_height);
                glUniform2f(glGetUniformLocation(pass2_shader, "output_size"), pass2_width, pass2_height);
                glUniform1f(glGetUniformLocation(pass2_shader, "frame_count"), frame_count);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, ntsc_dot_crawl);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, target_textures[0]);
                draw_mesh(&canvas_mesh);
            }

            // 3rd pass
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[2]);
                glViewport(0, 0, pass3_width, pass3_height);
                const GLfloat clear_color[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
                glClearBufferfv(GL_COLOR, 0, clear_color);
                glUseProgram(pass3_shader);
                glUniformMatrix4fv(glGetUniformLocation(pass3_shader, "model_view_projection"), 1, GL_FALSE, identity_matrix);
                glUniform2f(glGetUniformLocation(pass3_shader, "texture_size"), pass2_width, pass2_height);
                glUniform2f(glGetUniformLocation(pass3_shader, "input_size"), pass2_width, pass2_height);
                glUniform2f(glGetUniformLocation(pass3_shader, "output_size"), pass3_width, pass3_height);
                glBindTexture(GL_TEXTURE_2D, target_textures[1]);
                draw_mesh(&canvas_mesh);
            }

            // final draw to the main framebuffer
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glViewport(0, 0, scaled_width, scaled_height);
                glClear(GL_COLOR_BUFFER_BIT);
                glUseProgram(canvas_shader);
                glUniformMatrix4fv(glGetUniformLocation(canvas_shader, "model_view_projection"), 1, GL_FALSE, upside_down_matrix);
                glBindTexture(GL_TEXTURE_2D, target_textures[2]);
                draw_mesh(&canvas_mesh);
            }
        }

        glXSwapBuffers(display, window);

        END_MONITORING(rendering);

        BEGIN_MONITORING(drawing);

        game::update_and_draw(&canvas);

        if (show_monitoring_overlay) {
            int graph_x = 10;
            int graph_y = 10;
            int graph_height = 32;
            int bar_width = 1;

            // Draw the graph background.

            int box_width = bar_width * monitoring::MAX_SLICES;
            draw_rectangle_transparent(&canvas, graph_x, graph_y,
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
                    draw_rectangle(&canvas, bar_x, graph_y, bar_width,
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
                            draw_rectangle(&canvas, bar_x, graph_y + y_bottom,
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

        // Since the monitoring data has been reported or ignored at this
        // point, tell the monitoring system to go ahead and move to the next
        // time slice.
        monitoring::complete_frame();

        END_MONITORING(drawing);

        input::poll();

        // Flush the events queue and respond to any pertinent events.
        while (XPending(display) > 0) {
            XEvent event = {};
            XNextEvent(display, &event);
            switch (event.type) {
                case KeyPress: {
                    XKeyEvent key_press = event.xkey;
                    KeySym keysym = XLookupKeysym(&key_press, 0);
                    input::on_key_press(keysym);
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
                        input::on_key_release(keysym);
                    }
                    break;
                }
                case ButtonPress: {
                    XButtonPressedEvent button_press = event.xbutton;
                    input::on_button_press(button_press.button);
                    break;
                }
                case ButtonRelease: {
                    XButtonReleasedEvent button_release = event.xbutton;
                    input::on_button_release(button_release.button);
                    break;
                }
                case MotionNotify: {
                    XMotionEvent motion = event.xmotion;
                    int x = motion.x / pixel_scale;
                    int y = motion.y / pixel_scale;
                    input::on_mouse_move(x, y);
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
    unload_pixmap(display, icccm_icon);

    // Shutdown all systems.
    game::shutdown();
    audio::shutdown();
    input::shutdown();
    monitoring::shutdown();

    // Free and destroy any system resources.
    canvas_destroy(&canvas);

    glDeleteTextures(ARRAY_COUNT(target_textures), target_textures);
    glDeleteFramebuffers(ARRAY_COUNT(framebuffers), framebuffers);
    glDeleteSamplers(ARRAY_COUNT(samplers.array), samplers.array);
    glDeleteTextures(1, &canvas_texture);
    glDeleteTextures(1, &ntsc_dot_crawl);
    glDeleteProgram(pass3_shader);
    glDeleteProgram(pass2_shader);
    glDeleteProgram(pass1_shader);
    glDeleteProgram(canvas_shader);
    destroy_mesh(&canvas_mesh);

    glXDestroyContext(display, rendering_context);
    XFree(wm_hints);
    XFree(size_hints);
    XFreeColormap(display, colormap);
    XCloseDisplay(display);

    return 0;
}
