#pragma once

#include "sized_types.h"
#include "font.h"

struct Canvas {
    u32* pixels;
    int width;
    int height;
};

struct Atlas {
    u8* data;
    int width;
    int height;
    int bytes_per_pixel;
};

void canvas_fill(Canvas* canvas, u32 colour);
void draw_subimage(Canvas* canvas, Atlas* atlas, int cx, int cy, int tx, int ty, int width, int height);
void draw_rectangle(Canvas* canvas, int cx, int cy, int width, int height, u32 colour);
void draw_rectangle_transparent(Canvas* canvas, int cx, int cy, int width, int height, u32 colour);
void draw_line(Canvas* canvas, int x1, int y1, int x2, int y2, u32 colour);
void draw_text(Canvas* canvas, Atlas* atlas, BmFont* font, const char* text, int cx, int cy);

extern const u32 distinct_colour_table[64];
