#ifndef MELEE_NATIVE_GX_MATH_H
#define MELEE_NATIVE_GX_MATH_H

/*
 * Hand-written column-major matrix math used by the renderer and the extras.
 *
 * TODO(P-301): replace this module with the compiled decomp functions from
 * src/sysdolphin/baselib/mtx.c and vec.c once the platform shim exists.
 * Until then this is the only copy; there is no second source of truth.
 */
#define PI 3.14159265358979323846f

typedef float Mat4[16];

void m4_identity(Mat4 m);
void m4_mul(Mat4 out, const Mat4 a, const Mat4 b);
void m4_ortho(Mat4 m, float l, float r, float b, float t, float n, float f);
void m4_frustum(Mat4 m, float l, float r, float b, float t, float n, float f);
void m4_mul_translate(Mat4 m, float x, float y, float z);
void m4_mul_scale(Mat4 m, float x, float y, float z);
void m4_mul_rotate(Mat4 m, float angle_deg, float x, float y, float z);
void m4_look_at(Mat4 m, const float eye[3], const float target[3]);
void m4_normal_mtx(float out[9], const Mat4 mv);
void m4_transform_dir(float out[3], const Mat4 m, const float v[3]);
void m4_transform_point(float out[3], const Mat4 m, const float v[3]);

#endif
