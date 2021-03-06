#include "evdev_text.h"

#include <linux/input.h>

#define TEXT_CASE(code)	\
    case code: return #code;

const char* button_code_text(int virtual_code) {
    switch (virtual_code) {
        // BTN_MISC
        TEXT_CASE(BTN_0);
        TEXT_CASE(BTN_1);
        TEXT_CASE(BTN_2);
        TEXT_CASE(BTN_3);
        TEXT_CASE(BTN_4);
        TEXT_CASE(BTN_5);
        TEXT_CASE(BTN_6);
        TEXT_CASE(BTN_7);
        TEXT_CASE(BTN_8);
        TEXT_CASE(BTN_9);

        // BTN_MOUSE
        TEXT_CASE(BTN_LEFT);
        TEXT_CASE(BTN_RIGHT);
        TEXT_CASE(BTN_MIDDLE);
        TEXT_CASE(BTN_SIDE);
        TEXT_CASE(BTN_EXTRA);
        TEXT_CASE(BTN_FORWARD);
        TEXT_CASE(BTN_BACK);
        TEXT_CASE(BTN_TASK);

        // BTN_JOYSTICK - Joysticks and Flight Sticks
        TEXT_CASE(BTN_TRIGGER);
        TEXT_CASE(BTN_THUMB);
        TEXT_CASE(BTN_THUMB2);
        TEXT_CASE(BTN_TOP);
        TEXT_CASE(BTN_TOP2);
        TEXT_CASE(BTN_PINKIE);
        TEXT_CASE(BTN_BASE);
        TEXT_CASE(BTN_BASE2);
        TEXT_CASE(BTN_BASE3);
        TEXT_CASE(BTN_BASE4);
        TEXT_CASE(BTN_BASE5);
        TEXT_CASE(BTN_BASE6);
        TEXT_CASE(BTN_DEAD);

        // BTN_GAMEPAD
        TEXT_CASE(BTN_SOUTH);
        TEXT_CASE(BTN_EAST);
        TEXT_CASE(BTN_C);
        TEXT_CASE(BTN_NORTH);
        TEXT_CASE(BTN_WEST);
        TEXT_CASE(BTN_Z);
        TEXT_CASE(BTN_TL);
        TEXT_CASE(BTN_TR);
        TEXT_CASE(BTN_TL2);
        TEXT_CASE(BTN_TR2);
        TEXT_CASE(BTN_SELECT);
        TEXT_CASE(BTN_START);
        TEXT_CASE(BTN_MODE);
        TEXT_CASE(BTN_THUMBL);
        TEXT_CASE(BTN_THUMBR);

        // BTN_DIGI - Trackpads, Stylus, and Touchpads
        TEXT_CASE(BTN_TOOL_PEN);
        TEXT_CASE(BTN_TOOL_RUBBER);
        TEXT_CASE(BTN_TOOL_BRUSH);
        TEXT_CASE(BTN_TOOL_PENCIL);
        TEXT_CASE(BTN_TOOL_AIRBRUSH);
        TEXT_CASE(BTN_TOOL_FINGER);
        TEXT_CASE(BTN_TOOL_MOUSE);
        TEXT_CASE(BTN_TOOL_LENS);
        TEXT_CASE(BTN_TOOL_QUINTTAP);
        TEXT_CASE(BTN_TOUCH);
        TEXT_CASE(BTN_STYLUS);
        TEXT_CASE(BTN_STYLUS2);
        TEXT_CASE(BTN_TOOL_DOUBLETAP);
        TEXT_CASE(BTN_TOOL_TRIPLETAP);
        TEXT_CASE(BTN_TOOL_QUADTAP);

        // BTN_WHEEL - Driving Wheels
        TEXT_CASE(BTN_GEAR_DOWN);
        TEXT_CASE(BTN_GEAR_UP);

        // for d-pad reported as digital buttons, as opposed
        // to being reported as a hat switch
        TEXT_CASE(BTN_DPAD_UP);
        TEXT_CASE(BTN_DPAD_DOWN);
        TEXT_CASE(BTN_DPAD_LEFT);
        TEXT_CASE(BTN_DPAD_RIGHT);

        // BTN_TRIGGER_HAPPY - Extra buttons
        TEXT_CASE(BTN_TRIGGER_HAPPY1);
        TEXT_CASE(BTN_TRIGGER_HAPPY2);
        TEXT_CASE(BTN_TRIGGER_HAPPY3);
        TEXT_CASE(BTN_TRIGGER_HAPPY4);
        TEXT_CASE(BTN_TRIGGER_HAPPY5);
        TEXT_CASE(BTN_TRIGGER_HAPPY6);
        TEXT_CASE(BTN_TRIGGER_HAPPY7);
        TEXT_CASE(BTN_TRIGGER_HAPPY8);
        TEXT_CASE(BTN_TRIGGER_HAPPY9);
        TEXT_CASE(BTN_TRIGGER_HAPPY10);
        TEXT_CASE(BTN_TRIGGER_HAPPY11);
        TEXT_CASE(BTN_TRIGGER_HAPPY12);
        TEXT_CASE(BTN_TRIGGER_HAPPY13);
        TEXT_CASE(BTN_TRIGGER_HAPPY14);
        TEXT_CASE(BTN_TRIGGER_HAPPY15);
        TEXT_CASE(BTN_TRIGGER_HAPPY16);
        TEXT_CASE(BTN_TRIGGER_HAPPY17);
        TEXT_CASE(BTN_TRIGGER_HAPPY18);
        TEXT_CASE(BTN_TRIGGER_HAPPY19);
        TEXT_CASE(BTN_TRIGGER_HAPPY20);
        TEXT_CASE(BTN_TRIGGER_HAPPY21);
        TEXT_CASE(BTN_TRIGGER_HAPPY22);
        TEXT_CASE(BTN_TRIGGER_HAPPY23);
        TEXT_CASE(BTN_TRIGGER_HAPPY24);
        TEXT_CASE(BTN_TRIGGER_HAPPY25);
        TEXT_CASE(BTN_TRIGGER_HAPPY26);
        TEXT_CASE(BTN_TRIGGER_HAPPY27);
        TEXT_CASE(BTN_TRIGGER_HAPPY28);
        TEXT_CASE(BTN_TRIGGER_HAPPY29);
        TEXT_CASE(BTN_TRIGGER_HAPPY30);
        TEXT_CASE(BTN_TRIGGER_HAPPY31);
        TEXT_CASE(BTN_TRIGGER_HAPPY32);
        TEXT_CASE(BTN_TRIGGER_HAPPY33);
        TEXT_CASE(BTN_TRIGGER_HAPPY34);
        TEXT_CASE(BTN_TRIGGER_HAPPY35);
        TEXT_CASE(BTN_TRIGGER_HAPPY36);
        TEXT_CASE(BTN_TRIGGER_HAPPY37);
        TEXT_CASE(BTN_TRIGGER_HAPPY38);
        TEXT_CASE(BTN_TRIGGER_HAPPY39);
        TEXT_CASE(BTN_TRIGGER_HAPPY40);
    }
    return "Button Unknown";
}

