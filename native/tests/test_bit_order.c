/*
 * Command/flag bit order (P-502 gate zero).
 *
 * MWCC allocates a bit-field group from the most significant bit of its
 * storage unit and the console reads each command word big-endian.  GCC can be
 * told both at once with `scalar_storage_order`, which is what `CMD_BE` used
 * to expand to -- but **Clang ignores that attribute in silence** (ADR-0022),
 * so the WebAssembly build decoded a real "play sound" command as opcode 4
 * instead of 17 and compiled without a warning.
 *
 * The port therefore states the layout instead of asking for it: host-order
 * declarations in <decomp_cmd_bits.h>, read through `CMD_U()`, which supplies
 * the byte swap.  This test walks **every field of every command struct** --
 * `native/tests/cmd_bits_checks.inc`, generated beside the header from the
 * same declarations -- and compares what the port reads against what the
 * console reads, arithmetically.  It is compiler-independent by construction:
 * it passes on GCC and on Emscripten, and it fails if the generated header
 * ever drifts from <melee/lb/types.h>.
 *
 * A sample would not have been enough.  63 of the 76 groups fill their storage
 * unit exactly and 13 do not, and it is the short ones that a naive field
 * reversal gets wrong (G-180).
 */
#include <melee/ft/types.h>
#include <melee/gm/types.h>
#include <melee/gr/types.h>
#include <melee/lb/types.h>

#include <stdio.h>
#include <string.h>

static int failures;
static int checked;

/* What the console reads: a field of width `n` whose top bit sits `p` bits
 * below the top of the big-endian command word. */
static long long console_field(unsigned word, int p, int n, int is_signed)
{
    unsigned mask = (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    unsigned v = (word >> (32 - p - n)) & mask;
    if (is_signed && n < 32 && (v >> (n - 1)) & 1u) {
        return (long long) v - (1LL << n);
    }
    if (is_signed) {
        return (long long) (int) v;
    }
    return (long long) v;
}

static unsigned long long rng = 0x9E3779B97F4A7C15ull;

static unsigned xorshift(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned) (rng >> 16);
}

/* A raw command word, big-endian in memory exactly as it sits in the archive,
 * with `cmd` pointing at it the way the interpreter does. */
static unsigned char raw[8];
static CommandInfo cmd;
static unsigned word;

static void set_word(unsigned w)
{
    word = w;
    raw[0] = (unsigned char) (w >> 24);
    raw[1] = (unsigned char) (w >> 16);
    raw[2] = (unsigned char) (w >> 8);
    raw[3] = (unsigned char) w;
    cmd.u = (union CmdUnion*) raw;
}

/* Read each struct off the word CMD_U() produced, rather than through its
 * CmdUnion member: several members are named differently from their struct
 * (`struct spawn_hitbox_5 create_hitbox_5`), and it is the declaration that is
 * under test.  CMD_U's byte swap is exercised by the value of `host`. */
