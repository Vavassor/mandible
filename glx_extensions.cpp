#include "glx_extensions.h"

#include <cstring>

// Internal Get Procedure Address
#define IntGetProcAddress(name) \
    glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name))

// GLX_EXT_swap_control........................................................

bool have_ext_swap_control;

void (*ptrc_glXSwapIntervalEXT)(Display*, GLXDrawable, int) = nullptr;

static int load_ext_swap_control() {
    int failed = 0;
    ptrc_glXSwapIntervalEXT = (void (*)(Display*, GLXDrawable, int)) IntGetProcAddress("glXSwapIntervalEXT");
    if (!ptrc_glXSwapIntervalEXT) { ++failed; }
    return failed;
}

// Extension-Loading Map.......................................................

typedef int (*ExtensionLoadCall)();

struct ExtensionMapping {
    const char* name;
    ExtensionLoadCall load_extension;
    bool* loaded;
};

#define EXTENSION_MAP_COUNT 1

static ExtensionMapping extension_map[EXTENSION_MAP_COUNT] = {
    { "GLX_EXT_swap_control", load_ext_swap_control, &have_ext_swap_control },
};

// Load GLX Extensions.........................................................

static void clear_extension_variables() {
    have_ext_swap_control = false;
}

void load_glx_extensions(Display* display, int screen) {
    clear_extension_variables();

    const char* extensions_string = glXQueryExtensionsString(display, screen);
    for (int i = 0; i < EXTENSION_MAP_COUNT; ++i) {
        if (std::strstr(extensions_string, extension_map[i].name) &&
            extension_map[i].load_extension) {
            int failed = extension_map[i].load_extension();
            if (failed > 0) {
                *extension_map[i].loaded = true;
            }
        }
    }
}
