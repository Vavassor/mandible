#pragma once

struct Canvas;
struct Stack;
struct Heap;

namespace game {

void startup(Heap* heap, Stack* stack);
void shutdown(Heap* heap);
void update(Stack* stack);
void draw(Canvas* canvas, Stack* stack);
void update_fps(int count);

} // namespace game