#define CHK(S, F, P, N, SG)                                                   \
    do {                                                                      \
        unsigned host = CMD_U(&cmd)->host_word;                               \
        struct S s;                                                           \
        long long got, want = console_field(word, (P), (N), (SG));            \
        memcpy(&s, &host, sizeof s);                                          \
        got = (long long) s.F;                                                \
        checked++;                                                            \
        if (got != want && failures++ < 20) {                                 \
            fprintf(stderr,                                                   \
                    "bit_order: %s.%s of %08x = %lld, console reads %lld\n",  \
                    #S, #F, word, got, want);                                 \
        }                                                                     \
        if (sizeof(struct S) != 4 && failures++ < 20) {                       \
            fprintf(stderr, "bit_order: sizeof %s = %u, expected 4\n", #S,    \
                    (unsigned) sizeof(struct S));                             \
        }                                                                     \
    } while (0)

static void check_color_overlay(void)
{
    /* The same treatment, on the three ColorOverlay groups; they are unnamed
     * structs inside a union, so the generator cannot reach them. */
    union ColorOverlay_x8_t* p = (union ColorOverlay_x8_t*) raw;
    struct {
        const char* name;
        long long got, want;
    } v[8];
    int i, n = 0;

#define CO(expr, P, N, SG)                                                    \
    do {                                                                      \
        v[n].name = #expr;                                                    \
        v[n].got = (long long) (CO_X8(p)->expr);                              \
        v[n].want = console_field(word, (P), (N), (SG));                      \
        n++;                                                                  \
    } while (0)

    CO(light_rot1.unk, 0, 6, 1);
    CO(light_rot1.x, 6, 13, 1);
    CO(light_rot1.yz, 19, 13, 1);
    CO(light_rot2.x0_0, 0, 1, 0);
    CO(light_rot2.light_enable, 6, 1, 0);
    CO(light_rot2.x, 8, 12, 1);
    CO(light_rot2.yz, 20, 12, 1);
    CO(unk.timer, 6, 26, 0);
#undef CO

    for (i = 0; i < n; i++) {
        checked++;
        if (v[i].got != v[i].want && failures++ < 20) {
            fprintf(stderr,
                    "bit_order: ColorOverlay %s of %08x = %lld, console "
                    "reads %lld\n",
                    v[i].name, word, v[i].got, v[i].want);
        }
    }
}

static void check_opcode_view(void)
{
    /* ftaction.c dispatches on this one: the opcode of any command word. */
    struct gmScriptEventDefault* e =
        (struct gmScriptEventDefault*) CMD_U(&cmd);
    checked++;
    if ((long long) e->opcode != console_field(word, 0, 6, 0) &&
        failures++ < 20)
    {
        fprintf(stderr, "bit_order: gmScriptEventDefault.opcode of %08x = %u\n",
                word, (unsigned) e->opcode);
    }
}

static void check_flag_bytes(void)
{
    /* PORT_BF_BE's two groups: one byte, so MSB-first allocation is all there
     * is to reproduce.  `value = 1` must set the console's bit, not b0. */
    UnkFlagStruct f;
    struct grCorneria_GroundVars g;

    memset(&f, 0, sizeof f);
    f.byte = 0x80;
    checked++;
    if (f.b0 != 1 && failures++ < 20) {
        fprintf(stderr, "bit_order: UnkFlagStruct 0x80 is not b0\n");
    }
    f.byte = 0x01;
    checked++;
    if (f.b7 != 1 && failures++ < 20) {
        fprintf(stderr, "bit_order: UnkFlagStruct 0x01 is not b7\n");
    }

    memset(&g, 0, sizeof g);
    g.xC4.value = 1;
    checked++;
    if ((g.xC4.flags.b0 | g.xC4.flags.b1 | g.xC4.flags.b2) != 0 &&
        failures++ < 20)
    {
        fprintf(stderr, "bit_order: grCorneria xC4 value=1 named a flag\n");
    }
    g.xC4.value = 0x80;
    checked++;
    if (g.xC4.flags.b0 != 1 && failures++ < 20) {
        fprintf(stderr, "bit_order: grCorneria xC4 0x80 is not b0\n");
    }
}

int main(void)
{
    int it;

    /* 0x44000000 is a real "play sound" command word; the console reads
     * opcode 17 from the top six bits, and Clang read 4 before this test. */
    set_word(0x44000000u);
    if ((unsigned) CMD_U(&cmd)->sound_effect_0.opcode != 17) {
        fprintf(stderr, "bit_order: sound_effect_0.opcode of 44000000 = %u, "
                        "expected 17\n",
                (unsigned) CMD_U(&cmd)->sound_effect_0.opcode);
        failures++;
    }

    for (it = 0; it < 512; it++) {
        set_word(it == 0   ? 0x44000000u
                 : it == 1 ? 0xFFFFFFFFu
                 : it == 2 ? 0x00000000u
                           : xorshift());
#include "cmd_bits_checks.inc"
        check_color_overlay();
        check_opcode_view();
    }
    check_flag_bytes();

    if (failures != 0) {
        fprintf(stderr, "test_bit_order: FAIL (%d of %d)\n", failures, checked);
        return 1;
    }
    printf("test_bit_order: PASS (%d field reads)\n", checked);
    return 0;
}
