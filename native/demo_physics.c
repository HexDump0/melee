#include "demo_physics.h"

#include <math.h>

#define DEMO_FIGHTER_HALF_WIDTH 4.5f
#define DEMO_FIGHTER_HEIGHT 11.0f
#define DEMO_ATTACK_RANGE 14.0f
#define DEMO_ATTACK_HEIGHT 14.0f
#define DEMO_ATTACK_FRAMES 10
#define DEMO_ATTACK_COOLDOWN 18
#define DEMO_ATTACK_DAMAGE 8.0f
#define DEMO_ATTACK_KNOCKBACK 5.5f

static float clampf(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static float approach_zero(float value, float amount)
{
    if (fabsf(value) <= amount) {
        return 0.0f;
    }
    return value > 0.0f ? value - amount : value + amount;
}

void demo_physics_default_attrs(DemoPhysicsAttrs* attrs)
{
    if (!attrs) {
        return;
    }
    /*
     * These are tunable sandbox values, not a claim about one character's
     * exact data table. The field names mirror ftCo_DatAttrs in
     * src/melee/ft/types.h (+034..+078).
     */
    attrs->ground_accel = 0.22f;
    attrs->ground_friction = 0.12f;
    attrs->ground_max_speed = 3.2f;
    attrs->gravity = 0.12f;
    attrs->terminal_velocity = 3.0f;
    attrs->air_accel = 0.08f;
    attrs->air_friction = 0.05f;
    attrs->air_max_speed = 2.2f;
    attrs->jump_velocity = 3.1f;
    attrs->max_jumps = 2;
}

void demo_physics_init_world(DemoPhysicsWorld* world)
{
    if (!world) {
        return;
    }
    world->platform_count = 3;
    world->platforms[0].left = -70.0f;
    world->platforms[0].right = 70.0f;
    world->platforms[0].top = 0.0f;
    world->platforms[1].left = -35.0f;
    world->platforms[1].right = -5.0f;
    world->platforms[1].top = 12.0f;
    world->platforms[2].left = 5.0f;
    world->platforms[2].right = 35.0f;
    world->platforms[2].top = 12.0f;
    world->spawn_x = 0.0f;
    world->spawn_y = 0.0f;
    world->death_y = -80.0f;
}

void demo_physics_reset(DemoPhysicsFighter* fighter,
                        const DemoPhysicsWorld* world)
{
    if (!fighter) {
        return;
    }
    fighter->x = world ? world->spawn_x : 0.0f;
    fighter->y = world ? world->spawn_y : 0.0f;
    fighter->vx = 0.0f;
    fighter->vy = 0.0f;
    fighter->facing = 1;
    fighter->grounded = 1;
    fighter->jumps = 2;
    fighter->attack_timer = 0;
    fighter->attack_cooldown = 0;
    fighter->hitstun = 0;
    fighter->shield = 0;
    fighter->damage = 0.0f;
    fighter->stocks = 4;
}

static void land_on_platform(DemoPhysicsFighter* fighter,
                             const DemoPhysicsWorld* world,
                             float old_y, int max_jumps)
{
    int i;
    if (!world) {
        return;
    }
    for (i = 0; i < world->platform_count && i < DEMO_PHYSICS_MAX_PLATFORMS;
         ++i) {
        const DemoPhysicsPlatform* p = &world->platforms[i];
        if (fighter->x + DEMO_FIGHTER_HALF_WIDTH < p->left ||
            fighter->x - DEMO_FIGHTER_HALF_WIDTH > p->right) {
            continue;
        }
        /* One-way platforms: only a descending crossing lands. */
        if (fighter->vy <= 0.0f && old_y >= p->top &&
            fighter->y <= p->top) {
            fighter->y = p->top;
            fighter->vy = 0.0f;
            fighter->grounded = 1;
            fighter->jumps = max_jumps;
            return;
        }
    }
}

static int has_platform_under(const DemoPhysicsFighter* fighter,
                              const DemoPhysicsWorld* world)
{
    int i;
    if (!world) {
        return 0;
    }
    for (i = 0; i < world->platform_count && i < DEMO_PHYSICS_MAX_PLATFORMS;
         ++i) {
        const DemoPhysicsPlatform* p = &world->platforms[i];
        if (fighter->x + DEMO_FIGHTER_HALF_WIDTH >= p->left &&
            fighter->x - DEMO_FIGHTER_HALF_WIDTH <= p->right &&
            fabsf(fighter->y - p->top) < 0.01f) {
            return 1;
        }
    }
    return 0;
}

void demo_physics_step(DemoPhysicsFighter* fighter,
                       const DemoPhysicsAttrs* attrs,
                       const DemoPhysicsWorld* world,
                       DemoPhysicsInput input)
{
    float axis;
    float old_y;
    int jump;
    if (!fighter || !attrs) {
        return;
    }

    axis = clampf(input.axis, -1.0f, 1.0f);
    jump = input.jump_pressed != 0;
    fighter->shield = input.shield != 0;
    if (fighter->attack_timer > 0) {
        --fighter->attack_timer;
    }
    if (fighter->attack_cooldown > 0) {
        --fighter->attack_cooldown;
    }

    if (fighter->hitstun > 0) {
        --fighter->hitstun;
        fighter->shield = 0;
        {
            float old_y = fighter->y;
            fighter->x += fighter->vx;
            fighter->y += fighter->vy;
            if (fighter->grounded && !has_platform_under(fighter, world)) {
                fighter->grounded = 0;
                fighter->jumps = 1;
            }
            if (!fighter->grounded) {
                land_on_platform(fighter, world, old_y, attrs->max_jumps);
            }
        }
        fighter->vy -= attrs->gravity;
        if (fighter->vy < -attrs->terminal_velocity) {
            fighter->vy = -attrs->terminal_velocity;
        }
        if (fighter->grounded) {
            fighter->vx = approach_zero(fighter->vx, attrs->ground_friction);
        }
        return;
    }

    if (axis > 0.05f) {
        fighter->facing = 1;
    } else if (axis < -0.05f) {
        fighter->facing = -1;
    }

    if (jump && !fighter->shield && fighter->jumps > 0) {
        fighter->vy = attrs->jump_velocity;
        fighter->grounded = 0;
        --fighter->jumps;
    }
    if (input.attack_pressed && !fighter->shield &&
        fighter->attack_cooldown == 0) {
        fighter->attack_timer = DEMO_ATTACK_FRAMES;
        fighter->attack_cooldown = DEMO_ATTACK_COOLDOWN;
    }

    if (fighter->grounded) {
        /* Mirrors ftCommon_CalcGroundAccel_DashRun's target/clamp shape. */
        float target = axis * attrs->ground_max_speed;
        if (fabsf(axis) < 0.05f) {
            fighter->vx = approach_zero(fighter->vx, attrs->ground_friction);
        } else if (fighter->vx * axis < 0.0f) {
            fighter->vx += axis * attrs->ground_accel;
        } else if (fabsf(fighter->vx) < fabsf(target)) {
            fighter->vx += axis * attrs->ground_accel;
            fighter->vx = clampf(fighter->vx, -fabsf(target), fabsf(target));
        } else {
            fighter->vx = approach_zero(fighter->vx, attrs->ground_friction);
        }
        fighter->vx = clampf(fighter->vx, -attrs->ground_max_speed,
                             attrs->ground_max_speed);
    } else {
        /* Mirrors ftCommon_CalcSelfAccel_Drift and its air max clamp. */
        float target = axis * attrs->air_max_speed;
        float accel = axis * attrs->air_accel;
        if (fabsf(axis) < 0.05f) {
            fighter->vx = approach_zero(fighter->vx, attrs->air_friction);
        } else if (fighter->vx * accel < 0.0f) {
            fighter->vx += accel;
        } else {
            fighter->vx += accel;
            if ((accel > 0.0f && fighter->vx > target) ||
                (accel < 0.0f && fighter->vx < target)) {
                fighter->vx = target;
            }
        }
        fighter->vx = clampf(fighter->vx, -attrs->air_max_speed,
                             attrs->air_max_speed);
        /* Exact sign and terminal clamp from ftCommon_Fall. */
        fighter->vy -= attrs->gravity;
        if (fighter->vy < -attrs->terminal_velocity) {
            fighter->vy = -attrs->terminal_velocity;
        }
    }

    old_y = fighter->y;
    fighter->x += fighter->vx;
    fighter->y += fighter->vy;
    if (fighter->grounded && !has_platform_under(fighter, world)) {
        /* Walking past a platform edge enters fall with one aerial jump. */
        fighter->grounded = 0;
        fighter->jumps = attrs->max_jumps > 1 ? attrs->max_jumps - 1 : 0;
    }
    if (!fighter->grounded) {
        land_on_platform(fighter, world, old_y, attrs->max_jumps);
    }
    if (world && fighter->y < world->death_y) {
        int stocks = fighter->stocks;
        if (fighter->stocks > 0) {
            --fighter->stocks;
        }
        demo_physics_reset(fighter, world);
        fighter->stocks = stocks > 0 ? stocks - 1 : 0;
    }
}

int demo_physics_try_hit(DemoPhysicsFighter* attacker,
                         DemoPhysicsFighter* target)
{
    float dx;
    float dy;
    if (!attacker || !target || attacker == target || attacker->attack_timer == 0 ||
        target->shield) {
        return 0;
    }
    dx = target->x - attacker->x;
    dy = fabsf(target->y - attacker->y);
    if (dx * (float) attacker->facing < 0.0f ||
        fabsf(dx) > DEMO_ATTACK_RANGE || dy > DEMO_ATTACK_HEIGHT) {
        return 0;
    }
    target->damage += DEMO_ATTACK_DAMAGE;
    target->vx = (float) attacker->facing * DEMO_ATTACK_KNOCKBACK;
    target->vy = DEMO_ATTACK_KNOCKBACK * 0.65f;
    target->grounded = 0;
    target->hitstun = 20;
    target->jumps = target->jumps > 0 ? target->jumps : 0;
    attacker->attack_timer = 0;
    return 1;
}
