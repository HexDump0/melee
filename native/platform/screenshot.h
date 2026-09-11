#ifndef MELEE_NATIVE_PLATFORM_SCREENSHOT_H
#define MELEE_NATIVE_PLATFORM_SCREENSHOT_H

/* Reads the current framebuffer and writes a BMP (SDL pixel format). */
int platform_screenshot(const char *path, int w, int h);

#endif
