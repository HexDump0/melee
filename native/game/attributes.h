#ifndef MELEE_NATIVE_DEMO_ATTRIBUTES_H
#define MELEE_NATIVE_DEMO_ATTRIBUTES_H

#include <stddef.h>
#include "extras/physics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Loads Mario's common per-frame movement values from PlMr.dat's
 * ftDataMario HSD public root. */
int demo_load_mario_attrs(const char *disc_image, DemoPhysicsAttrs *attrs,
                          char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
