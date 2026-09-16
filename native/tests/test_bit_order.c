/*
 * Command/flag bit order (P-502/W0, gate zero).
 *
 * MWCC allocates a bit-field group from the most significant bit of its
 * storage unit; GCC and Clang allocate from the least significant one.  The
 * decompilation's subaction command structs are declared with CMD_BE
 * (scalar_storage_order), which reproduces MWCC's allocation -- on GCC.
 * ADR-0022 records that Clang *ignores* that attribute, and Emscripten is
 * Clang, so the browser target decodes these structs wrongly while compiling
 * without complaint.
 *
 * This test pins the console's decoding of real subaction bytes, so the
 * failure is a red test rather than a wrong instruction executed at runtime.
 * Expected values are the definition of MSB-first allocation: a field of
 * width w declared after p preceding bits occupies the w bits starting p
 * below the top of its storage unit.
 */
#include <melee/lb/types.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char* what, unsigned got, unsigned want)
{
    if (got != want) {
        fprintf(stderr, "bit_order: %s = %u, expected %u\n", what, got, want);
        failures++;
    }
}

int main(void)
{
    /* Fills its storage unit exactly: 6 + 8 + 18 = 32.
     * 0x44000000 is a real "play sound" command word; the console reads
     * opcode 17 from the top six bits. */
    {
        const unsigned char be[4] = { 0x44, 0x00, 0x00, 0x00 };
        struct sound_effect_0 s;
        memcpy(&s, be, sizeof s);
        check("sound_effect_0.opcode", s.opcode, 17);
        check("sound_effect_0.behavior", s.behavior, 0);
        check("sound_effect_0.unknown", s.unknown, 0);
    }

    /* Does NOT fill its unit: 6 + 1 = 7 of 8 bits.  This is the case where
     * naively reversing the field order is wrong -- the group must be padded
     * to the top of the byte first (G-180). */
    {
        const unsigned char be[1] = { 0x46 };
        struct unk6 s;
        memcpy(&s, be, sizeof s);
        check("unk6.opcode", s.opcode, 17); /* 0x46 >> 2 */
        check("unk6.unk1", s.unk1, 1);      /* (0x46 >> 1) & 1 */
    }

    /* Also short of its unit: 9 + 9 + 9 = 27 of 32. */
    {
        const unsigned char be[4] = { 0x00, 0x80, 0x40, 0x20 };
        struct set_throw_hitbox_1 s;
        unsigned w = 0x00804020u;
        memcpy(&s, be, sizeof s);
        check("set_throw_hitbox_1.unk0", s.unk0, (w >> 23) & 0x1FF);
        check("set_throw_hitbox_1.hit_x24", s.hit_x24, (w >> 14) & 0x1FF);
        check("set_throw_hitbox_1.hit_x28", s.hit_x28, (w >> 5) & 0x1FF);
    }

    if (failures != 0) {
        fprintf(stderr, "test_bit_order: FAIL (%d)\n", failures);
        return 1;
    }
    printf("test_bit_order: PASS\n");
    return 0;
}