const char* abs_code_text(int virtual_code) {
    switch (virtual_code) {
        // Gamepad, Joystick, and arcade-style controller codes
        TEXT_CASE(ABS_X);
        TEXT_CASE(ABS_Y);
        TEXT_CASE(ABS_Z);
        TEXT_CASE(ABS_RX);
        TEXT_CASE(ABS_RY);
        TEXT_CASE(ABS_RZ);
        TEXT_CASE(ABS_THROTTLE);
        TEXT_CASE(ABS_RUDDER);
        TEXT_CASE(ABS_WHEEL);
        TEXT_CASE(ABS_GAS);
        TEXT_CASE(ABS_BRAKE);
        TEXT_CASE(ABS_HAT0X);
        TEXT_CASE(ABS_HAT0Y);
        TEXT_CASE(ABS_HAT1X);
        TEXT_CASE(ABS_HAT1Y);
        TEXT_CASE(ABS_HAT2X);
        TEXT_CASE(ABS_HAT2Y);
        TEXT_CASE(ABS_HAT3X);
        TEXT_CASE(ABS_HAT3Y);

        // Tablet codes
        TEXT_CASE(ABS_PRESSURE);
        TEXT_CASE(ABS_DISTANCE);
        TEXT_CASE(ABS_TILT_X);
        TEXT_CASE(ABS_TILT_Y);
        TEXT_CASE(ABS_TOOL_WIDTH);

        // Miscellaneous
        TEXT_CASE(ABS_VOLUME);
        TEXT_CASE(ABS_MISC);

        // Multi-Touch Pad codes
        TEXT_CASE(ABS_MT_SLOT);
        TEXT_CASE(ABS_MT_TOUCH_MAJOR);
        TEXT_CASE(ABS_MT_TOUCH_MINOR);
        TEXT_CASE(ABS_MT_WIDTH_MAJOR);
        TEXT_CASE(ABS_MT_WIDTH_MINOR);
        TEXT_CASE(ABS_MT_ORIENTATION);
        TEXT_CASE(ABS_MT_POSITION_X);
        TEXT_CASE(ABS_MT_POSITION_Y);
        TEXT_CASE(ABS_MT_TOOL_TYPE);
        TEXT_CASE(ABS_MT_BLOB_ID);
        TEXT_CASE(ABS_MT_TRACKING_ID);
        TEXT_CASE(ABS_MT_PRESSURE);
        TEXT_CASE(ABS_MT_DISTANCE);
        TEXT_CASE(ABS_MT_TOOL_X);
        TEXT_CASE(ABS_MT_TOOL_Y);
    }
    return "Absolute Axis Unknown";
}

const char* rel_code_text(int virtual_code) {
    switch (virtual_code) {
        // mice, track-balls, scroll wheels
        TEXT_CASE(REL_X);
        TEXT_CASE(REL_Y);
        TEXT_CASE(REL_Z);
        TEXT_CASE(REL_RX);
        TEXT_CASE(REL_RY);
        TEXT_CASE(REL_RZ);
        TEXT_CASE(REL_HWHEEL);
        TEXT_CASE(REL_DIAL);
        TEXT_CASE(REL_WHEEL);
        TEXT_CASE(REL_MISC);
    }
    return "Relative Axis Unknown";
}
