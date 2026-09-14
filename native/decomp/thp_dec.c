/*
 * P-685: host THP (MTHP) movie decoder.
 *
 * `extern/dolphin/src/dolphin/thp/THPDec.c` drives the GameCube locked cache
 * and leaves only the `__MWERKS__` asm control flow intact; its C fallback
 * labels fall through unconditionally, so it cannot be compiled to working C
 * on the host.  This replacement TU implements the same split API the game
 * calls (`lbmthp.c`, `lb_01F8.c`) with the decoder algorithm from Aurora's
 * MIT `lib/dolphin/thp/THPDec.cpp` (GX I8 tiling included), transcribed to C.
 *
 * The game's call shape:
 *   THPVideoDecode(&header{w,h}, &status, work_area, jpeg_bytes, &workdesc)
 *     -> returns an opaque state stored in the caller's work area,
 *   THPDec_80331340(state, Y, U, V) / THPDec_803313D0(state, Y, U, V, width)
 *     -> copies the decoded planes (640x480 or generic).
 *
 * Aurora reference symbols: `parse_headers`, `BitReader`, `decode_block`,
 * `inverse_dct`, `write_block` (aurora-reference/lib/dolphin/thp/THPDec.cpp).
 */
#include <dolphin/types.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

/* --------------------------------------------------------------- constants */

static const u8 thp_natural_order[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static const float thp_aan_scale[8] = {
    1.0f,          1.387039845f, 1.306562965f, 1.175875602f,
    1.0f,          0.785694958f, 0.541196100f, 0.275899379f,
};

#define THP_SQRT2 1.414213562f
#define THP_C2 1.847759065f
#define THP_C2_MINUS_C6 1.082392200f
#define THP_C2_PLUS_C6 2.613125930f
#define THP_C6 (THP_C2 - THP_C2_MINUS_C6)
#define THP_OUTPUT_BIAS (128.0f * 8.0f)

#define THP_MARKER_SOI 0xD8
#define THP_MARKER_SOF0 0xC0
#define THP_MARKER_DHT 0xC4
#define THP_MARKER_DQT 0xDB
#define THP_MARKER_DRI 0xDD
#define THP_MARKER_SOS 0xDA

/* ---------------------------------------------------------------- context */

typedef struct ThpQuantTable {
    float values[64];
    u8 valid;
} ThpQuantTable;

typedef struct ThpHuffmanTable {
    u8 counts[17];
    u16 first_codes[17];
    u16 symbol_offsets[17];
    u8 symbols[256];
    u8 valid;
} ThpHuffmanTable;

typedef struct ThpContext {
    ThpQuantTable quant[3];
    ThpHuffmanTable huff[4];
    u8 quant_selector[3];
    u8 dc_selector[3];
    u8 ac_selector[3];
    s32 predicted_dc[3];
    u16 width;
    u16 height;
    u16 restart_interval;
    u32 scan_offset;
} ThpContext;

/* The caller's work area starts with this header; the tiled Y/U/V planes
 * follow it (THPDec_8032FD40 reserves plane bytes after a 0x4028 scratch
 * allowance, which covers this struct with room to spare). */
typedef struct ThpState {
    u32 magic;
    u16 width;
    u16 height;
    u8* y;
    u8* u;
    u8* v;
} ThpState;

#define THP_STATE_MAGIC 0x54485031u /* "THP1" */

/* Mirrors lbmthp.c's private THPDec_8032FD40_Data. */
typedef struct ThpWorkData {
    s32 val0;
    u16 val1;
    u16 _pad;
    u8 val2;
} ThpWorkData;

/* ----------------------------------------------------------------- parser */

static u16 thp_be16(const u8* p)
{
    return (u16) ((p[0] << 8) | p[1]);
}

static int thp_parse_quant_tables(const u8* data, unsigned size,
                                  ThpContext* ctx)
{
    unsigned position = 0;

    while (position < size) {
        u8 descriptor;
        u8 id;
        int i;
        int row;
        int column;

        if (size - position < 65) {
            return 0;
        }
        descriptor = data[position++];
        id = descriptor & 15;
        if ((descriptor >> 4) != 0 || id >= 3) {
            return 0;
        }
        {
            float natural[64];
            for (i = 0; i < 64; i++) {
                natural[thp_natural_order[i]] = (float) data[position++];
            }
            for (row = 0; row < 8; row++) {
                for (column = 0; column < 8; column++) {
                    int index = row * 8 + column;
                    ctx->quant[id].values[index] =
                        (float) ((double) natural[index] *
                                 thp_aan_scale[row] * thp_aan_scale[column]);
                }
            }
        }
        ctx->quant[id].valid = 1;
    }
    return 1;
}

static int thp_parse_frame_header(const u8* data, unsigned size, ThpContext* ctx)
{
    int i;

    if (size < 6 || data[0] != 8) {
        return 0;
    }
    ctx->height = thp_be16(data + 1);
    ctx->width = thp_be16(data + 3);
    if (data[5] != 3) {
        return 0;
    }
    if (size < 15 || ctx->width == 0 || ctx->height == 0) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        u8 sampling = data[7 + i * 3];
        u8 table = data[8 + i * 3];
        if ((i == 0 && sampling != 0x22) || (i != 0 && sampling != 0x11) ||
            table >= 3)
        {
            return 0;
        }
        ctx->quant_selector[i] = table;
    }
    return 1;
}

