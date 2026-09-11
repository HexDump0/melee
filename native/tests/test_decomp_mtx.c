/*
 * Differential test for the compiled decomp HSD math (P-301).
 *
 * HSD_MtxSRT is compiled verbatim from src/sysdolphin/baselib/mtx.c by the
 * melee_decomp_math target (native/decomp/shim/decomp_shim.h).  It is checked
 * against two oracles:
 *
 *   1. ref_make_local_mtx: literal transcription of the hand copy that used to
 *      live in hsd/model.c, kept here as the differential oracle AGENTS 0.1
 *      asks for.  The compiled TU and this transcription run the same formula
 *      in the same order, so comparisons are expected bitwise.
 *   2. gx/math.c composition T * Rz * Ry * Rx * S for the vec4 == NULL case,
 *      an independent path that also proves the 3x4 -> Mat4 layout conversion.
 *
 * The RNG is a fixed-seed LCG so a failure reproduces exactly.
 */
#include "decomp/decomp_math.h"
#include "gx/math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SAMPLES 100000
#define ORACLE_TOL 1e-5f

static int failures;

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    failures++;
}

/* Literal transcription of the deleted hand copy, hsd/model.c:make_local_mtx. */
static void ref_make_local_mtx(float m[3][4], const float scale[3],
                               const float rot[3], const float pos[3],
                               const float *parent_scale)
{
    float sx = scale[0];
    float sy = scale[1];
    float sz = scale[2];
    float vx2 = sx;
    float vx1 = sx;
    float vx = sx;
    float vy2 = sy;
    float vy1 = sy;
    float vy = sy;
    float vz2 = sz;
    float vz1 = sz;
    float vz = sz;
    float sin_x;
    float cos_x;
    float sin_y;
    float cos_y;
    float sin_z;
    float cos_z;
    if (parent_scale != NULL) {
        float t1 = 1.0f / parent_scale[0];
        float t2 = 1.0f / parent_scale[1];
        float t3 = 1.0f / parent_scale[2];
        vy2 *= parent_scale[1] * t1;
        vz2 *= parent_scale[2] * t1;
        vx1 *= parent_scale[0] * t2;
        vz1 *= parent_scale[2] * t2;
        vx *= parent_scale[0] * t3;
        vy *= parent_scale[1] * t3;
    }
    sin_x = sinf(rot[0]);
    cos_x = cosf(rot[0]);
    sin_y = sinf(rot[1]);
    cos_y = cosf(rot[1]);
    sin_z = sinf(rot[2]);
    cos_z = cosf(rot[2]);
    m[0][0] = cos_z * (vx2 * cos_y);
    m[1][0] = sin_z * (vx1 * cos_y);
    m[2][0] = -vx * sin_y;
    m[0][1] = vy2 * ((cos_z * (sin_x * sin_y)) - (cos_x * sin_z));
    m[1][1] = vy1 * ((sin_z * (sin_x * sin_y)) + (cos_x * cos_z));
    m[2][1] = cos_y * (vy * sin_x);
    m[0][2] = vz2 * ((cos_z * (cos_x * sin_y)) + (sin_x * sin_z));
    m[1][2] = vz1 * ((sin_z * (cos_x * sin_y)) - (sin_x * cos_z));
    m[2][2] = cos_y * (vz * cos_x);
    m[0][3] = pos[0];
    m[1][3] = pos[1];
    m[2][3] = pos[2];
}

static unsigned rng_state = 0x5EED1234u;

static float rnd(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (float) ((rng_state >> 8) & 0xffffu) / 32768.0f - 1.0f;
}

