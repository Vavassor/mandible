#pragma once

struct Canvas;

namespace game {

void startup();
void shutdown();
void update_and_draw(Canvas* canvas);
void update_fps(int count);

} // namespace game
