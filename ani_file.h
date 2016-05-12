#pragma once

namespace ani {

struct Frame {
    int x, y, width, height;
    int origin_x, origin_y;
    int ticks;
};

struct Sequence {
    char* name;
    Frame* frames;
    int frames_count;
};

struct Asset {
    Sequence* sequences;
    int sequences_count;
};

bool load_asset(Asset* asset, const char* filename);
void unload_asset(Asset* asset);
bool save_asset(Asset* asset, const char* filename);

} // namespace ani
