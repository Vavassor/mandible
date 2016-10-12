#include "input.h"

#include "sized_types.h"
#include "logging.h"
#include "profile.h"
#include "string_utilities.h"
#include "evdev_text.h"
#include "assert.h"

#include <X11/keysym.h>

#include <libudev.h>
#include <linux/input.h>
#include <linux/joystick.h>

#include <unistd.h>
#include <fcntl.h>

#include <cmath>

using std::sqrt;

// General-Use Functions.......................................................

#define BIT_COUNT(x) ((((x)-1)/(sizeof(long) * 8))+1)

#define CHECK_BIT(array, n) \
    (((1UL << ((n) % (sizeof(long) * 8))) & ((array)[(n) / (sizeof(long) * 8)])) != 0)

static bool starts_with(const char* string, const char* token) {
    while (*token) {
        if (*token++ != *string++) {
            return false;
        }
    }
    return true;
}

// Keyboard State Functions....................................................

namespace input {

enum KeyMapping {
    KEY_MAPPING_LEFT,
    KEY_MAPPING_UP,
    KEY_MAPPING_RIGHT,
    KEY_MAPPING_DOWN,
    KEY_MAPPING_A,
    KEY_MAPPING_TAB,
    KEY_MAPPING_COUNT,
};

struct KeyboardState {
    u32 key_map[KEY_MAPPING_COUNT];
    bool keys_pressed[KEY_MAPPING_COUNT];
    int edge_counts[KEY_MAPPING_COUNT];
};

static void setup_keyboard_state(KeyboardState* keyboard_state) {
    keyboard_state->key_map[KEY_MAPPING_LEFT]  = XK_Left;
    keyboard_state->key_map[KEY_MAPPING_UP]    = XK_Up;
    keyboard_state->key_map[KEY_MAPPING_RIGHT] = XK_Right;
    keyboard_state->key_map[KEY_MAPPING_DOWN]  = XK_Down;
    keyboard_state->key_map[KEY_MAPPING_A]     = XK_x;
    keyboard_state->key_map[KEY_MAPPING_TAB]   = XK_Tab;
}

static void press_key(KeyboardState* keyboard_state, u32 key_symbol) {
    for (int i = 0; i < KEY_MAPPING_COUNT; ++i) {
        if (keyboard_state->key_map[i] == key_symbol) {
            keyboard_state->keys_pressed[i] = true;
            keyboard_state->edge_counts[i] = 0;
        }
    }
}

static void release_key(KeyboardState* keyboard_state, u32 key_symbol) {
    for (int i = 0; i < KEY_MAPPING_COUNT; ++i) {
        if (keyboard_state->key_map[i] == key_symbol) {
            keyboard_state->keys_pressed[i] = false;
            keyboard_state->edge_counts[i] = 0;
        }
    }
}

static void update_key_change_counts(KeyboardState* keyboard_state) {
    for (int i = 0; i < KEY_MAPPING_COUNT; ++i) {
        keyboard_state->edge_counts[i] += 1;
    }
}

static bool is_key_down(KeyboardState* keyboard_state, KeyMapping key) {
    return keyboard_state->keys_pressed[key];
}

// Gamepad Functions...........................................................

struct Device {
    struct AxisSpecification {
        int deadband[2];
        double coefficients[2];
    };

