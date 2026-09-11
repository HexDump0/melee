/*
 * Portable C backend for the Dolphin SDK math primitives.
 *
 * The SDK's own TUs (extern/dolphin/src/dolphin/mtx/mtx.c, vec.c) are
 * Metrowerks hybrid asm and cannot be compiled on the host (G-047), so the
 * decompiled port provides them here (ADR-0011 rule 3).  Semantics follow the
 * SDK's C twins (C_MTXConcat, C_MTXCopy, ...) and the Mtx layout: row-major
 * 3x4 with translation in column 3.
 *
 * Only the primitives the compiled HSD path actually calls are implemented;
 * add more as the link demands, with a CTest if behavior is nontrivial.
 */
#include <dolphin/mtx.h>
#include <sysdolphin/baselib/mtx.h>

#include <math.h>
#include <string.h>

void PSMTXIdentity(register Mtx m)
{
    memset(m, 0, sizeof(Mtx));
    m[0][0] = 1.0f;
    m[1][1] = 1.0f;
    m[2][2] = 1.0f;
}

void PSMTXCopy(register Mtx src, register Mtx dst)
{
    memcpy(dst, src, sizeof(Mtx));
}

void PSMTXConcat(register Mtx a, register Mtx b, register Mtx ab)
{
    Mtx m;
    f32(*o)[4] = (ab == a || ab == b) ? m : ab;

    o[0][0] = a[0][2] * b[2][0] + (a[0][0] * b[0][0] + a[0][1] * b[1][0]);
    o[0][1] = a[0][2] * b[2][1] + (a[0][0] * b[0][1] + a[0][1] * b[1][1]);
    o[0][2] = a[0][2] * b[2][2] + (a[0][0] * b[0][2] + a[0][1] * b[1][2]);
    o[0][3] = a[0][3] + (a[0][2] * b[2][3] +
                         (a[0][0] * b[0][3] + a[0][1] * b[1][3]));
    o[1][0] = a[1][2] * b[2][0] + (a[1][0] * b[0][0] + a[1][1] * b[1][0]);
    o[1][1] = a[1][2] * b[2][1] + (a[1][0] * b[0][1] + a[1][1] * b[1][1]);
    o[1][2] = a[1][2] * b[2][2] + (a[1][0] * b[0][2] + a[1][1] * b[1][2]);
    o[1][3] = a[1][3] + (a[1][2] * b[2][3] +
                         (a[1][0] * b[0][3] + a[1][1] * b[1][3]));
    o[2][0] = a[2][2] * b[2][0] + (a[2][0] * b[0][0] + a[2][1] * b[1][0]);
    o[2][1] = a[2][2] * b[2][1] + (a[2][0] * b[0][1] + a[2][1] * b[1][1]);
    o[2][2] = a[2][2] * b[2][2] + (a[2][0] * b[0][2] + a[2][1] * b[1][2]);
    o[2][3] = a[2][3] + (a[2][2] * b[2][3] +
                         (a[2][0] * b[0][3] + a[2][1] * b[1][3]));

    if (o != ab) {
        memcpy(ab, m, sizeof(Mtx));
    }
}

void PSMTXScale(register Mtx m, register f32 xS, register f32 yS,
                register f32 zS)
{
    memset(m, 0, sizeof(Mtx));
    m[0][0] = xS;
    m[1][1] = yS;
    m[2][2] = zS;
}

void PSMTXTrans(register Mtx m, register f32 xT, register f32 yT,
                register f32 zT)
{
    PSMTXIdentity(m);
    m[0][3] = xT;
    m[1][3] = yT;
    m[2][3] = zT;
}

void PSMTXQuat(register Mtx m, register Quaternion* q)
{
    f32 s;
    f32 xs;
    f32 ys;
    f32 zs;
    f32 wx;
    f32 wy;
    f32 wz;
    f32 xx;
    f32 xy;
    f32 xz;
    f32 yy;
    f32 yz;
    f32 zz;

    s = 2.0f / (q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w);
    xs = q->x * s;
    ys = q->y * s;
    zs = q->z * s;
    wx = q->w * xs;
    wy = q->w * ys;
    wz = q->w * zs;
    xx = q->x * xs;
    xy = q->x * ys;
    xz = q->x * zs;
    yy = q->y * ys;
    yz = q->y * zs;
    zz = q->z * zs;

    m[0][0] = 1.0f - (yy + zz);
    m[0][1] = xy - wz;
    m[0][2] = xz + wy;
    m[0][3] = 0.0f;
    m[1][0] = xy + wz;
    m[1][1] = 1.0f - (xx + zz);
    m[1][2] = yz - wx;
    m[1][3] = 0.0f;
    m[2][0] = xz - wy;
    m[2][1] = yz + wx;
    m[2][2] = 1.0f - (xx + yy);
    m[2][3] = 0.0f;
}