static int thp_parse_huffman_tables(const u8* data, unsigned size,
                                    ThpContext* ctx)
{
    unsigned position = 0;

    while (position < size) {
        u8 descriptor;
        u8 table_class;
        u8 id;
        ThpHuffmanTable table;
        unsigned symbol_count = 0;
        u32 code = 0;
        u16 symbol_offset = 0;
        unsigned length;

        if (size - position < 17) {
            return 0;
        }
        descriptor = data[position++];
        table_class = (u8) (descriptor >> 4);
        id = (u8) (descriptor & 15);
        if (table_class > 1 || id > 1) {
            return 0;
        }
        memset(&table, 0, sizeof(table));
        for (length = 1; length <= 16; length++) {
            table.counts[length] = data[position++];
            symbol_count += table.counts[length];
        }
        if (symbol_count > 256 || symbol_count > size - position) {
            return 0;
        }
        memcpy(table.symbols, data + position, symbol_count);
        position += symbol_count;

        for (length = 1; length <= 16; length++) {
            u32 count = table.counts[length];
            if (code + count > (1u << length)) {
                return 0;
            }
            table.first_codes[length] = (u16) code;
            table.symbol_offsets[length] = symbol_offset;
            code = (code + count) << 1;
            symbol_offset = (u16) (symbol_offset + count);
        }
        table.valid = 1;
        ctx->huff[id * 2 + table_class] = table;
    }
    return 1;
}

static int thp_parse_scan_header(const u8* data, unsigned size, ThpContext* ctx)
{
    int i;

    if (size < 1 || data[0] != 3 || size < 10) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        u8 selectors = data[2 + i * 2];
        u8 dc_table = (u8) (selectors >> 4);
        u8 ac_table = (u8) (selectors & 15);
        if (dc_table > 1 || ac_table > 1 ||
            !ctx->huff[dc_table * 2].valid ||
            !ctx->huff[ac_table * 2 + 1].valid)
        {
            return 0;
        }
        ctx->dc_selector[i] = dc_table;
        ctx->ac_selector[i] = ac_table;
        ctx->predicted_dc[i] = 0;
    }
    if (data[7] != 0 || data[8] != 63 || data[9] != 0) {
        return 0;
    }
    return 1;
}

static int thp_read_segment(const u8* file, u32* position, const u8** segment,
                            unsigned* segment_size)
{
    u16 encoded_size = thp_be16(file + *position);
    if (encoded_size < 2) {
        return 0;
    }
    *position += 2;
    *segment = file + *position;
    *segment_size = encoded_size - 2;
    *position += encoded_size - 2;
    return 1;
}

