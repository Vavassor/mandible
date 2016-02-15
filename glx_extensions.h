#ifndef GLX_EXTENSIONS_H_
#define GLX_EXTENSIONS_H_

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GLX_EXT_swap_control......................................................*/

extern bool have_ext_swap_control;

#define GLX_SWAP_INTERVAL_EXT     0x20F1
#define GLX_MAX_SWAP_INTERVAL_EXT 0x20F2

extern void (*ptrc_glXSwapIntervalEXT)(Display *display, GLXDrawable drawable, int interval);
#define glXSwapIntervalEXT ptrc_glXSwapIntervalEXT

/* Load GLX Extensions.......................................................*/

void load_glx_extensions(Display *display, int screen);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLX_EXTENSIONS_H_ */