void PSMTXMultVec(register Mtx44 m, register Vec* src, register Vec* dst)
{
    Vec out;
    out.x = m[0][0] * src->x + m[0][1] * src->y + m[0][2] * src->z + m[0][3];
    out.y = m[1][0] * src->x + m[1][1] * src->y + m[1][2] * src->z + m[1][3];
    out.z = m[2][0] * src->x + m[2][1] * src->y + m[2][2] * src->z + m[2][3];
    *dst = out;
}

void PSMTXMultVecSR(register Mtx44 m, register Vec* src, register Vec* dst)
{
    Vec out;
    out.x = m[0][0] * src->x + m[0][1] * src->y + m[0][2] * src->z;
    out.y = m[1][0] * src->x + m[1][1] * src->y + m[1][2] * src->z;
    out.z = m[2][0] * src->x + m[2][1] * src->y + m[2][2] * src->z;
    *dst = out;
}

void PSMTXRotAxisRad(register Mtx m, register Vec* axis, register f32 rad)
{
    Vec n;
    f32 s;
    f32 c;
    f32 t;
    f32 x;
    f32 y;
    f32 z;
    f32 xSq;
    f32 ySq;
    f32 zSq;

    s = sinf(rad);
    c = cosf(rad);
    t = 1.0f - c;
    PSVECNormalize(axis, &n);
    x = n.x;
    y = n.y;
    z = n.z;
    xSq = x * x;
    ySq = y * y;
    zSq = z * z;
    m[0][0] = c + t * xSq;
    m[0][1] = y * (t * x) - s * z;
    m[0][2] = z * (t * x) + s * y;
    m[0][3] = 0.0f;
    m[1][0] = y * (t * x) + s * z;
    m[1][1] = c + t * ySq;
    m[1][2] = z * (t * y) - s * x;
    m[1][3] = 0.0f;
    m[2][0] = z * (t * x) - s * y;
    m[2][1] = z * (t * y) + s * x;
    m[2][2] = c + t * zSq;
    m[2][3] = 0.0f;
}

u32 PSMTXInverse(register Mtx src, register Mtx inv)
{
    HSD_MtxInverse(src, inv);
    return 0;
}

void PSVECAdd(register Vec* a, register Vec* b, register Vec* c)
{
    c->x = a->x + b->x;
    c->y = a->y + b->y;
    c->z = a->z + b->z;
}

void PSVECSubtract(register Vec* a, register Vec* b, register Vec* c)
{
    c->x = a->x - b->x;
    c->y = a->y - b->y;
    c->z = a->z - b->z;
}

void PSVECScale(register Vec* src, register Vec* dst, register f32 scale)
{
    dst->x = src->x * scale;
    dst->y = src->y * scale;
    dst->z = src->z * scale;
}

void PSVECNormalize(register Vec* src, register Vec* dst)
{
    f32 mag = src->z * src->z + (src->x * src->x + src->y * src->y);
    mag = 1.0f / sqrtf(mag);
    dst->x = src->x * mag;
    dst->y = src->y * mag;
    dst->z = src->z * mag;
}

f32 PSVECMag(register Vec* v)
{
    return sqrtf(v->z * v->z + (v->x * v->x + v->y * v->y));
}

f32 PSVECDotProduct(register Vec* a, register Vec* b)
{
    return a->z * b->z + (a->x * b->x + a->y * b->y);
}

void PSVECCrossProduct(register Vec* a, register Vec* b, register Vec* dst)
{
    Vec out;
    out.x = a->y * b->z - a->z * b->y;
    out.y = a->z * b->x - a->x * b->z;
    out.z = a->x * b->y - a->y * b->x;
    *dst = out;
}