static int thp_parse_headers(const u8* file, u32 file_limit, ThpContext* ctx)
{
    u32 position = 0;

    while (position < file_limit) {
        u8 prefix;
        u8 marker;
        const u8* segment;
        unsigned segment_size;

        prefix = file[position++];
        if (prefix != 0xFF) {
            return 0;
        }
        do {
            if (position >= file_limit) {
                return 0;
            }
            marker = file[position++];
        } while (marker == 0xFF);

        if (marker == THP_MARKER_SOI) {
            continue;
        }
        if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
            if (!thp_read_segment(file, &position, &segment, &segment_size)) {
                return 0;
            }
            continue;
        }
        switch (marker) {
        case THP_MARKER_SOF0:
            if (!thp_read_segment(file, &position, &segment, &segment_size) ||
                !thp_parse_frame_header(segment, segment_size, ctx))
            {
                return 0;
            }
            break;
        case THP_MARKER_DHT:
            if (!thp_read_segment(file, &position, &segment, &segment_size) ||
                !thp_parse_huffman_tables(segment, segment_size, ctx))
            {
                return 0;
            }
            break;
        case THP_MARKER_DQT:
            if (!thp_read_segment(file, &position, &segment, &segment_size) ||
                !thp_parse_quant_tables(segment, segment_size, ctx))
            {
                return 0;
            }
            break;
        case THP_MARKER_DRI:
            if (!thp_read_segment(file, &position, &segment, &segment_size) ||
                segment_size != 2)
            {
                return 0;
            }
            ctx->restart_interval = thp_be16(segment);
            break;
        case THP_MARKER_SOS:
            if (!thp_read_segment(file, &position, &segment, &segment_size) ||
                !thp_parse_scan_header(segment, segment_size, ctx))
            {
                return 0;
            }
            ctx->scan_offset = position;
            return 1;
        default:
            return 0;
        }
    }
    return 0;
}

/* --------------------------------------------------------------- bitstream */

typedef struct ThpBitReader {
    const u8* data;
    u32 bit_position;
} ThpBitReader;

static u32 thp_bits(ThpBitReader* reader, u8 count)
{
    u32 value = 0;
    u8 i;

    for (i = 0; i < count; i++) {
        u32 byte_position = reader->bit_position >> 3;
        value = (value << 1) |
                ((reader->data[byte_position] >>
                  (7 - (reader->bit_position & 7))) &
                 1);
        reader->bit_position++;
    }
    return value;
}

static void thp_bits_align(ThpBitReader* reader)
{
    reader->bit_position = (reader->bit_position + 7) & ~7u;
}

static int thp_decode_huffman(ThpBitReader* reader, const ThpHuffmanTable* table,
                              u8* symbol)
{
    u32 code = 0;
    unsigned length;

    for (length = 1; length <= 16; length++) {
        code = (code << 1) | thp_bits(reader, 1);
        {
            u32 first_code = table->first_codes[length];
            u32 count = table->counts[length];
            if (code >= first_code && code - first_code < count) {
                *symbol =
                    table->symbols[table->symbol_offsets[length] + code -
                                   first_code];
                return 1;
            }
        }
    }
    return 0;
}

static s32 thp_extend(u32 value, u8 bit_count)
{
    if (bit_count != 0 && value < (1u << (bit_count - 1))) {
        return (s32) value - (s32) (1u << bit_count) + 1;
    }
    return (s32) value;
}

