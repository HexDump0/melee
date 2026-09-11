/* Tiny assert-based checks for the hand-written matrix math.  These exist so
 * the compiled-decomp experiment (P-301) has a parity target: when mtx.c is
 * compiled behind the shim, the same cases must produce the same results. */
#include "gx/math.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);      \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static void check_near(const float got[3], float x, float y, float z,
                       const char *msg)
{
    if (!near(got[0], x) || !near(got[1], y) || !near(got[2], z)) {
        fprintf(stderr, "FAIL: %s got [%f %f %f] want [%f %f %f]\n", msg,
                got[0], got[1], got[2], x, y, z);
        failures++;
    }
}

int main(void)
{
    Mat4 m, a, b;
    float v[3];

    m4_identity(m);
    CHECK(near(m[0], 1) && near(m[5], 1) && near(m[10], 1) && near(m[15], 1),
          "identity diagonal");
    CHECK(near(m[1], 0) && near(m[4], 0) && near(m[12], 0),
          "identity off-diagonal");

    /* glTranslate then glRotate builds T*R, so the origin stays put. */
    m4_identity(a);
    m4_mul_translate(a, 2.0f, 0.0f, 0.0f);
    m4_mul_rotate(a, 90.0f, 0.0f, 0.0f, 1.0f);
    m4_transform_point(v, a, (const float[3]){ 0, 0, 0 });
    check_near(v, 2.0f, 0.0f, 0.0f, "T*R keeps origin");

    /* glRotate then glTranslate builds R*T, so the translation is rotated. */
    m4_identity(a);
    m4_mul_rotate(a, 90.0f, 0.0f, 0.0f, 1.0f);
    m4_mul_translate(a, 2.0f, 0.0f, 0.0f);
    m4_transform_point(v, a, (const float[3]){ 0, 0, 0 });
    check_near(v, 0.0f, 2.0f, 0.0f, "R*T rotates translation");

    /* a single rotation moves +X to +Y. */
    m4_identity(b);
    m4_mul_rotate(b, 90.0f, 0.0f, 0.0f, 1.0f);
    m4_transform_point(v, b, (const float[3]){ 1, 0, 0 });
    check_near(v, 0.0f, 1.0f, 0.0f, "rotate +X to +Y");

    /* dir transform normalizes. */
    m4_identity(b);
    m4_mul_scale(b, 3.0f, 0.0f, 0.0f);
    m4_transform_dir(v, b, (const float[3]){ 2, 0, 0 });
    check_near(v, 1.0f, 0.0f, 0.0f, "transform_dir normalize");

    /* normal matrix of a rotation is its inverse transpose (the rotation
     * itself); of a uniform scale it is scale^-1. */
    {
        float n[9];
        m4_identity(b);
        m4_mul_rotate(b, 35.0f, 0.0f, 1.0f, 0.0f);
        m4_normal_mtx(n, b);
        m4_transform_dir(v, b, (const float[3]){ 0, 0, 1 });
        {
            float w[3] = { n[6], n[7], n[8] };
            float len = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            w[0] /= len; w[1] /= len; w[2] /= len;
            check_near(w, v[0], v[1], v[2], "normal matrix rotation");
        }
    }

    if (failures == 0) {
        printf("math: all checks passed\n");
    }
    return failures != 0;
}
