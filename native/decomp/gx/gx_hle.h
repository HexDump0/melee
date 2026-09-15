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
#define GX_HLE_MAX_TEXOBJS 128
#define GX_HLE_MAX_STAGES 8

typedef struct GxHleVertex {
    float clip[4];          /* post-projection clip space (GX y-down)     */
    float view[3];          /* position-matrix space (camera view)        */
    unsigned char color[4]; /* GX_VA_CLR0, zero when absent               */
    /* Generated TEXCOORD0..7.  GX_TG_MTX3x4 produces projective STQ;
     * keep q through rasterization so the fragment stage divides per pixel. */
    float uv[8][3];
    float nrm[3];           /* view-space normal (GPU channel evaluation) */
    float has_color;        /* 1 when GX_VA_CLR0 was present              */
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
    /* GXSetViewport is in EFB pixels with a top-left origin; the depth range
     * is the GX nearz/farz pair.  The shadow pass renders into a 256x256
     * sub-viewport, so this has to be applied per draw, not once per frame. */
    float viewport[4];  /* x, y, width, height */
    float depth_range[2]; /* near, far */
    unsigned char alpha_comp0, alpha_ref0, alpha_op, alpha_comp1, alpha_ref1;
    /* P-680 line/point raster sizes (GXSetLineWidth/GXSetPointSize). */
    unsigned char line_width, point_size;
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
    /* P-679: hardware fog coordinates from the SDK GXSetFog packing
     * (A/(B - depth) - C, perspective; the SDK always programs
     * c_proj_fsel's projection bit 0). */
    float fog_a, fog_b, fog_c;
    /* GXSetFogRangeAdj/GXInitFogAdjTable (12-bit table / 256). */
    unsigned char fog_adj_enable;
    unsigned short fog_adj_center;
    float fog_adj_k[10];
    /* P-615: GXSetZTexture depth output (GX_ZT_DISABLE/ADD/REPLACE). */
    unsigned char ztex_op, ztex_fmt;
    float ztex_bias;
    /* GXSetCopyClear registers, consumed only when GXCopyTex(clear) runs. */
    unsigned char copy_clear_color[4];
    unsigned int copy_clear_z;
} GxHleDrawState;

/* P-615: GXCopyTex appears at its point in the command stream, so the GL
 * backend can capture the EFB before the next draw samples it. */
#define GX_HLE_DRAW_PRIM 0
#define GX_HLE_DRAW_COPY_TEX 1

/* P-680: primitives in one draw snapshot can mix triangle strips, lines and
 * points (GXCallDisplayList groups, direct-mode sequences).  Each run is a
 * contiguous vertex range with one GL-compatible topology; a draw with
 * run_count == 0 renders as triangles (pre-P-680 captures/tests). */
#define GX_HLE_MAX_RUNS 16
#define GX_HLE_MODE_TRIANGLES 0
#define GX_HLE_MODE_LINES 1
#define GX_HLE_MODE_POINTS 2

typedef struct GxHleRun {
    size_t first_vertex;
    size_t vertex_count;
    unsigned char mode;
} GxHleRun;

typedef struct GxHleDraw {
    size_t first_vertex;
    size_t vertex_count;
    GxHleDrawState state;
    int kind;
    GxHleRun runs[GX_HLE_MAX_RUNS];
    int run_count;
    void* copy_dest;
    unsigned short copy_left, copy_top, copy_w, copy_h; /* EFB source rect */
    unsigned short copy_dst_w, copy_dst_h;              /* texture size */
    unsigned int copy_fmt;
    unsigned char copy_clear;
} GxHleDraw;

/* Call once before submitting a frame's GX commands. */
void gx_hle_begin_frame(void);

/* Optional decoded-texture cache hook, set by gx_gl (targets that render
 * call it for each GXInitTexObj so CPU-updated images are re-decoded). */
void gx_hle_set_texture_invalidate_hook(void (*fn)(const void* image));

/* Drops the captured geometry while keeping the GX state, so HSD's internal
 * state caches stay coherent across a multi-pass capture. */
void gx_hle_discard_geometry(void);

/* Wipes the captured GX register state (not just the frame capture).  Do not
 * call at a normal frame boundary; pair with HSD_StateInvalidate(-1) when the
 * compiled engine is running. */
void gx_hle_reset_state(void);

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
