#ifndef MELEE_NATIVE_GAME_ATTRIBUTES_H
#define MELEE_NATIVE_GAME_ATTRIBUTES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Movement values read from the game's ftCo_DatAttrs table.  The sandbox
 * consumes these; the real fighter code will too. */
typedef struct FighterAttrs {
    float ground_accel;
    float ground_friction;
    float ground_max_speed;
    float gravity;
    float terminal_velocity;
    float air_accel;
    float air_friction;
    float air_max_speed;
    float jump_velocity;
    int max_jumps;
} FighterAttrs;

/* Loads Mario's common per-frame movement values from PlMr.dat's
 * ftDataMario HSD public root. */
int load_mario_attrs(const char *disc_image, FighterAttrs *attrs,
                          char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
