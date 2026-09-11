/*
 * Small host-side movement sandbox for the native port.
 *
 * This is deliberately independent of Fighter/HSD/GX.  It is a controller
 * harness for bringing up a renderer and input loop before the full Melee
 * object/state machine is portable.  Values and update equations are kept
 * close to the original where the source is unambiguous; the hitbox,
 * platforms, respawn, and state transitions are sandbox code.
 */
#ifndef MELEE_NATIVE_EXTRAS_PHYSICS_H
#define MELEE_NATIVE_EXTRAS_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "game/attributes.h"

#define SANDBOX_MAX_PLATFORMS 8

typedef struct SandboxInput {
    /* Expected range is [-1, 1]. Values outside it are clamped. */
    float axis;
    int jump_pressed;
    int attack_pressed;
    int shield;
} SandboxInput;

typedef struct SandboxFighter {
    float x;
    float y;
    float vx;
    float vy;
    int facing;       /* -1 or +1 */
    int grounded;
    int jumps;        /* jumps remaining, including the ground jump */
    int attack_timer; /* sandbox attack window, in fixed frames */
    int attack_cooldown;
    int hitstun;
    int shield;
    float damage;
    int stocks;
} SandboxFighter;

typedef struct SandboxPlatform {
    float left;
    float right;
    float top;
} SandboxPlatform;

typedef struct SandboxWorld {
    SandboxPlatform platforms[SANDBOX_MAX_PLATFORMS];
    int platform_count;
    float spawn_x;
    float spawn_y;
    float death_y;
} SandboxWorld;

/* Defaults are intentionally sandbox-friendly approximations of a Melee fighter. */
void sandbox_default_attrs(FighterAttrs* attrs);
void sandbox_init_world(SandboxWorld* world);
void sandbox_reset(SandboxFighter* fighter, const SandboxWorld* world);

/* Advances exactly one 60 Hz simulation frame. */
void sandbox_step(SandboxFighter* fighter,
                       const FighterAttrs* attrs,
                       const SandboxWorld* world,
                       SandboxInput input);

/* Applies the sandbox attack interaction. Returns non-zero when a hit occurred. */
int sandbox_try_hit(SandboxFighter* attacker,
                         SandboxFighter* target);

#ifdef __cplusplus
}
#endif

#endif