static int thp_decode_block(ThpBitReader* reader, ThpContext* ctx,
                            unsigned component, s16* block)
{
    const ThpHuffmanTable* dc_table = &ctx->huff[ctx->dc_selector[component] * 2];
    const ThpHuffmanTable* ac_table =
        &ctx->huff[ctx->ac_selector[component] * 2 + 1];
    u8 bit_count = 0;
    size_t coefficient;

    memset(block, 0, 64 * sizeof(*block));
    if (!thp_decode_huffman(reader, dc_table, &bit_count) || bit_count > 16) {
        return 0;
    }
    ctx->predicted_dc[component] +=
        thp_extend(thp_bits(reader, bit_count), bit_count);
    block[0] = (s16) ctx->predicted_dc[component];

    coefficient = 1;
    while (coefficient < 64) {
        u8 run_and_size = 0;
        u8 run;
        if (!thp_decode_huffman(reader, ac_table, &run_and_size)) {
            return 0;
        }
        run = (u8) (run_and_size >> 4);
        bit_count = run_and_size & 15;
        if (bit_count == 0) {
            if (run == 15) {
                coefficient += 16;
                continue;
            }
            break;
        }
        coefficient += run;
        if (coefficient >= 64) {
            return 0;
        }
        block[thp_natural_order[coefficient]] =
            (s16) thp_extend(thp_bits(reader, bit_count), bit_count);
        coefficient++;
    }
    return 1;
}

/* -------------------------------------------------------------- transform */

typedef struct ThpEvenHalf {
    float out0, out1, out2, out3;
} ThpEvenHalf;

typedef struct ThpOddHalf {
    float out7, out6, out5, out4;
} ThpOddHalf;

static ThpEvenHalf thp_aan_even(float sum04, float dif04, float sum26,
                                float dif26)
{
    float rotated = fmaf(dif26, THP_SQRT2, -sum26);
    ThpEvenHalf half;
    half.out0 = sum04 + sum26;
    half.out1 = dif04 + rotated;
    half.out2 = dif04 - rotated;
    half.out3 = sum04 - sum26;
    return half;
}

static ThpOddHalf thp_aan_odd(float z10, float z11, float z12, float z13)
{
    ThpOddHalf half;
    float out7 = z11 + z13;
    float z5 = (z10 + z12) * THP_C2;
    float out6 = fmaf(-z10, THP_C2_PLUS_C6, z5) - out7;
    float out5 = fmaf(z11 - z13, THP_SQRT2, -out6);
    float out4 = fmaf(-z12, THP_C2_MINUS_C6, z5) - out5;
    half.out7 = out7;
    half.out6 = out6;
    half.out5 = out5;
    half.out4 = out4;
    return half;
}

static void thp_combine(const ThpEvenHalf* even, const ThpOddHalf* odd,
                        float* out)
{
    out[0] = even->out0 + odd->out7;
    out[1] = even->out1 + odd->out6;
    out[2] = even->out2 + odd->out5;
    out[3] = even->out3 + odd->out4;
    out[4] = even->out3 - odd->out4;
    out[5] = even->out2 - odd->out5;
    out[6] = even->out1 - odd->out6;
    out[7] = even->out0 - odd->out7;
}

static void thp_transform_row(const s16* coefficients, const float* quant,
                              float* out)
{
    float x0 = (float) coefficients[0] * quant[0];
    float x1 = (float) coefficients[1] * quant[1];
    float x2 = (float) coefficients[2] * quant[2];
    float x3 = (float) coefficients[3] * quant[3];
    float c4 = (float) coefficients[4];
    float c5 = (float) coefficients[5];
    float c6 = (float) coefficients[6];
    float c7 = (float) coefficients[7];
    ThpEvenHalf even =
        thp_aan_even(fmaf(c4, quant[4], x0), fmaf(-c4, quant[4], x0),
                     fmaf(c6, quant[6], x2), fmaf(-c6, quant[6], x2));
    ThpOddHalf odd = thp_aan_odd(fmaf(c5, quant[5], -x3),
                                 fmaf(c7, quant[7], x1),
                                 fmaf(-c7, quant[7], x1),
                                 fmaf(c5, quant[5], x3));
    thp_combine(&even, &odd, out);
}

static void thp_transform_row_low4(const float* x, float* out)
{
    float sum02 = x[0] + x[2];
    float dif02 = x[0] - x[2];
    ThpEvenHalf even;
    ThpOddHalf odd;
    even.out0 = sum02;
    even.out1 = fmaf(x[2], THP_SQRT2, dif02);
    even.out2 = fmaf(-x[2], THP_SQRT2, sum02);
    even.out3 = dif02;
    odd = thp_aan_odd(-x[3], x[1], x[1], x[3]);
    thp_combine(&even, &odd, out);
}

