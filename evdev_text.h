#ifndef EVDEV_TEXT_H_
#define EVDEV_TEXT_H_

#ifdef __cplusplus
extern "C" {
#endif

const char *button_code_text(int virtual_code);
const char *abs_code_text(int virtual_code);
const char *rel_code_text(int virtual_code);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
