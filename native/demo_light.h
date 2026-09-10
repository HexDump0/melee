#ifndef DEMO_LIGHT_H
#define DEMO_LIGHT_H

/*
 * Scene lights and fog for the native demo.
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

#define DEMO_MAX_LOBS 8

#define DEMO_LOBJ_AMBIENT 0u
#define DEMO_LOBJ_INFINITE 1u
#define DEMO_LOBJ_POINT 2u
#define DEMO_LOBJ_SPOT 3u
#define DEMO_LOBJ_DIFFUSE 0x04u
#define DEMO_LOBJ_SPECULAR 0x08u
#define DEMO_LOBJ_ALPHA 0x10u
#define DEMO_LOBJ_HIDDEN 0x20u
#define DEMO_LOBJ_RAW_PARAM 0x40u

typedef struct DemoLight {
    uint16_t flags;
    uint16_t attnflags;
    uint8_t color[4];
    uint8_t type;      /* DEMO_LOBJ_* */
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
} DemoLight;

typedef struct DemoFog {
    uint8_t present;
    uint32_t type; /* GX_FOG_* */
    float start;
    float end;
    uint8_t color[4];
} DemoFog;

typedef struct DemoLightSet {
    size_t count;
    DemoLight lights[DEMO_MAX_LOBS];
    DemoFog fog;
} DemoLightSet;

/* Loads MnSlChr.usd (fallback MnSlChr.dat) and parses
 * MnSelectChrDataTable's light0/light1 chains and fog. */
int demo_lights_load(const char *disc_image, DemoLightSet *set, char *error,
                     size_t error_size);

void demo_lights_dump(const DemoLightSet *set);

/* The active ambient light used by HSD_SetupChannelMode case 4: the ambient
 * slot's colour when it carries LOBJ_DIFFUSE, else black. */
void demo_lights_ambient(const DemoLightSet *set, float out[3]);

/* Number of non-ambient lights carrying `mask` (DEMO_LOBJ_DIFFUSE/SPECULAR). */
size_t demo_lights_count(const DemoLightSet *set, uint16_t mask);

#endif