static void check_hand_cases(void)
{
    DecompVec3 one = { 1.0f, 1.0f, 1.0f };
    DecompVec3 zero = { 0.0f, 0.0f, 0.0f };
    DecompVec3 two = { 2.0f, 2.0f, 2.0f };
    DecompVec3 pos = { 1.0f, 2.0f, 3.0f };
    DecompVec3 rot_z = { 0.0f, 0.0f, PI / 2.0f };
    float m[3][4];
    float v[3];

    HSD_MtxSRT(m, &one, &zero, &zero, NULL);
    if (m[0][0] != 1.0f || m[1][1] != 1.0f || m[2][2] != 1.0f ||
        m[0][1] != 0.0f || m[1][0] != 0.0f || m[0][3] != 0.0f) {
        fail("identity SRT is not identity");
    }

    HSD_MtxSRT(m, &two, &zero, &pos, NULL);
    if (m[0][0] != 2.0f || m[1][1] != 2.0f || m[2][2] != 2.0f ||
        m[0][3] != 1.0f || m[1][3] != 2.0f || m[2][3] != 3.0f) {
        fail("scale+translate SRT");
    }

    /* +90 degrees about Z maps +X to +Y (matches the gx/math.c convention). */
    HSD_MtxSRT(m, &one, &rot_z, &zero, NULL);
    v[0] = m[0][0];
    v[1] = m[1][0];
    v[2] = m[2][0];
    if (fabsf(v[0]) > 1e-6f || fabsf(v[1] - 1.0f) > 1e-6f ||
        fabsf(v[2]) > 1e-6f) {
        fail("Z rotation does not map +X to +Y");
    }
}

static void check_random(void)
{
    float worst_ref = 0.0f;
    float worst_oracle = 0.0f;
    int ref_mismatch = 0;
    int oracle_mismatch = 0;
    int i;
    int k;

    for (i = 0; i < SAMPLES; ++i) {
        float scale[3];
        float rot[3];
        float pos[3];
        float parent[3];
        float got[3][4];
        float ref[3][4];
        DecompVec3 scale_v;
        DecompVec3 rot_v;
        DecompVec3 pos_v;
        DecompVec3 parent_v;
        const float *parent_p = NULL;

        for (k = 0; k < 3; ++k) {
            scale[k] = rnd() * 4.0f;
            rot[k] = rnd() * 6.0f;
            pos[k] = rnd() * 50.0f;
        }
        scale_v.x = scale[0];
        scale_v.y = scale[1];
        scale_v.z = scale[2];
        rot_v.x = rot[0];
        rot_v.y = rot[1];
        rot_v.z = rot[2];
        pos_v.x = pos[0];
        pos_v.y = pos[1];
        pos_v.z = pos[2];

        if (i & 1) {
            for (k = 0; k < 3; ++k) {
                parent[k] = 0.05f + fabsf(rnd());
            }
            parent_v.x = parent[0];
            parent_v.y = parent[1];
            parent_v.z = parent[2];
            parent_p = parent;
        }

        HSD_MtxSRT(got, &scale_v, &rot_v, &pos_v, parent_p ? &parent_v : NULL);
        ref_make_local_mtx(ref, scale, rot, pos, parent_p);

        if (memcmp(got, ref, sizeof(ref)) != 0) {
            int r;
            int c;
            ref_mismatch++;
            for (r = 0; r < 3; ++r) {
                for (c = 0; c < 4; ++c) {
                    float d = fabsf(got[r][c] - ref[r][c]);
                    if (d > worst_ref) {
                        worst_ref = d;
                    }
                }
            }
        }

        /* Independent oracle: no parent-scale case only. */
        if (parent_p == NULL) {
            Mat4 comp;
            int r;
            int c;
            m4_identity(comp);
            m4_mul_translate(comp, pos[0], pos[1], pos[2]);
            m4_mul_rotate(comp, rot[2] * 180.0f / PI, 0.0f, 0.0f, 1.0f);
            m4_mul_rotate(comp, rot[1] * 180.0f / PI, 0.0f, 1.0f, 0.0f);
            m4_mul_rotate(comp, rot[0] * 180.0f / PI, 1.0f, 0.0f, 0.0f);
            m4_mul_scale(comp, scale[0], scale[1], scale[2]);

            for (r = 0; r < 3; ++r) {
                for (c = 0; c < 4; ++c) {
                    float d = fabsf(got[r][c] - comp[c * 4 + r]);
                    if (d > worst_oracle) {
                        worst_oracle = d;
                    }
                    if (d > ORACLE_TOL) {
                        oracle_mismatch++;
                    }
                }
            }
        }
    }

    printf("decomp_mtx: %d samples, ref worst %.9g, gx oracle worst %.9g\n",
           SAMPLES, worst_ref, worst_oracle);
    if (ref_mismatch != 0) {
        fail("HSD_MtxSRT differs from the literal transcription");
    }
    if (oracle_mismatch != 0) {
        fail("HSD_MtxSRT differs from the gx/math.c composition");
    }
}

int main(void)
{
    check_hand_cases();
    check_random();
    if (failures == 0) {
        printf("decomp_mtx: all checks passed\n");
    }
    return failures != 0;
}
