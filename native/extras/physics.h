/*
 * Small host-side movement sandbox for the native demo.
 *
 * This is deliberately independent of Fighter/HSD/GX.  It is a controller
 * harness for bringing up a renderer and input loop before the full Melee
 * object/state machine is portable.  Values and update equations are kept
 * close to the original where the source is unambiguous; the hitbox,
 * platforms, respawn, and state transitions are demo code.
 */
#ifndef MELEE_NATIVE_DEMO_PHYSICS_H
#define MELEE_NATIVE_DEMO_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#define DEMO_PHYSICS_MAX_PLATFORMS 8

typedef struct DemoPhysicsInput {
    /* Expected range is [-1, 1]. Values outside it are clamped. */
    float axis;
    int jump_pressed;
    int attack_pressed;
    int shield;
} DemoPhysicsInput;

typedef struct DemoPhysicsAttrs {
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
} DemoPhysicsAttrs;

typedef struct DemoPhysicsFighter {
    float x;
    float y;
    float vx;
    float vy;
    int facing;       /* -1 or +1 */
    int grounded;
    int jumps;        /* jumps remaining, including the ground jump */
    int attack_timer; /* demo attack window, in fixed frames */
    int attack_cooldown;
    int hitstun;
    int shield;
    float damage;
    int stocks;
} DemoPhysicsFighter;

typedef struct DemoPhysicsPlatform {
    float left;
    float right;
    float top;
} DemoPhysicsPlatform;

typedef struct DemoPhysicsWorld {
    DemoPhysicsPlatform platforms[DEMO_PHYSICS_MAX_PLATFORMS];
    int platform_count;
    float spawn_x;
    float spawn_y;
    float death_y;
} DemoPhysicsWorld;

/* Defaults are intentionally demo-friendly approximations of a Melee fighter. */
void demo_physics_default_attrs(DemoPhysicsAttrs* attrs);
void demo_physics_init_world(DemoPhysicsWorld* world);
void demo_physics_reset(DemoPhysicsFighter* fighter, const DemoPhysicsWorld* world);

/* Advances exactly one 60 Hz simulation frame. */
void demo_physics_step(DemoPhysicsFighter* fighter,
                       const DemoPhysicsAttrs* attrs,
                       const DemoPhysicsWorld* world,
                       DemoPhysicsInput input);

/* Applies the demo attack interaction. Returns non-zero when a hit occurred. */
int demo_physics_try_hit(DemoPhysicsFighter* attacker,
                         DemoPhysicsFighter* target);

#ifdef __cplusplus
}
#endif

#endif