    char name[128];
    u8 button_map[KEY_MAX - BTN_MISC];
    AxisSpecification axis_attributes[ABS_MAX];
    u8 absolute_map[ABS_MAX];
    u64 device_number;
    int file;
    int button_count;
    int axis_count;
    int hat_count;
    int ball_count;
    u16 vendor;
    u16 product;
    u16 version;
};

static const int device_collection_max = 8;

struct DeviceCollection {
    Device devices[device_collection_max];
    int device_count;
};

static void close_device(Device* device) {
    close(device->file);
    LOG_DEBUG("input device #%lu closed", device->device_number);
}

static bool evdev_setup_device(Device* device) {
    // Get basic identifying information for the device.
    if (ioctl(device->file, EVIOCGNAME(sizeof device->name), device->name) < 0) {
        // @Incomplete: log error
        return false;
    }
    LOG_DEBUG("named: %s", device->name);

    input_id id;
    if (ioctl(device->file, EVIOCGID, &id) < 0) {
        // @Incomplete: log error
        return false;
    }
    device->vendor = id.vendor;
    device->product = id.product;
    device->version = id.version;

    // Determine its capabilities.
    unsigned long keybit[BIT_COUNT(KEY_MAX)] = { 0 };
    unsigned long absbit[BIT_COUNT(ABS_MAX)] = { 0 };
    unsigned long relbit[BIT_COUNT(REL_MAX)] = { 0 };

    if (ioctl(device->file, EVIOCGBIT(EV_KEY, sizeof keybit), keybit) < 0 ||
        ioctl(device->file, EVIOCGBIT(EV_ABS, sizeof absbit), absbit) < 0 ||
        ioctl(device->file, EVIOCGBIT(EV_REL, sizeof relbit), relbit) < 0) {
        // @Incomplete: log error
        return false;
    }

    // Get normal joystick buttons first.
    for (int i = BTN_JOYSTICK; i < KEY_MAX; ++i) {
        if (CHECK_BIT(keybit, i)) {
            LOG_DEBUG("has button: 0x%x %s", i, button_code_text(i));
            device->button_map[i - BTN_MISC] = device->button_count;
            device->button_count += 1;
        }
    }

    // Then, get miscellaneous buttons after joystick buttons.
    for (int i = BTN_MISC; i < BTN_JOYSTICK; ++i) {
        if (CHECK_BIT(keybit, i)) {
            LOG_DEBUG("has button: 0x%x %s", i, button_code_text(i));
            device->button_map[i - BTN_MISC] = device->button_count;
            device->button_count += 1;
        }
    }

    // Get axes before hats.
    for (int i = 0; i < ABS_MISC; ++i) {
        // Skip over hats, in the middle of the absolute section.
        if (i == ABS_HAT0X) {
            i = ABS_HAT3Y;
            continue;
        }

        // Get this axis's specification.
        if (CHECK_BIT(absbit, i)) {
            input_absinfo absinfo;
            if (ioctl(device->file, EVIOCGABS(i), &absinfo) < 0) {
                continue;
            }

            device->absolute_map[i] = device->axis_count;

            int min = absinfo.minimum;
            int max = absinfo.maximum;

            int dead_zone = absinfo.flat + (max - min) / 9;

            int dead_min = 0;
            double normalized_min = 0.0;
            if (min < 0) {
                dead_min = -dead_zone;
                normalized_min = -1.0;
            }
            device->axis_attributes[i].deadband[0] = dead_min;

            int dead_max = 0;
            double normalized_max = 0.0;
            if (max > 0) {
                dead_max = dead_zone;
                normalized_max = 1.0;
            }
            device->axis_attributes[i].deadband[1] = dead_max;

            double a = (normalized_max - normalized_min) / static_cast<double>((max - dead_max) + (normalized_min - min));
            double b = normalized_max - a * static_cast<double>(max - dead_max);

            device->axis_attributes[i].coefficients[0] = a;
            device->axis_attributes[i].coefficients[1] = b;

            device->axis_count += 1;
        }
    }

    // Then go and get hat-switches after axes.
    for (int i = ABS_HAT0X; i <= ABS_HAT3Y; i += 2) {
        if (CHECK_BIT(absbit, i) || CHECK_BIT(absbit, i + 1)) {
            input_absinfo absinfo;
            if (ioctl(device->file, EVIOCGABS(i), &absinfo) < 0) {
                continue;
            }

            LOG_DEBUG("has hat: 0x%x %s  Values = { %i, %i, %i, %i, %i, %i }",
                      (i - ABS_HAT0X) / 2, abs_code_text(i),
                      absinfo.value, absinfo.minimum, absinfo.maximum,
                      absinfo.fuzz, absinfo.flat, absinfo.resolution);
            device->hat_count += 1;
        }
    }

    if (CHECK_BIT(relbit, REL_X) || CHECK_BIT(relbit, REL_Y)) {
        // Balls are not used yet, but register their existence anyways.
        device->ball_count += 1;
    }

    return true;
}

static bool js_setup_device(Device* device) {
    s8 axis_count;
    s8 button_count;
    if (ioctl(device->file, JSIOCGAXES, &axis_count) < 0 ||
        ioctl(device->file, JSIOCGBUTTONS, &button_count) < 0 ||
        ioctl(device->file, JSIOCGNAME(sizeof device->name), &device->name) < 0) {
        // @Incomplete: add logging
        return false;
    }

    LOG_DEBUG("named: %s", device->name);

    bool polling = true;
    js_event event;
    while (polling && read(device->file, &event, sizeof event)) {
        switch (event.type & ~JS_EVENT_INIT) {
            case JS_EVENT_AXIS: {
                if (device->axis_count >= axis_count) {
                    polling = false;
                    break;
                }
                LOG_DEBUG("has axis: 0x%x", event.number);
                device->absolute_map[event.number] = device->axis_count;
                device->axis_count += 1;
                break;
            }
            case JS_EVENT_BUTTON: {
                if (device->button_count >= button_count) {
                    polling = false;
                    break;
                }
                LOG_DEBUG("has button: 0x%x", event.number);
                device->button_map[event.number] = device->button_count;
                device->button_count += 1;
                break;
            }
        }
    }

    return true;
}

static void add_device(DeviceCollection* device_collection, udev_device* device) {
    if (device_collection->device_count >= device_collection_max) {
        ASSERT(!"too many input devices!");
        return;
    }

    // whichever class or classes udev categorizes it as
    bool is_joystick = false;
    const char* subsystem = udev_device_get_subsystem(device);
    if (subsystem && strings_match(subsystem, "input")) {
        const char* val;

        val = udev_device_get_property_value(device, "ID_INPUT_JOYSTICK");
        if (val && strings_match(val, "1")) {
            is_joystick = true;
        }

        // Fall back to old-style input classes.
        val = udev_device_get_property_value(device, "ID_CLASS");
        if (val && strings_match(val, "joystick")) {
            is_joystick = true;
        }
    }
    if (!is_joystick) {
        // @Incomplete: log error
        return;
    }

    Device added_device = {};

    // Open the device file.

    const char* device_path = udev_device_get_devnode(device);
    int file = open(device_path, O_RDONLY | O_NONBLOCK, 0);
    if (file < 0) {
        // @Incomplete: log error
        return;
    }
    added_device.file = file;

    dev_t device_number = udev_device_get_devnum(device);
    added_device.device_number = device_number;
    LOG_DEBUG("input device #%lu opened at: %s", device_number, device_path);

    bool device_setup = false;

    const char* system_name = udev_device_get_sysname(device);
    if (starts_with(system_name, "event")) {
        // Use the device under the evdev system.
        device_setup = evdev_setup_device(&added_device);
    } else if (starts_with(system_name, "js")) {
        // use device with the joystick api
        device_setup = js_setup_device(&added_device);
    }

    if (device_setup) {
        device_collection->devices[device_collection->device_count] = added_device;
        device_collection->device_count += 1;
    } else {
        close_device(&added_device);
    }
}

static void remove_device(DeviceCollection* device_collection, udev_device* device) {
    if (device_collection->device_count <= 0) {
        ASSERT(!"no devices to remove");
        return;
    }

    dev_t device_number = udev_device_get_devnum(device);
    for (int i = 0; i < device_collection->device_count; ++i) {
        Device* search_device = device_collection->devices + i;
        if (device_number == search_device->device_number) {
            close_device(search_device);
            int final = device_collection->device_count - 1;
            if (final >= 0) {
                device_collection->devices[i] = device_collection->devices[final];
            }
            device_collection->device_count -= 1;
            break;
        }
    }
}

static void force_detect_devices(udev* context, DeviceCollection* device_collection) {
    // Create a list of the devices in the 'input' subsystem.
    udev_enumerate* enumerator = udev_enumerate_new(context);
    udev_enumerate_add_match_subsystem(enumerator, "input");
    udev_enumerate_scan_devices(enumerator);
    udev_list_entry* devices = udev_enumerate_get_list_entry(enumerator);

    // Iterate over each entry in the list and add it to the collection.

    udev_list_entry* entry;
    udev_list_entry_foreach(entry, devices) {
        const char* path = udev_list_entry_get_name(entry);
        udev_device* device = udev_device_new_from_syspath(context, path);
        add_device(device_collection, device);
        udev_device_unref(device);
    }

    udev_enumerate_unref(enumerator);
}

static void check_device_monitor(udev_monitor* device_monitor, DeviceCollection* device_collection) {
    // the file descriptor for the monitor
    int fd = udev_monitor_get_fd(device_monitor);
    ASSERT(fd >= 0); // fd is supposed to be guaranteed valid but assert to be sure

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    int result = select(fd + 1, &fds, nullptr, nullptr, &timeout);

    // Check if the file descriptor has received data.
    if (result > 0 && FD_ISSET(fd, &fds)) {
        udev_device* device = udev_monitor_receive_device(device_monitor);
        if (!device) {
            LOG_ERROR("A device change occurred, but info about it was not able to be obtained.");
        }

        const char* action = udev_device_get_action(device);
        if (!action) {
            LOG_ERROR("A device change occurred, but the type of change was not discernible.");

        } else if (strings_match(action, "add")) {
            add_device(device_collection, device);

        } else if (strings_match(action, "remove")) {
            remove_device(device_collection, device);

        } else if (strings_match(action, "change")) {
            remove_device(device_collection, device);
            add_device(device_collection, device);

        } else {
            LOG_ERROR("Device change event of type \"%s\" is not a type that's handled.", action);
        }

        udev_device_unref(device);
    }
}

// Controller Functions........................................................

struct Controller {
    bool buttons[USER_BUTTON_COUNT];
    int button_counts[USER_BUTTON_COUNT];
    double axes[USER_AXIS_COUNT];
};

bool is_button_pressed(Controller* controller, UserButton button) {
    return controller->buttons[button];
}

bool is_button_released(Controller* controller, UserButton button) {
    return controller->buttons[button];
}

bool is_button_tapped(Controller* controller, UserButton button) {
    if (controller->buttons[button]) {
        // when the button is pressed and has been for a single frame only
        // (hasn't been incremented to 1 or greater, yet)
        return controller->button_counts[button] == 0;
    } else {
        return false;
    }
}

double get_axis(Controller* controller, UserAxis axis) {
    return controller->axes[axis];
}

static void update_controller_from_keyboard_state(Controller* controller, KeyboardState* keyboard_state) {
    double dx = 0.0;
    double dy = 0.0;
    if (is_key_down(keyboard_state, KEY_MAPPING_LEFT)) {
        dx -= 1.0;
    }
    if (is_key_down(keyboard_state, KEY_MAPPING_RIGHT)) {
        dx += 1.0;
    }
    if (is_key_down(keyboard_state, KEY_MAPPING_DOWN)) {
        dy -= 1.0;
    }
    if (is_key_down(keyboard_state, KEY_MAPPING_UP)) {
        dy += 1.0;
    }
    double magnitude = sqrt(dx * dx + dy * dy);
    if (magnitude > 0.0) {
        controller->axes[USER_AXIS_HORIZONTAL] = dx / magnitude;
        controller->axes[USER_AXIS_VERTICAL] = dy / magnitude;
    } else {
        controller->axes[USER_AXIS_HORIZONTAL] = 0.0;
        controller->axes[USER_AXIS_VERTICAL] = 0.0;
    }

    controller->buttons[USER_BUTTON_A] = keyboard_state->keys_pressed[KEY_MAPPING_A];
    controller->button_counts[USER_BUTTON_A] = keyboard_state->edge_counts[KEY_MAPPING_A];

    controller->buttons[USER_BUTTON_TAB] = keyboard_state->keys_pressed[KEY_MAPPING_TAB];
    controller->button_counts[USER_BUTTON_TAB] = keyboard_state->edge_counts[KEY_MAPPING_TAB];
}

#if 0
static void update_controller_from_gamepad(Controller* controller, Device* device) {
    // @Incomplete
}
#endif

// Mouse Functions.............................................................

static const int button_count = 2;

struct MouseState {
    int x, y;
    bool buttons_pressed[button_count];
    int edge_counts[button_count];
};

static void press_button(MouseState* mouse_state, unsigned int button) {
    if (button == 1 || button == 2) {
        int button_index = button - 1;
        mouse_state->buttons_pressed[button_index] = true;
        mouse_state->edge_counts[button_index] = 0;
    }
}

static void release_button(MouseState* mouse_state, unsigned int button) {
    if (button == 1 || button == 2) {
        int button_index = button - 1;
        mouse_state->buttons_pressed[button_index] = false;
        mouse_state->edge_counts[button_index] = 0;
    }
}

static void move_mouse(MouseState* mouse_state, int x, int y) {
    mouse_state->x = x;
    mouse_state->y = y;
}

static void update_button_change_counts(MouseState* mouse_state) {
    for (int i = 0; i < button_count; ++i) {
        mouse_state->edge_counts[i] += 1;
    }
}

// Global Input System Functions...............................................

namespace {
    DeviceCollection device_collection;
    KeyboardState keyboard_state;
    MouseState mouse_state;
    udev* context;
    udev_monitor* device_monitor;
    Controller controller;
}

bool startup() {
    // create library interface
    udev* udev = udev_new();
    if (!udev) {
        LOG_ERROR("Could not create udev library context for gamepad input.");
        shutdown();
        return false;
    }
    context = udev;

    // create monitor for device-changed notifications
    udev_monitor* monitor = udev_monitor_new_from_netlink(udev, "udev");
    if (!monitor) {
        LOG_ERROR("Could not create a monitor for detecting device changes.");
        shutdown();
        return false;
    }
    device_monitor = monitor;

    udev_monitor_filter_add_match_subsystem_devtype(device_monitor, "input", nullptr);
    udev_monitor_enable_receiving(device_monitor);

    force_detect_devices(context, &device_collection);

    setup_keyboard_state(&keyboard_state);

    return true;
}

void shutdown() {
    for (int i = 0; i < device_collection.device_count; ++i) {
        close_device(device_collection.devices + i);
    }
    if (device_monitor) {
        udev_monitor_unref(device_monitor);
    }
    if (context) {
        udev_unref(context);
    }
}

void poll() {
    PROFILE_SCOPED();
    check_device_monitor(device_monitor, &device_collection);
    update_controller_from_keyboard_state(&controller, &keyboard_state);
    update_key_change_counts(&keyboard_state);
    update_button_change_counts(&mouse_state);
}

void on_key_press(u32 key_symbol) {
    press_key(&keyboard_state, key_symbol);
}

void on_key_release(u32 key_symbol) {
    release_key(&keyboard_state, key_symbol);
}

void on_button_press(unsigned int button) {
    press_button(&mouse_state, button);
}

void on_button_release(unsigned int button) {
    release_button(&mouse_state, button);
}

void on_mouse_move(int x, int y) {
    move_mouse(&mouse_state, x, y);
}

Controller* get_controller() {
    return &controller;
}

void get_mouse_position(int* x, int* y) {
    *x = mouse_state.x;
    *y = mouse_state.y;
}

bool get_mouse_pressed() {
    return mouse_state.buttons_pressed[0];
}

bool get_mouse_clicked() {
    return mouse_state.buttons_pressed[0] && mouse_state.edge_counts[0] == 0;
}

} // namespace input
