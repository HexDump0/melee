#ifndef MELEE_NATIVE_HSD_LIGHT_H
#define MELEE_NATIVE_HSD_LIGHT_H

/*
 * Scene lights and fog for the native port.
 *
 * The game builds `HSD_LObj` objects from `HSD_LightDesc` chains and feeds
 * them to `HSD_SetupChannelMode` / `HSD_SetupChannel`.  The port only needs
 * the data (type, colour, position/interest, attenuation, shininess) to
 * evaluate the same channel equations in its shaders, so this module parses
 * the descriptors rather than carrying GX light objects.
 *
 * Source of truth: src/sysdolphin/baselib/lobj.h + lobj.c (LOBJ_* flags,
 * HSD_LObjSetupInit, setup_*_lightobj), fog.h + fog.c (HSD_Fog).
 */
#include <stddef.h>
#include <stdint.h>

#define MAX_LOBS 8

#ifndef LOBJ_AMBIENT
#define LOBJ_AMBIENT 0u
#endif
#ifndef LOBJ_INFINITE
#define LOBJ_INFINITE 1u
#endif
#ifndef LOBJ_POINT
#define LOBJ_POINT 2u
#endif
#ifndef LOBJ_SPOT
#define LOBJ_SPOT 3u
#endif
#ifndef LOBJ_DIFFUSE
#define LOBJ_DIFFUSE 0x04u
#endif
#ifndef LOBJ_SPECULAR
#define LOBJ_SPECULAR 0x08u
#endif
#ifndef LOBJ_ALPHA
#define LOBJ_ALPHA 0x10u
#endif
#ifndef LOBJ_HIDDEN
#define LOBJ_HIDDEN 0x20u
#endif
#ifndef LOBJ_RAW_PARAM
#define LOBJ_RAW_PARAM 0x40u
#endif

typedef struct SceneLight {
    uint16_t flags;
    uint16_t attnflags;
    uint8_t color[4];
    uint8_t type;      /* LOBJ_* */
    uint8_t has_position;
    uint8_t has_interest;
    float position[3]; /* HSD_WObjDesc.pos */
    float interest[3];
    float shininess;
    /* Finite lights. */
    float ref_dist;
    float ref_br;
    uint32_t dist_func;
    float attn_a0, attn_a1, attn_a2;
    float attn_k0, attn_k1, attn_k2;
    float cutoff;
    uint32_t spot_func;
} SceneLight;

typedef struct SceneFog {
    uint8_t present;
    uint32_t type; /* GX_FOG_* */
    float start;
    float end;
    uint8_t color[4];
} SceneFog;

typedef struct SceneLights {
    size_t count;
    SceneLight lights[MAX_LOBS];
    SceneFog fog;
} SceneLights;

/* Loads MnSlChr.usd (fallback MnSlChr.dat) and parses
 * MnSelectChrDataTable's light0/light1 chains and fog. */
int lights_load(const char *disc_image, SceneLights *set, char *error,
                     size_t error_size);

void lights_dump(const SceneLights *set);

/* The active ambient light used by HSD_SetupChannelMode case 4: the ambient
 * slot's colour when it carries LOBJ_DIFFUSE, else black. */
void lights_ambient(const SceneLights *set, float out[3]);

/* Number of non-ambient lights carrying `mask` (LOBJ_DIFFUSE/SPECULAR). */
size_t lights_count(const SceneLights *set, uint16_t mask);

#endif
