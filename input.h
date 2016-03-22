#pragma once

#include "sized_types.h"

namespace input {

enum UserButton {
	USER_BUTTON_A,
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
float get_axis(Controller* controller, UserAxis axis);

struct System;
System* startup();
void shutdown(System* system);
void poll(System* system);
void on_key_press(System* system, u32 key_symbol);
void on_key_release(System* system, u32 key_symbol);
Controller* get_controller(System* system);

} // namespace input