static void thp_transform_row_dc_ac(float dc, float ac, float* out)
{
    float a1 = fmaf(ac, THP_C2, -ac);
    float a2 = fmaf(ac, THP_SQRT2, -a1);
    float a3 = fmaf(-ac, THP_C6, a2);
    out[0] = dc + ac;
    out[1] = dc + a1;
    out[2] = dc + a2;
    out[3] = dc - a3;
    out[4] = dc + a3;
    out[5] = dc - a2;
    out[6] = dc - a1;
    out[7] = dc - ac;
}

static void thp_transform_column(const float* x, float* out)
{
    ThpEvenHalf even =
        thp_aan_even((x[0] + x[4]) + THP_OUTPUT_BIAS,
                     (x[0] - x[4]) + THP_OUTPUT_BIAS, x[2] + x[6], x[2] - x[6]);
    ThpOddHalf odd = thp_aan_odd(x[5] - x[3], x[1] + x[7], x[1] - x[7],
                                 x[5] + x[3]);
    thp_combine(&even, &odd, out);
}

static unsigned thp_row_extent(const s16* coefficients)
{
    unsigned extent = 8;
    while (extent > 0 && coefficients[extent - 1] == 0) {
        extent--;
    }
    return extent;
}

static void thp_inverse_dct(const s16* coefficients,
                            const ThpQuantTable* quantization, u8* output)
{
    float workspace[64];
    int row;
    int column;

    for (row = 0; row < 8; row++) {
        const s16* row_coefficients = &coefficients[row * 8];
        const float* row_quant = &quantization->values[row * 8];
        float* transformed = &workspace[row * 8];
        switch (thp_row_extent(row_coefficients)) {
        case 0:
        case 1:
            for (column = 0; column < 8; column++) {
                transformed[column] =
                    (float) row_coefficients[0] * row_quant[0];
            }
            break;
        case 2:
            thp_transform_row_dc_ac((float) row_coefficients[0] * row_quant[0],
                                    (float) row_coefficients[1] * row_quant[1],
                                    transformed);
            break;
        case 3:
        case 4: {
            float dequantized[8] = { 0 };
            for (column = 0; column < 4; column++) {
                dequantized[column] =
                    (float) row_coefficients[column] * row_quant[column];
            }
            thp_transform_row_low4(dequantized, transformed);
            break;
        }
        default:
            thp_transform_row(row_coefficients, row_quant, transformed);
            break;
        }
    }

    for (column = 0; column < 8; column++) {
        float input[8];
        float transformed[8];
        for (row = 0; row < 8; row++) {
            input[row] = workspace[row * 8 + column];
        }
        thp_transform_column(input, transformed);
        for (row = 0; row < 8; row++) {
            float scaled = transformed[row] * 0.125f;
            output[row * 8 + column] =
                scaled <= 0.0f ? 0 : scaled >= 255.0f ? 255 : (u8) scaled;
        }
    }
}

/* GX I8 tiling: 8x4 blocks, 32 bytes each (Aurora `write_block`). */
static void thp_write_block(u8* output, u16 width, u16 height, u16 block_x,
                            u16 block_y, const u8* pixels)
{
    size_t tiles_per_row = (width + 7) / 8;
    u16 row;

    for (row = 0; row < 8 && block_y + row < height; row++) {
        u16 column;
        for (column = 0; column < 8 && block_x + column < width; column++) {
            u16 x = (u16) (block_x + column);
            u16 y = (u16) (block_y + row);
            size_t tile = (size_t) (y / 4) * tiles_per_row + x / 8;
            size_t offset = tile * 32 + (size_t) (y & 3) * 8 + (x & 7);
            output[offset] = pixels[row * 8 + column];
        }
    }
}

