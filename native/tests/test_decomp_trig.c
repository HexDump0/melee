/*
 * P-688: differential test for the decompiled `atanf` (src/melee/lb/lbtrigf.c).
 *
 * The retail function is Metrowerks PowerPC code that uses `fnmsubs`
 * (b - a*c with a single rounding) and single-precision operations.  The port
 * compiles it with GCC/SSE2 and the non-MWERKS `__fnmsubs` fallback; this test
 * checks that the compiled result is bit-identical to an independent
 * transcription of the source that rounds every float operation explicitly
 * and emulates `fnmsubs` with x87 long-double arithmetic (80-bit, exact for
 * the 48-bit product).  Without the fallback the port silently links glibc's
 * `atanf`, which differs on a small fraction of inputs.
 *
 * The input sweep is deterministic: all zero/denormal encodings, dense
 * windows around every branch boundary, a stride sweep over the whole 2^32
 * encoding space, and an LCG sample.  Disc-free.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* lbtrigf.c reads these for acosf/asinf (NAN/INF); same data as
 * native/platform/misc.c and src/MSL/float.c. */
int MSL_TrigF_80400770[] = { 0x7FFFFFFF };
int MSL_TrigF_80400774[] = { 0x7F800000 };

/* Verbatim copy of the spec table in src/melee/lb/lbtrigf.c. */
static const float atanf_lookup[] = {
    1.0,
    -0.3333333134651184,
    0.1999988704919815,
    -0.14281649887561798,
    0.11041180044412613,
    -0.08459755778312683,
    0.04714243486523628,
    6.828420162200928,
    3.239828109741211,
    2.0,
    1.4464620351791382,
    1.1715729236602783,
    1.039566159248352,
    7.1350000325764995e-06,
    8.200000252145401e-07,
    0.0,
    6.299999881775875e-07,
    0.0,
    0.0,
    0.0,
    0.3926900029182434,
    0.5890486240386963,
    0.7853981256484985,
    0.9817469716072083,
    1.1780970096588135,
    1.3744460344314575,
    0.0,
    9.081698408408556e-06,
    2.3000000126671694e-08,
    6.30000016599297e-08,
    7.040000014058023e-07,
    2.499999993688107e-07,
    7.900000014160469e-07,
    2.414212942123413,
    1.4966057538986206,
    1.0,
    0.6681786179542542,
    0.4142135679721832,
    0.1989123672246933,
    5.620000251838064e-07,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
};

static uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static float bits_float(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

/* PowerPC fnmsubs: b - a*c, one rounding; the negation of the macro is
 * exact.  x87 long double carries the 48-bit product exactly. */
static float ref_fnmsubs(float a, float c, float b)
{
    return (float) ((long double) b - (long double) a * (long double) c);
}

static float lookup(int index, int offset)
{
    return atanf_lookup[index + offset];
}

/* Independent transcription of the source algorithm: every float operation
 * rounds to single precision (SSE2 keeps declared widths), and only the
 * fused fnmsubs uses the long-double exact form. */
static float ref_atanf(float x)
{
    float const silver_ratio = 2.4142136573791504f;
    float const silver_ratio_conjugate = 0.4142135679721832f;

    uint32_t xb = float_bits(x);
    uint32_t xa = xb & ~0x80000000u; /* the source clears the sign in place */
    uint32_t sign_bit_x = xb & 0x80000000u;
    float ax = bits_float(xa);
    float result;
    int lookup_index = -1;
    int x_ge_ratio = 0;

    if (ax >= silver_ratio) {
        x_ge_ratio = 1;
        result = 1.0f / ax;
    } else if (silver_ratio_conjugate < ax) {
        lookup_index = 0;
        switch (xa & 0x7F800000u) {
        case 0x3F000000u: {
            if (!((int32_t) xa < (int32_t) 0x3F08D5B9u)) {
                lookup_index = 1;
            }
            if (!((int32_t) xa < (int32_t) 0x3F521801u)) {
                lookup_index += 1;
            }
            break;
        }
        case 0x3F800000u: {
            lookup_index = 2;
            if (!((int32_t) xa < (int32_t) 0x3F9BF7ECu)) {
                lookup_index = 3;
            }
            if (!((int32_t) xa < (int32_t) 0x3FEF789Eu)) {
                lookup_index += 1;
            }
            break;
        }
        case 0x40000000u: {
            lookup_index = 4;
            break;
        }
        }
        {
            float offset_39 = lookup(lookup_index, 39);
            float offset_33 = lookup(lookup_index, 33);
            result = 1.0f / (offset_33 + (ax + offset_39));
            result = ref_fnmsubs(result, lookup(lookup_index, 7), offset_33) +
                     ref_fnmsubs(result, lookup(lookup_index, 13), offset_39);
        }
    } else {
        result = ax;
    }

    {
        float result_squared = result * result;
        result = result * result_squared *
                     (result_squared *
                          (result_squared *
                               (result_squared *
                                    (result_squared *
                                         (result_squared * (atanf_lookup[6]) +
                                          atanf_lookup[5]) +
                                     atanf_lookup[4]) +
                                atanf_lookup[3]) +
                           atanf_lookup[2]) +
                      atanf_lookup[1]) +
                 result;
        result += lookup(lookup_index, 27);
        result += lookup(lookup_index, 20);
    }

    if (x_ge_ratio) {
        result -= (float) M_PI_2;
        return sign_bit_x ? result : -result;
    }

    return bits_float(float_bits(result) | sign_bit_x);
}

static unsigned mismatches;
static unsigned shown;

static void check(uint32_t bits)
{
    float in = bits_float(bits);
    float got = atanf(in);
    float want = ref_atanf(in);

    if (float_bits(got) != float_bits(want)) {
        if (shown < 8) {
            shown++;
            fprintf(stderr,
                    "decomp_trig: x=%08x (%g): got %08x (%g) want %08x (%g)\n",
                    bits, (double) in, float_bits(got), (double) got,
                    float_bits(want), (double) want);
        }
        mismatches++;
    }
}

int main(void)
{
    static const uint32_t specials[] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu,
        0x807FFFFFu, 0x3F800000u, 0xBF800000u, 0x7F800000u, 0xFF800000u,
        0x7FC00000u, 0xFFC00000u, 0x7FFFFFFFu, 0xFFFFFFFFu,
    };
    uint64_t samples = 0;
    uint32_t rng = 0x9E3779B9u;
    size_t i;
    uint32_t b;

    for (i = 0; i < sizeof(specials) / sizeof(specials[0]); i++) {
        check(specials[i]);
        samples++;
    }

    /* All zero/denormal encodings, both signs. */
    for (b = 0; b < (1u << 23); b++) {
        check(b);
        check(b | 0x80000000u);
    }
    samples += 2ull << 23;

    /* Dense windows around every branch boundary. */
    {
        const uint32_t boundary[6] = {
            float_bits(2.4142136573791504f),
            float_bits(0.4142135679721832f),
            0x3F08D5B9u, 0x3F521801u, 0x3F9BF7ECu, 0x3FEF789Eu,
        };
        size_t j;
        for (j = 0; j < sizeof(boundary) / sizeof(boundary[0]); j++) {
            int d;
            for (d = -65536; d <= 65536; d++) {
                check(boundary[j] + (uint32_t) d);
                samples++;
            }
        }
    }

    /* Stride sweep over the whole encoding space (all exponents/signs). */
    for (uint64_t u = 0; u < 0x100000000ull; u += 1237ull) {
        check((uint32_t) u);
        samples++;
    }

    /* LCG sample of the remaining space. */
    for (b = 0; b < (1u << 25); b++) {
        rng = rng * 1664525u + 1013904223u;
        check(rng);
        samples++;
    }

    if (mismatches != 0) {
        fprintf(stderr, "decomp_trig: FAIL %u/%llu mismatches\n", mismatches,
                (unsigned long long) samples);
        return 1;
    }
    printf("decomp_trig: PASS %llu samples bit-identical\n",
           (unsigned long long) samples);
    return 0;
}
