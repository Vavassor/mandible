#pragma once

#include "sized_types.h"

namespace input {

enum UserButton {
	USER_BUTTON_A,
	USER_BUTTON_TAB,
	USER_BUTTON_COUNT,
};

enum UserAxis {
	USER_AXIS_HORIZONTAL,
	USER_AXIS_VERTICAL,
	USER_AXIS_COUNT,
};

struct Controller;
bool is_button_pressed(Controller* controller, UserButton button);
bool is_button_released(Controller* controller, UserButton button);
bool is_button_tapped(Controller* controller, UserButton button);
double get_axis(Controller* controller, UserAxis axis);

bool startup();
void shutdown();
void poll();
void on_key_press(u32 key_symbol);
void on_key_release(u32 key_symbol);
void on_button_press(unsigned int button);
void on_button_release(unsigned int button);
void on_mouse_move(int x, int y);
Controller* get_controller();

void get_mouse_position(int* x, int* y);
bool get_mouse_pressed();
bool get_mouse_clicked();

} // namespace input