static int thp_decode_block_to(ThpBitReader* reader, ThpContext* ctx,
                               unsigned component, u8* output, u16 width,
                               u16 height, u16 x, u16 y)
{
    s16 coefficients[64];
    u8 pixels[64];
    const ThpQuantTable* quantization;

    if (!thp_decode_block(reader, ctx, component, coefficients)) {
        return 0;
    }
    quantization = &ctx->quant[ctx->quant_selector[component]];
    if (!quantization->valid) {
        return 0;
    }
    thp_inverse_dct(coefficients, quantization, pixels);
    thp_write_block(output, width, height, x, y, pixels);
    return 1;
}

/* --------------------------------------------------------------- public API */

void THPInit(void) {}

s32 THPVideoDecode(const void* file, void* tileY, void* tileU, void* tileV,
                   void* workArea)
{
    const u8* jpeg = tileV;
    ThpState* state = tileU;
    ThpContext ctx;
    ThpBitReader bits;
    u8* y_plane;
    u8* u_plane;
    u8* v_plane;
    u16 chroma_width;
    u16 chroma_height;
    u16 mcu_columns;
    u16 mcu_rows;
    u16 mcu_x;
    u16 mcu_y;
    u32 restart_count = 0;
    size_t y_size;
    size_t uv_size;

    (void) file;
    (void) tileY;
    (void) workArea;
    if (jpeg == NULL || state == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    if (!thp_parse_headers(jpeg, 0x2000, &ctx)) {
        return -1;
    }

    chroma_width = (u16) ((ctx.width + 1) / 2);
    chroma_height = (u16) ((ctx.height + 1) / 2);
    y_size = (size_t) ctx.width * ctx.height;
    uv_size = (size_t) chroma_width * chroma_height;
    y_plane = (u8*) state + sizeof(*state);
    u_plane = y_plane + y_size;
    v_plane = u_plane + uv_size;

    state->magic = THP_STATE_MAGIC;
    state->width = ctx.width;
    state->height = ctx.height;
    state->y = y_plane;
    state->u = u_plane;
    state->v = v_plane;

    bits.data = jpeg;
    bits.bit_position = ctx.scan_offset * 8;

    mcu_columns = (u16) ((ctx.width + 15) / 16);
    mcu_rows = (u16) ((ctx.height + 15) / 16);
    for (mcu_y = 0; mcu_y < mcu_rows; mcu_y++) {
        for (mcu_x = 0; mcu_x < mcu_columns; mcu_x++) {
            u16 luma_x = (u16) (mcu_x * 16);
            u16 luma_y = (u16) (mcu_y * 16);
            if (!thp_decode_block_to(&bits, &ctx, 0, y_plane, ctx.width,
                                     ctx.height, luma_x, luma_y) ||
                !thp_decode_block_to(&bits, &ctx, 0, y_plane, ctx.width,
                                     ctx.height, (u16) (luma_x + 8), luma_y) ||
                !thp_decode_block_to(&bits, &ctx, 0, y_plane, ctx.width,
                                     ctx.height, luma_x, (u16) (luma_y + 8)) ||
                !thp_decode_block_to(&bits, &ctx, 0, y_plane, ctx.width,
                                     ctx.height, (u16) (luma_x + 8),
                                     (u16) (luma_y + 8)) ||
                !thp_decode_block_to(&bits, &ctx, 1, u_plane, chroma_width,
                                     chroma_height, (u16) (mcu_x * 8),
                                     (u16) (mcu_y * 8)) ||
                !thp_decode_block_to(&bits, &ctx, 2, v_plane, chroma_width,
                                     chroma_height, (u16) (mcu_x * 8),
                                     (u16) (mcu_y * 8)))
            {
                return -1;
            }
            if (ctx.restart_interval != 0 &&
                ++restart_count == ctx.restart_interval)
            {
                int i;
                thp_bits_align(&bits);
                restart_count = 0;
                for (i = 0; i < 3; i++) {
                    ctx.predicted_dc[i] = 0;
                }
            }
        }
    }
    return (s32) (uintptr_t) state;
}

static void thp_copy_planes(ThpState* state, void* out_y, void* out_u,
                            void* out_v)
{
    u16 chroma_width = (u16) ((state->width + 1) / 2);
    u16 chroma_height = (u16) ((state->height + 1) / 2);

    memcpy(out_y, state->y, (size_t) state->width * state->height);
    memcpy(out_u, state->u, (size_t) chroma_width * chroma_height);
    memcpy(out_v, state->v, (size_t) chroma_width * chroma_height);
}

void THPDec_80331340(s32 arg0, void* arg1, void* arg2, void* arg3)
{
    ThpState* state = (ThpState*) (uintptr_t) arg0;
    if (state == NULL || state->magic != THP_STATE_MAGIC) {
        return;
    }
    thp_copy_planes(state, arg1, arg2, arg3);
}

void THPDec_803313D0(s32 arg0, void* arg1, void* arg2, void* arg3, u32 arg4)
{
    ThpState* state = (ThpState*) (uintptr_t) arg0;
    (void) arg4;
    if (state == NULL || state->magic != THP_STATE_MAGIC) {
        return;
    }
    thp_copy_planes(state, arg1, arg2, arg3);
}

s32 THPDec_8032FD40(ThpWorkData* data, u16 num)
{
    s32 base;
    s32 extra;

    if (data == NULL) {
        return 0;
    }
    base = data->val0 + 0x4028;
    if (data->val2 != 4) {
        return 0;
    }
    extra = (data->val1 / 2) * (num / 2) * 2 + (data->val1 * num);
    return base + extra;
}

s32 THPDec_8032F8D4(u8* data, ThpWorkData* out)
{
    u8 h_sample[4] = { 0 };
    u8 v_sample[4] = { 0 };
    u8 component_id[4];
    u8 quantization_selector[4];
    u8 marker;
    u8 component_count;
    u8 i;
    u8 valid = 0;
    u32 j;
    u16 length;

    if (data == NULL || out == NULL) {
        return 0;
    }
    memset(out, 0, 0xC);

    {
        u8 soi0 = *data++;
        u8 soi1 = *data++;
        if (soi0 != 0xFF || soi1 != 0xD8) {
            return 0;
        }
    }

    for (;;) {
        if (*data++ != 0xFF) {
            return 0;
        }
        while ((s8) *data == 0xFF) {
            data++;
        }
        marker = *data++;

        if (marker == THP_MARKER_SOF0) {
            out->_pad = thp_be16(data + 3);
            out->val1 = thp_be16(data + 5);
            component_count = data[7];
            data += 8;
            if (component_count != 3) {
                return 0;
            }
            for (i = 0; i < component_count; i++) {
                u8 factors;
                component_id[i] = *data++;
                factors = *data++;
                h_sample[i] = (u8) (factors >> 4);
                v_sample[i] = (u8) (factors & 0xF);
                quantization_selector[i] = *data++;
            }
            if (h_sample[0] / h_sample[1] == 2 &&
                h_sample[0] / h_sample[2] == 2)
            {
                if (v_sample[0] / v_sample[1] == 2 &&
                    v_sample[0] / v_sample[2] == 2)
                {
                    out->val2 = 4;
                } else if (v_sample[0] == v_sample[1] &&
                           v_sample[0] == v_sample[2])
                {
                    out->val2 = 2;
                }
            } else if (h_sample[0] == h_sample[1] &&
                       h_sample[0] == h_sample[2])
            {
                if (v_sample[0] == v_sample[1] &&
                    v_sample[0] == v_sample[2])
                {
                    out->val2 = 1;
                }
            } else {
                return 0;
            }
        } else if (marker == 0xE0) {
            length = thp_be16(data);
            data += 2;
            for (i = 0; i < 5; i++) {
                component_count = *data++;
                if (component_count != "JFIF"[i]) {
                    return 0;
                }
            }
            valid = 1;
            for (j = 0; j < (u32) (length - 7); j++) {
                data++;
            }
        } else if (marker == THP_MARKER_SOS) {
            break;
        } else if (marker >= 0xC0 && marker <= 0xFE) {
            length = thp_be16(data);
            data += 2;
            for (j = 0; j < (u32) (length - 2); j++) {
                data++;
            }
        }
        if (out->val2 != 0 && valid != 0) {
            break;
        }
    }
    return 1;
}
