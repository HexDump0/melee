#ifndef MELEE_DECOMP_GX_HLE_H
#define MELEE_DECOMP_GX_HLE_H

/*
 * S2 GX high-level emulation for the compiled decompilation.
 *
 * The compiled engine (sysdolphin's MObj/TObj/PObj setup and dobj display
 * lists) calls the Dolphin SDK GX API.  This module implements that API with
 * real semantics: it records GX state, decodes the PObj display lists against
 * the GX vertex descriptors/arrays, applies the XF position/normal/texture
 * matrices and the GX channel lighting, and appends triangles to a per-frame
 * draw list.  gx_gl.c then renders the draw list with GLES3 (ADR-0009's
 * shader subset).
 */
#include <dolphin/gx.h>
#include <stddef.h>

#define GX_HLE_MAX_DRAWS 1024
#define GX_HLE_MAX_VERTS (1 << 18)
#define GX_HLE_MAX_TEXTURES 512
#define GX_HLE_MAX_TLUTS 32
#define GX_HLE_MAX_STAGES 8

typedef struct GxHleVertex {
    float clip[4];          /* post-projection clip space (GX y-down)     */
    float view[3];          /* position-matrix space (camera view)        */
    unsigned char color[4]; /* GX_VA_CLR0, zero when absent               */
    float uv[3][2];         /* generated TEXCOORD0/1/2                    */
    float ras[4];           /* channel 0 raster, alpha = channel specular */
    float ras1[4];          /* channel 1 raster, alpha = channel specular */
} GxHleVertex;

typedef struct GxHleTexture {
    const void* image;
    const void* palette; /* TLUT entries, NULL for direct-colour formats */
    unsigned int format;
    unsigned int palette_format;
    unsigned int palette_entries;
    unsigned short width;
    unsigned short height;
    unsigned char wrap_s, wrap_t;
    unsigned char mag_filt, min_filt;
    unsigned char mipmap;
    unsigned char bias_clamp, edge_lod;
    float lod_bias;
    float min_lod, max_lod;
    unsigned char anisotropy;
    int gl_texture; /* filled by gx_gl.c; 0 until uploaded */
} GxHleTexture;

typedef struct GxHleLight {
    GXColor color;
    float a[3];
    float k[3];
    float pos[3];
    float dir[3];
} GxHleLight;

typedef struct GxHleTevStage {
    unsigned char order_coord;
    unsigned char order_map;
    unsigned char order_chan;
    unsigned char color_a, color_b, color_c, color_d;
    unsigned char alpha_a, alpha_b, alpha_c, alpha_d;
    unsigned char color_op, color_bias, color_scale, color_clamp, color_reg;
    unsigned char alpha_op, alpha_bias, alpha_scale, alpha_clamp, alpha_reg;
    unsigned char ras_sel, tex_sel;
    unsigned char kc_sel, ka_sel;
    /* P-612 indirect texturing (GXSetTevIndirect/GXSetTevIndWarp). */
    unsigned char ind_enable, ind_stage, ind_format, ind_bias;
    unsigned char ind_mtx, ind_wrap_s, ind_wrap_t, ind_add_prev;
} GxHleTevStage;

typedef struct GxHleIndStage {
    unsigned char tex_coord;
    unsigned char tex_map;
    unsigned char scale_s, scale_t;
    float mtx[2][3];
    float scale; /* 2^scale_exp */
} GxHleIndStage;

typedef struct GxHleTexGen {
    unsigned char type; /* GXTexGenType */
    unsigned char src;  /* GXTexGenSrc */
    unsigned char normalize;
    unsigned char postmtx;
    unsigned char mtx_id;
} GxHleTexGen;

/* Everything the GL fragment stage needs; copied per draw. */
typedef struct GxHleDrawState {
    unsigned char cull_mode;
    unsigned char blend_type, blend_src, blend_dst, logic_op;
    unsigned char color_update, alpha_update;
    unsigned char z_enable, z_func, z_update, z_comp_loc;
    unsigned char dither;
    unsigned char dst_alpha_enable, dst_alpha;
    unsigned short scissor_x, scissor_y, scissor_w, scissor_h; /* EFB pixels */
    unsigned char alpha_comp0, alpha_ref0, alpha_op, alpha_comp1, alpha_ref1;
    unsigned char num_stages, num_texgens, num_chans;
    GxHleTevStage stages[GX_HLE_MAX_STAGES];
    GxHleTexGen texgen[8];
    float texmtx[10][3][4];
    float tev_color[4][4];  /* C0..C3, normalised */
    float tev_kcolor[4][4]; /* K0..K3 */
    unsigned char swap_table[4][4];
    /* Channel state keeps the four GX channels separate: 0/1 are COLOR0/
     * COLOR1 (the TEV raster sources), 2/3 are ALPHA0/ALPHA1.  HSD's
     * GX_COLOR0A0/GX_COLOR1A1 writes update both a colour slot and the
     * matching alpha slot's alpha component. */
    unsigned char ch_enable[4];
    unsigned char ch_amb_src[4], ch_mat_src[4];
    unsigned char ch_diff_fn[4], ch_attn_fn[4];
    unsigned int ch_light_mask[4];
    float ch_amb[4][4], ch_mat[4][4];
    GxHleLight lights[8];
    int texmap[8]; /* index into the frame texture table, -1 = none */
    unsigned char num_ind_stages;
    GxHleIndStage ind[4];
    float fog_start, fog_end;
    float fog_color[3];
    unsigned char fog_enable, fog_type;
    /* P-615: GXSetZTexture depth output (GX_ZT_DISABLE/ADD/REPLACE). */
    unsigned char ztex_op, ztex_fmt;
    float ztex_bias;
} GxHleDrawState;

/* P-615: GXCopyTex appears at its point in the command stream, so the GL
 * backend can capture the EFB before the next draw samples it. */
#define GX_HLE_DRAW_PRIM 0
#define GX_HLE_DRAW_COPY_TEX 1

typedef struct GxHleDraw {
    size_t first_vertex;
    size_t vertex_count;
    GxHleDrawState state;
    int kind;
    void* copy_dest;
    unsigned short copy_left, copy_top, copy_w, copy_h; /* EFB source rect */
    unsigned short copy_dst_w, copy_dst_h;              /* texture size */
    unsigned int copy_fmt;
    unsigned char copy_clear;
} GxHleDraw;

/* Call once before submitting a frame's GX commands. */
void gx_hle_begin_frame(void);

/* Drops the captured geometry while keeping the GX state, so HSD's internal
 * state caches stay coherent across a multi-pass capture. */
void gx_hle_discard_geometry(void);

/* Copies the captured frame out.  Returns 1 when a frame is available. */
int gx_hle_get_frame(const GxHleVertex** vertices, size_t* vertex_count,
                     const GxHleDraw** draws, size_t* draw_count,
                     GxHleTexture** textures, size_t* texture_count);

/* Stats for tests/logs. */
size_t gx_hle_display_list_count(void);
size_t gx_hle_primitive_count(void);
size_t gx_hle_skipped_count(void);
size_t gx_hle_degenerate_count(void);

/* Registers an asset buffer so gx_gl can bound texture decodes.  Multiple
 * buffers may be registered.  gx_hle_reset_assets drops all registrations
 * (the viewer does this when switching models). */
void gx_hle_register_asset(const void* base, size_t size);
void gx_hle_reset_assets(void);

/* Bytes remaining in the registered asset containing ptr, or (size_t) -1
 * when ptr is not inside one. */
size_t gx_hle_asset_remaining(const void* ptr);

#endif
