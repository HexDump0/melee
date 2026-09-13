/*
 * S2 GLES3 renderer for the GX HLE frame.
 *
 * Evaluates the captured GX state: per-vertex channel raster (computed by
 * gx_hle), the TEV combiner (up to 4 stages, constants, swap tables, alpha
 * test), textures (GX tiled data through the prototype decoder,
 * native/gx/texture.c) and the GX blend/depth/cull state.
 *
 * Shader bodies are in the ES3 subset of ADR-0009; EGL + GLESv2 are used
 * instead of SDL/desktop GL because the compiled product target is 32-bit and
 * only 32-bit Mesa EGL/GLESv2 are available on the reference machine.
 */
#include "decomp/gx/gx_gl.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decomp/gx/gx_hle.h"
#include "gx/texture.h"

#define MAX_GL_TEXTURES 256
#define MAX_DYNAMIC_COPIES 8
#define MAX_TEV_STAGES 8

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

typedef struct {
    const void* image;
    const void* palette;
    unsigned int format;
    unsigned int palette_format;
    unsigned int palette_entries;
    unsigned short width;
    unsigned short height;
    unsigned char wrap_s, wrap_t;
    unsigned char mag_filt, min_filt;
    unsigned char mipmap;
    unsigned char anisotropy;
    float lod_bias;
    float min_lod, max_lod;
    unsigned int last_used;
    GLuint name;
} GlTextureCache;

typedef struct {
    const void* image;
    unsigned short width;
    unsigned short height;
    GLuint name;
    GLuint framebuffer;
} GlDynamicCopy;

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;
static int gl_width = 640;
static int gl_height = 480;
static float clear_color[4] = { 0.05f, 0.06f, 0.09f, 1.0f };

static GLuint program;
static GLint u_tex[3];
static GLint u_tev_color;
static GLint u_tev_kcolor;
static GLint u_tev_order;
static GLint u_tev_cin;
static GLint u_tev_cop;
static GLint u_tev_ain;
static GLint u_tev_aop;
static GLint u_tev_sel;
static GLint u_tev_reg;
static GLint u_swap;
static GLint u_stages;
static GLint u_alpha_test;
static GLint u_acomp;
static GLint u_aref;
static GLint u_aop;
static GLint u_fog_enable;
static GLint u_fog_type;
static GLint u_fog_start;
static GLint u_fog_end;
static GLint u_fog_color;
static GLint u_tex_enable;
static GLint u_ras_flat;
static GLuint ztex_program;
static GLint u_ztex_sampler;
static GLint u_ztex_op_loc;
static GLint u_ztex_bias_loc;
static GLint u_tex_lod_bias;
static GLint u_tex_dynamic_i4;
static GLint u_dst_alpha_enable;
static GLint u_dst_alpha;
static GLint u_ch_enable;
static GLint u_ch_amb_src;
static GLint u_ch_mat_src;
static GLint u_ch_diff_fn;
static GLint u_ch_light_mask;
static GLint u_ch_amb;
static GLint u_ch_mat;
static GLint u_light_pos;
static GLint u_light_color;
static GLint u_light_a;
static GLint u_light_k;
static GLint u_light_dir;
static GLint u_point_size;
/* P-672 indirect texturing (GXSetTevIndirect/GXSetIndTex*) */
static GLint u_ind_order;   /* ivec2[4]: coord, map */
static GLint u_ind_scale;   /* vec2[4]: 1/2^n for GX_ITS_1..256 */
static GLint u_ind_mtx0;    /* vec4[4]: row0.xyz, 2^scale_exp */
static GLint u_ind_mtx1;    /* vec4[4]: row1.xyz */
static GLint u_tev_ind_a;   /* ivec4[8]: stage, format, bias, mtx */
static GLint u_tev_ind_b;   /* ivec4[8]: wrap_s, wrap_t, add_prev, enable */
static GLint u_num_ind_stages;
static GLint u_tex_size;    /* vec3[3]: destination map sizes */
static GLint u_coord_srtg;  /* ivec3: toon coord samples the lit raster */

static GlTextureCache tex_cache[MAX_GL_TEXTURES];
static size_t tex_cache_count;
static GlDynamicCopy dynamic_copies[MAX_DYNAMIC_COPIES];
static size_t dynamic_copy_count;
static unsigned int tex_clock;
static int aniso_supported;
static int gpu_efb_copies;

static GLuint vertex_vao;
static GLuint vertex_vbo;
static size_t vertex_vbo_capacity;

static GxGlOptions gl_options = { 1, 1, -1, -1, 0, 0, 0 };

static void configure_vertex_layout(void)
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(GxHleVertex),
                          (const void*) offsetof(GxHleVertex, clip));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(GxHleVertex),
                          (const void*) offsetof(GxHleVertex, color));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GxHleVertex),
                          (const void*) offsetof(GxHleVertex, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GxHleVertex),
                          (const void*) (offsetof(GxHleVertex, uv) +
                                         2 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(GxHleVertex),
                          (const void*) offsetof(GxHleVertex, nrm));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(GxHleVertex),
                          (const void*) offsetof(GxHleVertex, has_color));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(GxHleVertex),
                          (const void*) offsetof(GxHleVertex, view));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, sizeof(GxHleVertex),
                          (const void*) (offsetof(GxHleVertex, uv) +
                                         4 * sizeof(float)));
}

/* --------------------------------------------------------------- shaders */

/* Channel evaluation (GX_SetupChannelMode lighting) runs here on the GPU:
 * it used to be per-vertex C in gx_hle.c and dominated the match frame time.
 * The formulas mirror the deleted channel_raster/channel_alpha exactly
 * (GX_DF_*, GX_SRC_REG/VTX, the XF light equation and the Dolphin specular
 * attenuation function). */
static const char* VERTEX_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "layout(location=0) in vec4 a_clip;\n"
    "layout(location=1) in vec4 a_color;\n"
    "layout(location=2) in vec2 a_uv0;\n"
    "layout(location=3) in vec2 a_uv1;\n"
    "layout(location=4) in vec3 a_nrm;\n"
    "layout(location=5) in float a_has_color;\n"
    "layout(location=6) in vec3 a_view;\n"
    "layout(location=7) in vec2 a_uv2;\n"
    "uniform int u_ch_enable[4];\n"
    "uniform int u_ch_amb_src[4];\n"
    "uniform int u_ch_mat_src[4];\n"
    "uniform int u_ch_diff_fn[4];\n"
    "uniform int u_ch_light_mask[4];\n"
    "uniform vec4 u_ch_amb[4];\n"
    "uniform vec4 u_ch_mat[4];\n"
    "uniform vec4 u_light_pos[8];\n"
    "uniform vec4 u_light_color[8];\n"
    "uniform vec4 u_light_a[8];\n"
    "uniform vec4 u_light_k[8];\n"
    "uniform vec4 u_light_dir[8];\n"
    "uniform float u_point_size;\n"
    "out vec4 v_color;\n"
    "out vec2 v_uv0;\n"
    "out vec2 v_uv1;\n"
    "out vec2 v_uv2;\n"
    "out vec4 v_ras0;\n"
    "out vec4 v_ras1;\n"
    "out float v_dist;\n"
    "bool light_infinite(vec3 p) { return dot(p, p) > 1.0e10; }\n"
    "void light_view_dir(int i, vec3 view, out vec3 ldir, out float attn) {\n"
    "    vec3 p = u_light_pos[i].xyz;\n"
    "    attn = 1.0;\n"
    "    if (light_infinite(p)) {\n"
    "        float len = length(p);\n"
    "        ldir = len > 0.0 ? p / len : vec3(0.0, 0.0, 1.0);\n"
    "    } else {\n"
    "        vec3 d = p - view;\n"
    "        float dist = length(d);\n"
    "        ldir = dist > 0.0 ? d / dist : vec3(0.0, 0.0, 1.0);\n"
    "        float dd = dist * dist;\n"
    "        float den = u_light_k[i].x + u_light_k[i].y * dist +\n"
    "                    u_light_k[i].z * dd;\n"
    "        attn = (u_light_a[i].x + u_light_a[i].y * dist +\n"
    "                u_light_a[i].z * dd) / den;\n"
    "    }\n"
    "}\n"
    "float diffuse_term(int fn, float ndl) {\n"
    "    if (fn == 0) return 1.0;\n"
    "    if (fn == 1) return ndl;\n"
    "    return ndl > 0.0 ? ndl : 0.0;\n"
    "}\n"
    "float quantize(float v) {\n"
    "    return floor(clamp(v, 0.0, 1.0) * 255.0 + 0.5) / 255.0;\n"
    "}\n"
    "float channel_alpha(int ch, vec4 color, bool has_color) {\n"
    "    int ac = ch + 2;\n"
    "    float mat = (u_ch_mat_src[ac] == 1 && has_color) ? color.a\n"
    "                                                     : u_ch_mat[ac].a;\n"
    "    int mask = u_ch_light_mask[ac];\n"
    "    if (u_ch_enable[ac] == 0 || mask == 0) return mat;\n"
    "    float lacc = (u_ch_amb_src[ac] == 1 && has_color) ? color.a\n"
    "                                                      : u_ch_amb[ac].a;\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        if ((mask & (1 << i)) == 0) continue;\n"
    "        vec3 ldir; float attn;\n"
    "        light_view_dir(i, a_view, ldir, attn);\n"
    "        float ndl = dot(a_nrm, ldir);\n"
    "        lacc += diffuse_term(u_ch_diff_fn[ac], ndl) * attn *\n"
    "                u_light_color[i].a;\n"
    "    }\n"
    "    return mat * lacc;\n"
    "}\n"
    "vec4 channel_raster(int ch, vec4 color, bool has_color) {\n"
    "    float alpha = channel_alpha(ch, color, has_color);\n"
    "    int mask = u_ch_light_mask[ch];\n"
    "    if (ch == 1 && mask != 0) {\n"
    "        /* GX_AF_SPEC (the SDK's default attn_fn value 0, used by HSD's\n"
    "         * specular channel): attn = max(0, a(t)/k(t)) with\n"
    "         * t = N.H gated by N.L, and the diffuse field forced to DF_NONE\n"
    "         * (GXSetChanCtrl writes 0 into register bits [7:9] for spec).\n"
    "         * The light colour is accumulated per channel, not greyed\n"
    "         * (Dolphin LightingShaderGen AttenuationFunc::Spec). */\n"
    "        vec3 spec = vec3(0.0);\n"
    "        for (int i = 0; i < 8; i++) {\n"
    "            if ((mask & (1 << i)) == 0) continue;\n"
    "            vec3 ldir; float attn;\n"
    "            light_view_dir(i, a_view, ldir, attn);\n"
    "            vec3 h;\n"
    "            float hn = length(u_light_dir[i].xyz);\n"
    "            if (hn > 1.0e-6) {\n"
    "                h = u_light_dir[i].xyz / hn;\n"
    "            } else {\n"
    "                h = ldir + vec3(0.0, 0.0, 1.0);\n"
    "                float hl = length(h);\n"
    "                if (hl > 0.0) h = h / hl;\n"
    "            }\n"
    "            float nh = dot(a_nrm, h);\n"
    "            nh = nh < 0.0 ? 0.0 : nh;\n"
    "            if (dot(a_nrm, ldir) < 0.0) nh = 0.0;\n"
    "            float num = u_light_a[i].x + u_light_a[i].y * nh +\n"
    "                        u_light_a[i].z * nh * nh;\n"
    "            float den = u_light_k[i].x + u_light_k[i].y * nh +\n"
    "                        u_light_k[i].z * nh * nh;\n"
    "            if (den != 0.0) {\n"
    "                spec += u_light_color[i].rgb * max(0.0, num / den);\n"
    "            }\n"
    "        }\n"
    "        return vec4(quantize(spec.r), quantize(spec.g), quantize(spec.b),\n"
    "                    alpha);\n"
    "    }\n"
    "    vec3 amb = (u_ch_amb_src[ch] == 1 && has_color) ? color.rgb\n"
    "                                                   : u_ch_amb[ch].rgb;\n"
    "    vec3 mat = (u_ch_mat_src[ch] == 1 && has_color) ? color.rgb\n"
    "                                                   : u_ch_mat[ch].rgb;\n"
    "    if (u_ch_enable[ch] == 0 || mask == 0) {\n"
    "        return vec4(quantize(mat.r), quantize(mat.g), quantize(mat.b),\n"
    "                    alpha);\n"
    "    }\n"
    "    vec3 lacc = amb;\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        if ((mask & (1 << i)) == 0) continue;\n"
    "        vec3 ldir; float attn;\n"
    "        light_view_dir(i, a_view, ldir, attn);\n"
    "        float ndl = dot(a_nrm, ldir);\n"
    "        float t = diffuse_term(u_ch_diff_fn[ch], ndl) * attn;\n"
    "        lacc += t * u_light_color[i].rgb;\n"
    "    }\n"
    "    return vec4(quantize(mat.r * lacc.r), quantize(mat.g * lacc.g),\n"
    "                quantize(mat.b * lacc.b), alpha);\n"
    "}\n"
    "void main() {\n"
    "    bool has_color = a_has_color > 0.5;\n"
    "    gl_Position = a_clip;\n"
    "    gl_PointSize = u_point_size;\n"
    "    v_color = a_color;\n"
    "    v_uv0 = a_uv0;\n"
    "    v_uv1 = a_uv1;\n"
    "    v_uv2 = a_uv2;\n"
    "    v_ras0 = channel_raster(0, a_color, has_color);\n"
    "    v_ras1 = channel_raster(1, a_color, has_color);\n"
    "    v_dist = -a_view.z;\n"
    "}\n";

static const char* ZTEX_FRAGMENT_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D u_ztex;\n"
    "uniform int u_ztex_op;\n"
    "uniform float u_ztex_bias;\n"
    "in vec2 v_uv0;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    float z = texture(u_ztex, v_uv0).r;\n"
    "    if (u_ztex_op == 2) gl_FragDepth = clamp(z + u_ztex_bias, 0.0, 1.0);\n"
    "    else gl_FragDepth = clamp(gl_FragCoord.z + z + u_ztex_bias, 0.0, 1.0);\n"
    "    frag = vec4(0.0);\n"
    "}\n";

static const char* FRAGMENT_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform sampler2D u_tex0;\n"
    "uniform sampler2D u_tex1;\n"
    "uniform sampler2D u_tex2;\n"
    "uniform vec4 u_tev_color[4];\n"
    "uniform vec4 u_tev_kcolor[4];\n"
    "/* order: x=coord 0..7/255, y=map 0..7/255, z=channel, w=unused */\n"
    "uniform ivec4 u_tev_order[8];\n"
    "/* color in: a,b,c,d */\n"
    "uniform ivec4 u_tev_cin[8];\n"
    "/* color op: op,bias,scale,clamp */\n"
    "uniform ivec4 u_tev_cop[8];\n"
    "uniform ivec4 u_tev_ain[8];\n"
    "uniform ivec4 u_tev_aop[8];\n"
    "/* sel: ras_sel, tex_sel, kc_sel, ka_sel */\n"
    "uniform ivec4 u_tev_sel[8];\n"
    "/* reg: color_reg, alpha_reg */\n"
    "uniform ivec2 u_tev_reg[8];\n"
    "uniform ivec4 u_swap[16];\n"
    "uniform int u_stages;\n"
    "uniform int u_alpha_test;\n"
    "uniform ivec2 u_acomp;\n"
    "uniform vec2 u_aref;\n"
    "uniform int u_aop;\n"
    "uniform int u_fog_enable;\n"
    "uniform int u_fog_type;\n"
    "uniform float u_fog_start;\n"
    "uniform float u_fog_end;\n"
    "uniform vec3 u_fog_color;\n"
    "uniform int u_tex_enable;\n"
    "uniform vec3 u_tex_lod_bias;\n"
    "uniform ivec3 u_tex_dynamic_i4;\n"
    "uniform int u_dst_alpha_enable;\n"
    "uniform float u_dst_alpha;\n"
    "uniform int u_ras_flat;\n"
    "uniform ivec2 u_ind_order[4];\n"
    "uniform vec2 u_ind_scale[4];\n"
    "uniform vec4 u_ind_mtx0[4];\n"
    "uniform vec4 u_ind_mtx1[4];\n"
    "uniform ivec4 u_tev_ind_a[8];\n"
    "uniform ivec4 u_tev_ind_b[8];\n"
    "uniform int u_num_ind_stages;\n"
    "uniform vec3 u_tex_size[3];\n"
    "uniform ivec3 u_coord_srtg;\n"
    "in vec4 v_color;\n"
    "in vec2 v_uv0;\n"
    "in vec2 v_uv1;\n"
    "in vec2 v_uv2;\n"
    "in vec4 v_ras0;\n"
    "in vec4 v_ras1;\n"
    "in float v_dist;\n"
    "out vec4 frag_color;\n"
    "vec4 swap4(vec4 v, ivec4 t) {\n"
    "    return vec4(v[t.x], v[t.y], v[t.z], v[t.w]);\n"
    "}\n"
    "float kfrac(int k) {\n"
    "    /* GX_TEV_KCSEL/KASEL_1, _7_8, _3_4, _5_8, _1_2, _3_8, _1_4, _1_8 */\n"
    "    return (k == 0) ? 1.0 : (k == 1) ? 0.875 : (k == 2) ? 0.75 :\n"
    "           (k == 3) ? 0.625 : (k == 4) ? 0.5 : (k == 5) ? 0.375 :\n"
    "           (k == 6) ? 0.25 : 0.125;\n"
    "}\n"
    "vec4 kreg(int i, vec4 k0, vec4 k1, vec4 k2, vec4 k3) {\n"
    "    int base = i & 3;\n"
    "    return (base == 0) ? k0 : (base == 1) ? k1 : (base == 2) ? k2 : k3;\n"
    "}\n"
    "vec4 konst_color(int kc, vec4 k0, vec4 k1, vec4 k2, vec4 k3) {\n"
    "    if (kc <= 7) return vec4(kfrac(kc));\n"
    "    if (kc <= 15) return kreg(kc - 12, k0, k1, k2, k3); /* K0..K3 */\n"
    "    {\n"
    "        /* 0x10..0x1F: K0..K3 component (R,G,B,A) replicated to rgb */\n"
    "        int comp = (kc >> 2) & 3;\n"
    "        vec4 k = kreg(kc & 3, k0, k1, k2, k3);\n"
    "        float v = (comp == 0) ? k.r : (comp == 1) ? k.g :\n"
    "                  (comp == 2) ? k.b : k.a;\n"
    "        return vec4(v);\n"
    "    }\n"
    "}\n"
    "float konst_alpha(int ka, vec4 k0, vec4 k1, vec4 k2, vec4 k3) {\n"
    "    if (ka <= 7) return kfrac(ka);\n"
    "    {\n"
    "        int comp = (ka >> 2) & 3;\n"
    "        vec4 k = kreg(ka & 3, k0, k1, k2, k3);\n"
    "        return (comp == 0) ? k.r : (comp == 1) ? k.g :\n"
    "               (comp == 2) ? k.b : k.a;\n"
    "    }\n"
    "}\n"
    "vec3 carg(int a, vec4 prev, vec4 c0, vec4 c1, vec4 c2, vec4 k, vec4 tex,"
    " vec4 ras, vec4 vcol) {\n"
    "    if (a == 0) return prev.rgb;\n"
    "    if (a == 1) return vec3(prev.a);\n"
    "    if (a == 2) return c0.rgb;\n"
    "    if (a == 3) return vec3(c0.a);\n"
    "    if (a == 4) return c1.rgb;\n"
    "    if (a == 5) return vec3(c1.a);\n"
    "    if (a == 6) return c2.rgb;\n"
    "    if (a == 7) return vec3(c2.a);\n"
    "    if (a == 8) return tex.rgb;\n"
    "    if (a == 9) return vec3(tex.a);\n"
    "    if (a == 10) return ras.rgb;\n"
    "    if (a == 11) return vec3(ras.a);\n"
    "    if (a == 12) return vec3(1.0);\n"
    "    if (a == 13) return vec3(0.5);\n"
    "    if (a == 14) return k.rgb;\n"
    "    return vec3(0.0);\n"
    "}\n"
    "float aarg(int a, vec4 prev, vec4 c0, vec4 c1, vec4 c2, vec4 k, vec4 tex,"
    " vec4 ras, vec4 vcol) {\n"
    "    if (a == 0) return prev.a;\n"
    "    if (a == 1) return c0.a;\n"
    "    if (a == 2) return c1.a;\n"
    "    if (a == 3) return c2.a;\n"
    "    if (a == 4) return tex.a;\n"
    "    if (a == 5) return ras.a;\n"
    "    if (a == 6) return k.a;\n"
    "    return 0.0;\n"
    "}\n"
    "float scale_factor(int s) {\n"
    "    if (s == 1) return 2.0;\n"
    "    if (s == 2) return 4.0;\n"
    "    if (s == 3) return 0.5;\n"
    "    return 1.0;\n"
    "}\n"
    "float bias_value(int b) {\n"
    "    if (b == 1) return 0.5;\n"
    "    if (b == 2) return -0.5;\n"
    "    return 0.0;\n"
    "}\n"
    "bool gx_compare(int func, float a, float ref) {\n"
    "    if (func == 0) return false;\n"
    "    if (func == 1) return a < ref;\n"
    "    if (func == 2) return a == ref;\n"
    "    if (func == 3) return a <= ref;\n"
    "    if (func == 4) return a > ref;\n"
    "    if (func == 5) return a != ref;\n"
    "    if (func == 6) return a >= ref;\n"
    "    return true;\n"
    "}\n"
    "/* P-672: the TEV texture coordinate, with the toon rule that a GX_TG_SRTG\n"
    " * coordinate samples the rasterized (lit) colour.  Only coords 0..2 are\n"
    " * carried as varyings; 3..7 fold onto 0 (resolve_stage_coords). */\n"
    "vec2 gx_coord_uv(int c) {\n"
    "    if (c >= 0 && c < 3 && u_coord_srtg[c] != 0) return v_ras0.xy;\n"
    "    if (c == 1) return v_uv1;\n"
    "    if (c == 2) return v_uv2;\n"
    "    return v_uv0;\n"
    "}\n"
    "vec4 gx_sample_map(int map, vec2 uv) {\n"
    "    if (map == 1) return texture(u_tex1, uv, u_tex_lod_bias.y);\n"
    "    if (map == 2) return texture(u_tex2, uv, u_tex_lod_bias.z);\n"
    "    return texture(u_tex0, uv, u_tex_lod_bias.x);\n"
    "}\n"
    "vec2 gx_tex_size(int map) {\n"
    "    vec2 s = u_tex_size[0].xy;\n"
    "    if (map == 1) s = u_tex_size[1].xy;\n"
    "    else if (map == 2) s = u_tex_size[2].xy;\n"
    "    return max(s, vec2(1.0));\n"
    "}\n"
    "/* GXIndTexWrap: OFF, 256, 128, 64, 32, 16, 0.  The hardware wraps the\n"
    " * texel coordinate; the offset conversion keeps us in normalized UVs. */\n"
    "float gx_ind_wrap(float v, int mode, float size) {\n"
    "    if (mode == 0) return v;\n"
    "    if (mode >= 6) return 0.0;\n"
    "    float span = (mode == 1) ? 256.0 : (mode == 2) ? 128.0 :\n"
    "                 (mode == 3) ? 64.0 : (mode == 4) ? 32.0 : 16.0;\n"
    "    return mod(v * size, span) / size;\n"
    "}\n"
    "void main() {\n"
    "    /* GXTevRegID is PREV=0, REG0=1, REG1=2, REG2=3. */\n"
    "    vec4 c0 = u_tev_color[1];\n"
    "    vec4 c1 = u_tev_color[2];\n"
    "    vec4 c2 = u_tev_color[3];\n"
    "    vec4 prev = v_ras0;\n"
    "    vec2 t_ind = vec2(0.0);\n"
    "    for (int i = 0; i < 8; ++i) {\n"
    "        if (i >= u_stages) break;\n"
    "        ivec4 ord = u_tev_order[i];\n"
    "        vec4 tex = vec4(1.0);\n"
    "        /* P-672: indirect texturing.  The reference stream (lbrefract.c)\n"
    "         * uses a static GX_ITM_0 matrix with ITF_8/ITB_ST/ITW_OFF.  The\n"
    "         * offset is evaluated in destination texels and converted to\n"
    "         * this renderer's normalized UV domain (Aurora shader.cpp\n"
    "         * `indirectOffsetTexel` / `size_bias`). */\n"
    "        ivec4 ind_a = u_tev_ind_a[i];\n"
    "        ivec4 ind_b = u_tev_ind_b[i];\n"
    "        vec2 uv = gx_coord_uv(ord.x);\n"
    "        if (ind_b.w != 0 && ind_a.x < u_num_ind_stages) {\n"
    "            ivec2 io = u_ind_order[ind_a.x];\n"
    "            vec2 iuv = gx_coord_uv(io.x) / u_ind_scale[ind_a.x];\n"
    "            vec3 s = gx_sample_map(io.y, iuv).abg * 255.0;\n"
    "            ivec3 coord = ivec3(floor(s + 0.5));\n"
    "            if (ind_a.y == 1) coord >>= 3;\n"
    "            else if (ind_a.y == 2) coord >>= 4;\n"
    "            else if (ind_a.y == 3) coord >>= 5;\n"
    "            int ind_bias = (ind_a.y == 0) ? -128 : 1;\n"
    "            if ((ind_a.z & 1) != 0) coord.x += ind_bias;\n"
    "            if ((ind_a.z & 2) != 0) coord.y += ind_bias;\n"
    "            if ((ind_a.z & 4) != 0) coord.z += ind_bias;\n"
    "            vec2 off = vec2(0.0);\n"
    "            if (ind_a.w >= 1 && ind_a.w <= 3) {\n"
    "                vec4 r0 = u_ind_mtx0[ind_a.w - 1];\n"
    "                vec4 r1 = u_ind_mtx1[ind_a.w - 1];\n"
    "                off = vec2(dot(r0.xyz, vec3(coord)),\n"
    "                           dot(r1.xyz, vec3(coord))) *\n"
    "                      r0.w / gx_tex_size(ord.y);\n"
    "            }\n"
    "            vec2 dsz = gx_tex_size(ord.y);\n"
    "            uv.x = gx_ind_wrap(uv.x, ind_b.x, dsz.x);\n"
    "            uv.y = gx_ind_wrap(uv.y, ind_b.y, dsz.y);\n"
    "            uv = uv + off;\n"
    "            if (ind_b.z != 0) t_ind += uv;\n"
    "            else t_ind = uv;\n"
    "            uv = t_ind;\n"
    "        }\n"
    "        if (u_tex_enable != 0 && ord.y != 255) {\n"
    "            tex = gx_sample_map(ord.y, uv);\n"
    "            int dynamic_i4 = (ord.y == 0) ? u_tex_dynamic_i4.x :\n"
    "                             (ord.y == 1) ? u_tex_dynamic_i4.y :\n"
    "                             (ord.y == 2) ? u_tex_dynamic_i4.z : 0;\n"
    "            if (dynamic_i4 != 0) {\n"
    "                float intensity = floor(clamp(tex.r, 0.0, 1.0) * 255.0 / 16.0) / 15.0;\n"
    "                tex = vec4(intensity, intensity, intensity, 1.0);\n"
    "            }\n"
    "        }\n"
    "        ivec4 sel = u_tev_sel[i];\n"
    "        vec4 ras = (ord.z == 1 || ord.z == 5) ? v_ras1 : v_ras0;\n"
    "        /* Viewer lights-off: channel 0 unlit white, but the specular\n"
    "         * channel must go black or the spec map is added at full\n"
    "         * strength (the prototype's u_lighting=0 does both). */\n"
    "        if (u_ras_flat != 0) {\n"
    "            ras = (ord.z == 1 || ord.z == 5) ? vec4(0.0, 0.0, 0.0, 1.0)\n"
    "                                             : vec4(1.0);\n"
    "        }\n"
    "        tex = swap4(tex, u_swap[sel.y]);\n"
    "        ras = swap4(ras, u_swap[sel.x]);\n"
    "        vec4 k = konst_color(sel.z, u_tev_kcolor[0], u_tev_kcolor[1],\n"
    "                             u_tev_kcolor[2], u_tev_kcolor[3]);\n"
    "        k.a = konst_alpha(sel.w, u_tev_kcolor[0], u_tev_kcolor[1],\n"
    "                          u_tev_kcolor[2], u_tev_kcolor[3]);\n"
    "        ivec4 cin = u_tev_cin[i];\n"
    "        vec3 a = carg(cin.x, prev, c0, c1, c2, k, tex, ras, v_color);\n"
    "        vec3 b = carg(cin.y, prev, c0, c1, c2, k, tex, ras, v_color);\n"
    "        vec3 c = carg(cin.z, prev, c0, c1, c2, k, tex, ras, v_color);\n"
    "        vec3 d = carg(cin.w, prev, c0, c1, c2, k, tex, ras, v_color);\n"
    "        vec3 lerp = a + c * (b - a);\n"
    "        ivec4 cop = u_tev_cop[i];\n"
    "        vec3 rgb = (cop.x == 1) ? (d - lerp) : (d + lerp);\n"
    "        rgb = rgb * scale_factor(cop.z) + bias_value(cop.y);\n"
    "        if (cop.w != 0) rgb = clamp(rgb, 0.0, 1.0);\n"
    "        ivec4 ain = u_tev_ain[i];\n"
    "        float aa = aarg(ain.x, prev, c0, c1, c2, k, tex, ras, v_color);\n"
    "        float ab = aarg(ain.y, prev, c0, c1, c2, k, tex, ras, v_color);\n"
    "        float ac = aarg(ain.z, prev, c0, c1, c2, k, tex, ras, v_color);\n"
    "        float ad = aarg(ain.w, prev, c0, c1, c2, k, tex, ras, v_color);\n"
    "        float alerp = aa + ac * (ab - aa);\n"
    "        ivec4 aop = u_tev_aop[i];\n"
    "        float av = (aop.x == 1) ? (ad - alerp) : (ad + alerp);\n"
    "        av = av * scale_factor(aop.z) + bias_value(aop.y);\n"
    "        if (aop.w != 0) av = clamp(av, 0.0, 1.0);\n"
    "        vec4 outv = vec4(rgb, av);\n"
    "        ivec2 reg = u_tev_reg[i];\n"
    "        /* out_reg selects where the result lands.  Writing a C register\n"
    "         * leaves the previous-stage chain untouched, which HSD's TEV\n"
    "         * graphs rely on (e.g. specular: C2 = spec map, then\n"
    "         * CPREV + RASC*C2 with CPREV still the diffuse). */\n"
    "        if (reg.x == 0) prev.rgb = outv.rgb;\n"
    "        else if (reg.x == 1) c0.rgb = outv.rgb;\n"
    "        else if (reg.x == 2) c1.rgb = outv.rgb;\n"
    "        else if (reg.x == 3) c2.rgb = outv.rgb;\n"
    "        if (reg.y == 0) prev.a = outv.a;\n"
    "        else if (reg.y == 1) c0.a = outv.a;\n"
    "        else if (reg.y == 2) c1.a = outv.a;\n"
    "        else if (reg.y == 3) c2.a = outv.a;\n"
    "    }\n"
    "    vec4 color = prev;\n"
    "    if (u_alpha_test != 0) {\n"
    "        float av = floor(color.a * 255.0 + 0.5);\n"
    "        bool p0 = gx_compare(u_acomp.x, av, floor(u_aref.x + 0.5));\n"
    "        bool p1 = gx_compare(u_acomp.y, av, floor(u_aref.y + 0.5));\n"
    "        bool pass = (u_aop == 1) ? (p0 || p1) :\n"
    "                    (u_aop == 2) ? (p0 != p1) :\n"
    "                    (u_aop == 3) ? (p0 == p1) : (p0 && p1);\n"
    "        if (!pass) discard;\n"
    "    }\n"
    "    if (u_fog_enable != 0) {\n"
    "        float d = v_dist;\n"
    "        float f;\n"
    "        if (u_fog_type == 2) {\n"
    "            f = clamp((u_fog_end - d) / max(u_fog_end - u_fog_start, 1e-4),\n"
    "                      0.0, 1.0);\n"
    "        } else {\n"
    "            float density = 1.0 / max(u_fog_end - u_fog_start, 1e-4);\n"
    "            if (u_fog_type == 4) f = exp(-density * d);\n"
    "            else if (u_fog_type == 5) f = exp(-density * density * d * d);\n"
    "            else if (u_fog_type == 6) f = 1.0 - exp(-density * d);\n"
    "            else if (u_fog_type == 7) f = 1.0 - exp(-density * density * d * d);\n"
    "            else f = 1.0;\n"
    "        }\n"
    "        color.rgb = mix(u_fog_color, color.rgb, f);\n"
    "    }\n"
    "    /* GXSetDstAlpha replaces the framebuffer alpha after the TEV chain. */\n"
    "    if (u_dst_alpha_enable != 0) color.a = u_dst_alpha;\n"
    "    frag_color = color;\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "gx_gl: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int build_program(char* error, size_t error_size)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, VERTEX_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SRC);
    GLint ok = 0;
    if (vs == 0 || fs == 0) {
        snprintf(error, error_size, "shader compile failed");
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        snprintf(error, error_size, "program link failed: %.200s", log);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_tex[0] = glGetUniformLocation(program, "u_tex0");
    u_tex[1] = glGetUniformLocation(program, "u_tex1");
    u_tex[2] = glGetUniformLocation(program, "u_tex2");
    u_tev_color = glGetUniformLocation(program, "u_tev_color");
    u_tev_kcolor = glGetUniformLocation(program, "u_tev_kcolor");
    u_tev_order = glGetUniformLocation(program, "u_tev_order");
    u_tev_cin = glGetUniformLocation(program, "u_tev_cin");
    u_tev_cop = glGetUniformLocation(program, "u_tev_cop");
    u_tev_ain = glGetUniformLocation(program, "u_tev_ain");
    u_tev_aop = glGetUniformLocation(program, "u_tev_aop");
    u_tev_sel = glGetUniformLocation(program, "u_tev_sel");
    u_tev_reg = glGetUniformLocation(program, "u_tev_reg");
    u_swap = glGetUniformLocation(program, "u_swap");
    u_stages = glGetUniformLocation(program, "u_stages");
    u_alpha_test = glGetUniformLocation(program, "u_alpha_test");
    u_acomp = glGetUniformLocation(program, "u_acomp");
    u_aref = glGetUniformLocation(program, "u_aref");
    u_aop = glGetUniformLocation(program, "u_aop");
    u_fog_enable = glGetUniformLocation(program, "u_fog_enable");
    u_fog_type = glGetUniformLocation(program, "u_fog_type");
    u_fog_start = glGetUniformLocation(program, "u_fog_start");
    u_fog_end = glGetUniformLocation(program, "u_fog_end");
    u_fog_color = glGetUniformLocation(program, "u_fog_color");
    u_tex_enable = glGetUniformLocation(program, "u_tex_enable");
    u_ras_flat = glGetUniformLocation(program, "u_ras_flat");


    u_tex_lod_bias = glGetUniformLocation(program, "u_tex_lod_bias");
    u_tex_dynamic_i4 = glGetUniformLocation(program, "u_tex_dynamic_i4");
    u_dst_alpha_enable = glGetUniformLocation(program, "u_dst_alpha_enable");
    u_dst_alpha = glGetUniformLocation(program, "u_dst_alpha");
    u_ch_enable = glGetUniformLocation(program, "u_ch_enable");
    u_ch_amb_src = glGetUniformLocation(program, "u_ch_amb_src");
    u_ch_mat_src = glGetUniformLocation(program, "u_ch_mat_src");
    u_ch_diff_fn = glGetUniformLocation(program, "u_ch_diff_fn");
    u_ch_light_mask = glGetUniformLocation(program, "u_ch_light_mask");
    u_ch_amb = glGetUniformLocation(program, "u_ch_amb");
    u_ch_mat = glGetUniformLocation(program, "u_ch_mat");
    u_light_pos = glGetUniformLocation(program, "u_light_pos");
    u_light_color = glGetUniformLocation(program, "u_light_color");
    u_light_a = glGetUniformLocation(program, "u_light_a");
    u_light_k = glGetUniformLocation(program, "u_light_k");
    u_light_dir = glGetUniformLocation(program, "u_light_dir");
    u_point_size = glGetUniformLocation(program, "u_point_size");
    u_ind_order = glGetUniformLocation(program, "u_ind_order");
    u_ind_scale = glGetUniformLocation(program, "u_ind_scale");
    u_ind_mtx0 = glGetUniformLocation(program, "u_ind_mtx0");
    u_ind_mtx1 = glGetUniformLocation(program, "u_ind_mtx1");
    u_tev_ind_a = glGetUniformLocation(program, "u_tev_ind_a");
    u_tev_ind_b = glGetUniformLocation(program, "u_tev_ind_b");
    u_num_ind_stages = glGetUniformLocation(program, "u_num_ind_stages");
    u_tex_size = glGetUniformLocation(program, "u_tex_size");
    u_coord_srtg = glGetUniformLocation(program, "u_coord_srtg");

    /* P-615: the Z-texture pass is a small dedicated program; writing
     * gl_FragDepth from the big TEV shader is ignored on Mesa/radeonsi. */
    {
        GLuint zvs = compile_shader(GL_VERTEX_SHADER, VERTEX_SRC);
        GLuint zfs = compile_shader(GL_FRAGMENT_SHADER, ZTEX_FRAGMENT_SRC);
        if (zvs != 0 && zfs != 0) {
            ztex_program = glCreateProgram();
            glAttachShader(ztex_program, zvs);
            glAttachShader(ztex_program, zfs);
            glLinkProgram(ztex_program);
            glGetProgramiv(ztex_program, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[2048];
                glGetProgramInfoLog(ztex_program, sizeof(log), NULL, log);
                fprintf(stderr, "gx_gl: ztex link failed: %.200s\n", log);
                ztex_program = 0;
            }
            glDeleteShader(zvs);
            glDeleteShader(zfs);
            u_ztex_sampler = glGetUniformLocation(ztex_program, "u_ztex");
            u_ztex_op_loc = glGetUniformLocation(ztex_program, "u_ztex_op");
            u_ztex_bias_loc = glGetUniformLocation(ztex_program, "u_ztex_bias");
        }
    }
    glUseProgram(program);
    glUniform1i(u_tex[0], 0);
    glUniform1i(u_tex[1], 1);
    glUniform1i(u_tex[2], 2);
    return 1;
}

/* ------------------------------------------------------------------ EGL */

static int gl_setup(char* error, size_t error_size)
{
    const char* extensions;
    if (!build_program(error, error_size)) {
        return 0;
    }
    extensions = (const char*) glGetString(GL_EXTENSIONS);
    aniso_supported = extensions != NULL &&
                      strstr(extensions,
                             "GL_EXT_texture_filter_anisotropic") != NULL;
    /* GLES has no client-side vertex arrays; one streaming VBO holds the
     * captured frame. */
    glGenVertexArrays(1, &vertex_vao);
    glBindVertexArray(vertex_vao);
    glGenBuffers(1, &vertex_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_vbo);
    /* The VBO object and GxHleVertex layout are permanent.  A VAO retains
     * these attribute bindings across buffer-storage replacements, so doing
     * this for every one of ~220 draws only burns thousands of GL calls per
     * frame. */
    configure_vertex_layout();
    glBindVertexArray(0);
    return 1;
}

int gx_gl_attach(int width, int height, char* error, size_t error_size)
{
    if (width > 0) {
        gl_width = width;
    }
    if (height > 0) {
        gl_height = height;
    }
    gpu_efb_copies = 1;
    return gl_setup(error, error_size);
}

void gx_gl_set_size(int width, int height)
{
    if (width > 0) {
        gl_width = width;
    }
    if (height > 0) {
        gl_height = height;
    }
}

void gx_gl_set_options(const GxGlOptions* options)
{
    if (options != NULL) {
        gl_options = *options;
    }
}

void gx_gl_clear_textures(void)
{
    size_t i;
    for (i = 0; i < tex_cache_count; ++i) {
        if (tex_cache[i].name != 0) {
            glDeleteTextures(1, &tex_cache[i].name);
        }
    }
    memset(tex_cache, 0, sizeof(tex_cache));
    tex_cache_count = 0;
    for (i = 0; i < dynamic_copy_count; ++i) {
        if (dynamic_copies[i].name != 0) {
            glDeleteTextures(1, &dynamic_copies[i].name);
        }
        if (dynamic_copies[i].framebuffer != 0) {
            glDeleteFramebuffers(1, &dynamic_copies[i].framebuffer);
        }
    }
    memset(dynamic_copies, 0, sizeof(dynamic_copies));
    dynamic_copy_count = 0;
}

int gx_gl_init(int width, int height, char* error, size_t error_size)
{
    static const EGLint config_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };
    static const EGLint context_attr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE
    };
    EGLint surface_attr[5];
    EGLConfig config;
    EGLint count = 0;
    EGLint major, minor;

    gpu_efb_copies = 0;
    gl_width = width > 0 ? width : 640;
    gl_height = height > 0 ? height : 480;

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        snprintf(error, error_size, "no EGL display");
        return 0;
    }
    if (!eglInitialize(egl_display, &major, &minor)) {
        snprintf(error, error_size, "eglInitialize failed 0x%x", eglGetError());
        return 0;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglChooseConfig(egl_display, config_attr, &config, 1, &count) ||
        count < 1) {
        snprintf(error, error_size, "no ES3 EGL config");
        return 0;
    }
    egl_context =
        eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attr);
    if (egl_context == EGL_NO_CONTEXT) {
        snprintf(error, error_size, "eglCreateContext failed 0x%x",
                 eglGetError());
        return 0;
    }
    surface_attr[0] = EGL_WIDTH;
    surface_attr[1] = gl_width;
    surface_attr[2] = EGL_HEIGHT;
    surface_attr[3] = gl_height;
    surface_attr[4] = EGL_NONE;
    egl_surface = eglCreatePbufferSurface(egl_display, config, surface_attr);
    if (egl_surface == EGL_NO_SURFACE) {
        snprintf(error, error_size, "eglCreatePbufferSurface failed 0x%x",
                 eglGetError());
        return 0;
    }
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        snprintf(error, error_size, "eglMakeCurrent failed 0x%x",
                 eglGetError());
        return 0;
    }
    printf("gx_gl: GL_VERSION=%s\n", (const char*) glGetString(GL_VERSION));
    printf("gx_gl: GL_RENDERER=%s\n", (const char*) glGetString(GL_RENDERER));
    return gl_setup(error, error_size);
}

void gx_gl_set_clear(float r, float g, float b, float a)
{
    clear_color[0] = r;
    clear_color[1] = g;
    clear_color[2] = b;
    clear_color[3] = a;
}

/* ----------------------------------------------------------- textures */

/* Bit-replication channel expansion, matching the GX texture unit and the
 * image decoders in native/gx/texture.c (P-675). */
static unsigned char pal_expand5(unsigned v) { return (unsigned char) ((v << 3) | (v >> 2)); }
static unsigned char pal_expand6(unsigned v) { return (unsigned char) ((v << 2) | (v >> 4)); }
static unsigned char pal_expand4(unsigned v) { return (unsigned char) ((v << 4) | v); }
static unsigned char pal_expand3(unsigned v) { return (unsigned char) ((v << 5) | (v << 2) | (v >> 1)); }

static unsigned char* expand_palette(const unsigned char* raw, unsigned int fmt,
                                     unsigned int count)
{
    unsigned char* out = (unsigned char*) malloc((size_t) count * 4);
    unsigned int i;
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        unsigned int v = ((unsigned int) raw[i * 2] << 8) | raw[i * 2 + 1];
        unsigned char* p = out + (size_t) i * 4;
        if (fmt == 0) { /* IA8: intensity in the high byte, alpha in the low */
            p[0] = p[1] = p[2] = (unsigned char) (v >> 8);
            p[3] = (unsigned char) (v & 0xFF);
        } else if (fmt == 1) { /* RGB565 */
            p[0] = pal_expand5((v >> 11) & 31);
            p[1] = pal_expand6((v >> 5) & 63);
            p[2] = pal_expand5(v & 31);
            p[3] = 255;
        } else { /* RGB5A3 */
            if (v & 0x8000) {
                p[0] = pal_expand5((v >> 10) & 31);
                p[1] = pal_expand5((v >> 5) & 31);
                p[2] = pal_expand5(v & 31);
                p[3] = 255;
            } else {
                p[0] = pal_expand4((v >> 8) & 15);
                p[1] = pal_expand4((v >> 4) & 15);
                p[2] = pal_expand4(v & 15);
                p[3] = pal_expand3((v >> 12) & 7);
            }
        }
    }
    return out;
}

static GLenum wrap_to_gl(int wrap)
{
    if (wrap == 1) {
        return GL_REPEAT;
    }
    if (wrap == 2) {
        return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

static GLenum min_filter_to_gl(unsigned char f, unsigned char mipmap,
                               unsigned int format)
{
    if (!mipmap) {
        f = (unsigned char) (f & 1u);
    }
    if (f == 5 && (format == 8 || format == 9 || format == 10)) {
        f = 3;
    }
    switch (f) {
    case 0:
        return GL_NEAREST;
    case 1:
        return GL_LINEAR;
    case 2:
        return GL_NEAREST_MIPMAP_NEAREST;
    case 3:
        return GL_LINEAR_MIPMAP_NEAREST;
    case 4:
        return GL_NEAREST_MIPMAP_LINEAR;
    default:
        return GL_LINEAR_MIPMAP_LINEAR;
    }
}

/* EFB-copied textures are updated in place at a fixed address; the cache key
 * is the source pointer, so a copy must drop any entry decoded from that
 * buffer or the map stays at the first frame's content (stale/black shadow). */
static void gl_texture_cache_invalidate(const void* image)
{
    size_t i = 0;

    while (i < tex_cache_count) {
        GlTextureCache* e = &tex_cache[i];
        if (e->image == image) {
            if (e->name != 0) {
                glDeleteTextures(1, &e->name);
            }
            tex_cache[i] = tex_cache[tex_cache_count - 1];
            tex_cache_count--;
            continue;
        }
        i++;
    }
}

static GLuint dynamic_texture_for(const GxHleTexture* t)
{
    size_t i;

    if (!gpu_efb_copies || t->format != GX_TF_I4) {
        return 0;
    }
    for (i = 0; i < dynamic_copy_count; ++i) {
        GlDynamicCopy* e = &dynamic_copies[i];
        if (e->image != t->image || e->width != t->width ||
            e->height != t->height)
        {
            continue;
        }
        glBindTexture(GL_TEXTURE_2D, e->name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        min_filter_to_gl(t->min_filt, 0, t->format));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        t->mag_filt == 0 ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        wrap_to_gl(t->wrap_s));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        wrap_to_gl(t->wrap_t));
        return e->name;
    }
    return 0;
}

static int texture_is_dynamic_i4(const GxHleTexture* t)
{
    size_t i;

    if (t == NULL || !gpu_efb_copies || t->format != GX_TF_I4) {
        return 0;
    }
    for (i = 0; i < dynamic_copy_count; ++i) {
        const GlDynamicCopy* e = &dynamic_copies[i];
        if (e->image == t->image && e->width == t->width &&
            e->height == t->height)
        {
            return 1;
        }
    }
    return 0;
}

static GLuint texture_for(const GxHleTexture* t)
{
    size_t i;
    unsigned char* rgba = NULL;
    char error[128];
    size_t bound;
    const void* image = t->image;
    const void* palette = t->palette;
    GLuint dynamic;

    if (image == NULL) {
        fprintf(stderr,
                "gx_gl: draw bound a texture with no image (%dx%d fmt=%u)\n",
                t->width, t->height, (unsigned) t->format);
        return 0;
    }
    dynamic = dynamic_texture_for(t);
    if (dynamic != 0) {
        return dynamic;
    }
    for (i = 0; i < tex_cache_count; ++i) {
        GlTextureCache* e = &tex_cache[i];
        if (e->image == image && e->palette == palette &&
            e->format == t->format && e->width == t->width &&
            e->height == t->height && e->wrap_s == t->wrap_s &&
            e->wrap_t == t->wrap_t && e->mag_filt == t->mag_filt &&
            e->min_filt == t->min_filt && e->mipmap == t->mipmap &&
            e->lod_bias == t->lod_bias && e->min_lod == t->min_lod &&
            e->max_lod == t->max_lod && e->anisotropy == t->anisotropy) {
            e->last_used = ++tex_clock;
            return e->name;
        }
    }

    /* Runtime textures (`HSD_ImageDescAlloc`, copied/streamed images) live
     * outside any parsed archive, and archive buffers can be freed and
     * reused, so the GX-declared dimensions are the only trustworthy bound:
     * they are exactly what the game's own GXInitTexObj draw reads. */
    bound = gx_texture_min_size((int) t->format, t->width, t->height);
    if (bound == 0) {
        bound = gx_hle_asset_remaining(image);
        if (bound == (size_t) -1) {
            bound = 64 * 1024;
        }
    }
    if (t->format == 8 || t->format == 9) {
        unsigned char* pal = NULL;
        if (palette == NULL || t->palette_entries == 0) {
            return 0;
        }
        pal = expand_palette((const unsigned char*) palette,
                             t->palette_format, t->palette_entries);
        if (pal == NULL) {
            return 0;
        }
        if (gx_texture_decode_ci(image, bound, t->width, t->height,
                                 (int) t->format, pal, t->palette_entries,
                                 &rgba, error, sizeof(error)) != 0) {
            free(pal);
            fprintf(stderr,
                    "gx_gl: CI texture decode failed: %s (%dx%d fmt=%u "
                    "avail=%zu)\n",
                    error, t->width, t->height, (unsigned) t->format, bound);
            return 0;
        }
        free(pal);
    } else {
        if (gx_texture_decode(image, bound, t->width, t->height,
                              (int) t->format, &rgba, error,
                              sizeof(error)) != 0) {
            fprintf(stderr,
                    "gx_gl: texture decode failed: %s (%dx%d fmt=%u "
                    "avail=%zu)\n",
                    error, t->width, t->height, (unsigned) t->format, bound);
            return 0;
        }
    }

    {
        GlTextureCache* e;
        if (tex_cache_count >= MAX_GL_TEXTURES) {
            /* Evict the least-recently-used entry: a match frame uses ~150
             * textures and the game keeps loading more, so overflowing the
             * cache must not silently bind texture 0 (black). */
            size_t oldest = 0;
            for (i = 1; i < tex_cache_count; ++i) {
                if (tex_cache[i].last_used < tex_cache[oldest].last_used) {
                    oldest = i;
                }
            }
            e = &tex_cache[oldest];
            if (e->name != 0) {
                glDeleteTextures(1, &e->name);
            }
            e->name = 0;
        } else {
            e = &tex_cache[tex_cache_count++];
        }
        glGenTextures(1, &e->name);
        glBindTexture(GL_TEXTURE_2D, e->name);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        if (t->mipmap) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        min_filter_to_gl(t->min_filt, t->mipmap, t->format));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        t->mag_filt == 0 ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        wrap_to_gl(t->wrap_s));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        wrap_to_gl(t->wrap_t));
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, t->min_lod);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, t->max_lod);
        if (aniso_supported && t->anisotropy > 0) {
            GLfloat samples = (GLfloat) (1u << t->anisotropy);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            samples);
        }
        e->image = image;
        e->palette = palette;
        e->format = t->format;
        e->palette_format = t->palette_format;
        e->palette_entries = t->palette_entries;
        e->width = (unsigned short) t->width;
        e->height = (unsigned short) t->height;
        e->wrap_s = t->wrap_s;
        e->wrap_t = t->wrap_t;
        e->mag_filt = t->mag_filt;
        e->min_filt = t->min_filt;
        e->mipmap = t->mipmap;
        e->lod_bias = t->lod_bias;
        e->min_lod = t->min_lod;
        e->max_lod = t->max_lod;
        e->anisotropy = t->anisotropy;
        e->last_used = ++tex_clock;
        free(rgba);
        return e->name;
    }
}

/* ------------------------------------------------------------- GL state */

static GLenum blend_factor(unsigned char f)
{
    switch (f) {
    case 0:
        return GL_ZERO;
    case 1:
        return GL_ONE;
    case 2:
        return GL_SRC_COLOR;
    case 3:
        return GL_ONE_MINUS_SRC_COLOR;
    case 4:
        return GL_SRC_ALPHA;
    case 5:
        return GL_ONE_MINUS_SRC_ALPHA;
    case 6:
        return GL_DST_ALPHA;
    case 7:
        return GL_ONE_MINUS_DST_ALPHA;
    default:
        return GL_ONE;
    }
}

static GLenum depth_func(unsigned char f)
{
    switch (f) {
    case 0:
        return GL_NEVER;
    case 1:
        return GL_LESS;
    case 2:
        return GL_EQUAL;
    case 3:
        return GL_LEQUAL;
    case 4:
        return GL_GREATER;
    case 5:
        return GL_NOTEQUAL;
    case 6:
        return GL_GEQUAL;
    default:
        return GL_ALWAYS;
    }
}

/* GXSetViewport is EFB pixels with a top-left origin; GL's is bottom-up.
 * Applying it per draw is what puts the shadow pass into its 256x256 corner
 * (its camera viewport) instead of over the whole surface. */
static void apply_viewport(const GxHleDrawState* s)
{
    GLfloat sx = (GLfloat) gl_width / 640.0f;
    GLfloat sy = (GLfloat) gl_height / 480.0f;
    GLint vx = (GLint) (s->viewport[0] * sx + 0.5f);
    GLint vy = (GLint) (s->viewport[1] * sy + 0.5f);
    GLint vw = (GLint) (s->viewport[2] * sx + 0.5f);
    GLint vh = (GLint) (s->viewport[3] * sy + 0.5f);

    if (vw < 1) {
        vw = 1;
    }
    if (vh < 1) {
        vh = 1;
    }
    glViewport(vx, gl_height - vy - vh, vw, vh);
    glDepthRangef(s->depth_range[0], s->depth_range[1]);
}

static void apply_draw_state(const GxHleDrawState* s)
{
    /* Wireframe is an inspection mode: GX culling would hide the interior and
     * back edges the owner reads the mesh by (the prototype viewer defaults
     * CULL OFF for the same reason).  The rest of the material state still
     * applies, so depth/blend stay faithful. */
    if (gl_options.no_cull || gl_options.wireframe) {
        glDisable(GL_CULL_FACE);
    } else {
        switch (s->cull_mode) {
        case 1: /* GX_CULL_FRONT */
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            break;
        case 2: /* GX_CULL_BACK */
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            break;
        case 3: /* GX_CULL_ALL */
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT_AND_BACK);
            break;
        default:
            glDisable(GL_CULL_FACE);
            break;
        }
    }

    if (s->blend_type == 1 || s->blend_type == 3) {
        glEnable(GL_BLEND);
        glBlendFunc(blend_factor(s->blend_src), blend_factor(s->blend_dst));
        glBlendEquation(s->blend_type == 3 ? GL_FUNC_REVERSE_SUBTRACT
                                           : GL_FUNC_ADD);
    } else if (s->blend_type == 2) {
        /* GX_BM_LOGIC: GLES3 has no logic ops.  Fighter materials do not use
         * GX_BM_LOGIC (HSD_SetupPEMode selects BLEND or NONE); S2 renders it
         * as opaque and records the deviation. */
        glDisable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
    } else {
        glDisable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
    }

    /* P-680: GX line width; GLES may clamp wide lines to 1. */
    glLineWidth((GLfloat) (s->line_width ? s->line_width : 1));
    if (s->z_enable) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthFunc(depth_func(s->z_func));
    glDepthMask(s->z_update ? GL_TRUE : GL_FALSE);
    glColorMask(s->color_update ? GL_TRUE : GL_FALSE,
                s->color_update ? GL_TRUE : GL_FALSE,
                s->color_update ? GL_TRUE : GL_FALSE,
                s->alpha_update ? GL_TRUE : GL_FALSE);
}

static void upload_draw_uniforms(const GxHleDrawState* s)
{
    GLint orders[MAX_TEV_STAGES][4];
    GLint cins[MAX_TEV_STAGES][4];
    GLint cops[MAX_TEV_STAGES][4];
    GLint ains[MAX_TEV_STAGES][4];
    GLint aops[MAX_TEV_STAGES][4];
    GLint sels[MAX_TEV_STAGES][4];
    GLint regs[MAX_TEV_STAGES][2];
    GLfloat colors[4][4];
    GLfloat kcolors[4][4];
    GLint swaps[16][4];
    int stages = s->num_stages;
    int i;
    int j;

    if (stages < 1) {
        stages = 1;
    }
    if (stages > MAX_TEV_STAGES) {
        stages = MAX_TEV_STAGES;
    }
    for (i = 0; i < MAX_TEV_STAGES; ++i) {
        const GxHleTevStage* st = &s->stages[i];
        orders[i][0] = st->order_coord;
        orders[i][1] = st->order_map;
        orders[i][2] = st->order_chan;
        orders[i][3] = 0;
        cins[i][0] = st->color_a;
        cins[i][1] = st->color_b;
        cins[i][2] = st->color_c;
        cins[i][3] = st->color_d;
        cops[i][0] = st->color_op;
        cops[i][1] = st->color_bias;
        cops[i][2] = st->color_scale;
        cops[i][3] = st->color_clamp;
        ains[i][0] = st->alpha_a;
        ains[i][1] = st->alpha_b;
        ains[i][2] = st->alpha_c;
        ains[i][3] = st->alpha_d;
        aops[i][0] = st->alpha_op;
        aops[i][1] = st->alpha_bias;
        aops[i][2] = st->alpha_scale;
        aops[i][3] = st->alpha_clamp;
        sels[i][0] = st->ras_sel;
        sels[i][1] = st->tex_sel;
        sels[i][2] = st->kc_sel;
        sels[i][3] = st->ka_sel;
        regs[i][0] = st->color_reg;
        regs[i][1] = st->alpha_reg;
    }
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            colors[i][j] = s->tev_color[i][j];
            kcolors[i][j] = s->tev_kcolor[i][j];
        }
    }
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            swaps[i * 4 + j][0] = s->swap_table[i][0];
            swaps[i * 4 + j][1] = s->swap_table[i][1];
            swaps[i * 4 + j][2] = s->swap_table[i][2];
            swaps[i * 4 + j][3] = s->swap_table[i][3];
        }
    }

    glUniform1i(u_stages, stages);
    glUniform4fv(u_tev_color, 4, &colors[0][0]);
    glUniform4fv(u_tev_kcolor, 4, &kcolors[0][0]);
    {
        GLint ch_enable[4];
        GLint ch_amb_src[4];
        GLint ch_mat_src[4];
        GLint ch_diff_fn[4];
        GLint ch_light_mask[4];
        GLfloat ch_amb[4][4];
        GLfloat ch_mat[4][4];
        GLfloat light_pos[8][4];
        GLfloat light_color[8][4];
        GLfloat light_a[8][4];
        GLfloat light_k[8][4];
        GLfloat light_dir[8][4];
        for (i = 0; i < 4; ++i) {
            ch_enable[i] = s->ch_enable[i];
            ch_amb_src[i] = s->ch_amb_src[i];
            ch_mat_src[i] = s->ch_mat_src[i];
            ch_diff_fn[i] = s->ch_diff_fn[i];
            ch_light_mask[i] = (GLint) s->ch_light_mask[i];
            memcpy(ch_amb[i], s->ch_amb[i], sizeof(ch_amb[i]));
            memcpy(ch_mat[i], s->ch_mat[i], sizeof(ch_mat[i]));
        }
        for (i = 0; i < 8; ++i) {
            const GxHleLight* l = &s->lights[i];
            light_pos[i][0] = l->pos[0];
            light_pos[i][1] = l->pos[1];
            light_pos[i][2] = l->pos[2];
            light_pos[i][3] = 0.0f;
            light_color[i][0] = l->color.r / 255.0f;
            light_color[i][1] = l->color.g / 255.0f;
            light_color[i][2] = l->color.b / 255.0f;
            light_color[i][3] = l->color.a / 255.0f;
            light_a[i][0] = l->a[0];
            light_a[i][1] = l->a[1];
            light_a[i][2] = l->a[2];
            light_a[i][3] = 0.0f;
            light_k[i][0] = l->k[0];
            light_k[i][1] = l->k[1];
            light_k[i][2] = l->k[2];
            light_k[i][3] = 0.0f;
            light_dir[i][0] = l->dir[0];
            light_dir[i][1] = l->dir[1];
            light_dir[i][2] = l->dir[2];
            light_dir[i][3] = 0.0f;
        }
        glUniform1iv(u_ch_enable, 4, ch_enable);
        glUniform1iv(u_ch_amb_src, 4, ch_amb_src);
        glUniform1iv(u_ch_mat_src, 4, ch_mat_src);
        glUniform1iv(u_ch_diff_fn, 4, ch_diff_fn);
        glUniform1iv(u_ch_light_mask, 4, ch_light_mask);
        glUniform4fv(u_ch_amb, 4, &ch_amb[0][0]);
        glUniform4fv(u_ch_mat, 4, &ch_mat[0][0]);
        glUniform4fv(u_light_pos, 8, &light_pos[0][0]);
        glUniform4fv(u_light_color, 8, &light_color[0][0]);
        glUniform4fv(u_light_a, 8, &light_a[0][0]);
        glUniform4fv(u_light_k, 8, &light_k[0][0]);
        glUniform4fv(u_light_dir, 8, &light_dir[0][0]);
    }
    glUniform4iv(u_tev_order, MAX_TEV_STAGES, &orders[0][0]);
    glUniform4iv(u_tev_cin, MAX_TEV_STAGES, &cins[0][0]);
    glUniform4iv(u_tev_cop, MAX_TEV_STAGES, &cops[0][0]);
    glUniform4iv(u_tev_ain, MAX_TEV_STAGES, &ains[0][0]);
    glUniform4iv(u_tev_aop, MAX_TEV_STAGES, &aops[0][0]);
    glUniform4iv(u_tev_sel, MAX_TEV_STAGES, &sels[0][0]);
    glUniform2iv(u_tev_reg, MAX_TEV_STAGES, &regs[0][0]);
    glUniform4iv(u_swap, 16, &swaps[0][0]);
    glUniform1i(u_alpha_test, gl_options.no_alpha_test ? 0 : 1);
    {
        GLint acomp[2] = { s->alpha_comp0, s->alpha_comp1 };
        GLfloat aref[2] = { (GLfloat) s->alpha_ref0,
                            (GLfloat) s->alpha_ref1 };
        glUniform2iv(u_acomp, 1, acomp);
        glUniform2fv(u_aref, 1, aref);
        glUniform1i(u_aop, s->alpha_op);
    }
    glUniform1i(u_fog_enable, s->fog_enable);
    glUniform1i(u_fog_type, s->fog_type);
    glUniform1f(u_fog_start, s->fog_start);
    glUniform1f(u_fog_end, s->fog_end);
    glUniform3f(u_fog_color, s->fog_color[0], s->fog_color[1],
                s->fog_color[2]);
    /* P-680: GXSetPointSize drives gl_PointSize in the shared VS. */
    glUniform1f(u_point_size, (GLfloat) (s->point_size ? s->point_size : 1));
    glUniform1i(u_tex_enable, gl_options.textures);
    glUniform1i(u_ras_flat, !gl_options.lighting);
    glUniform1i(u_dst_alpha_enable, s->dst_alpha_enable);
    glUniform1f(u_dst_alpha, (GLfloat) s->dst_alpha / 255.0f);

    /* P-672 indirect state.  GX_ITS_1..256 divide the coordinate by 2^n. */
    {
        static const GLfloat its_scale[9] = {
            1.0f,        0.5f,       0.25f,      0.125f,     0.0625f,
            0.03125f,    0.015625f,  0.0078125f, 0.00390625f
        };
        GLint ind_order[4][2];
        GLfloat ind_scale[4][2];
        GLfloat ind_mtx0[4][4];
        GLfloat ind_mtx1[4][4];
        GLint tev_ind_a[MAX_TEV_STAGES][4];
        GLint tev_ind_b[MAX_TEV_STAGES][4];
        GLint coord_srtg[3];
        for (i = 0; i < 4; ++i) {
            int ss = s->ind[i].scale_s & 15;
            int st = s->ind[i].scale_t & 15;
            if (ss > 8) ss = 8;
            if (st > 8) st = 8;
            ind_order[i][0] = s->ind[i].tex_coord;
            ind_order[i][1] = s->ind[i].tex_map;
            ind_scale[i][0] = its_scale[ss];
            ind_scale[i][1] = its_scale[st];
            ind_mtx0[i][0] = s->ind[i].mtx[0][0];
            ind_mtx0[i][1] = s->ind[i].mtx[0][1];
            ind_mtx0[i][2] = s->ind[i].mtx[0][2];
            ind_mtx0[i][3] = s->ind[i].scale;
            ind_mtx1[i][0] = s->ind[i].mtx[1][0];
            ind_mtx1[i][1] = s->ind[i].mtx[1][1];
            ind_mtx1[i][2] = s->ind[i].mtx[1][2];
            ind_mtx1[i][3] = 0.0f;
        }
        for (i = 0; i < MAX_TEV_STAGES; ++i) {
            const GxHleTevStage* st = &s->stages[i];
            tev_ind_a[i][0] = st->ind_stage;
            tev_ind_a[i][1] = st->ind_format;
            tev_ind_a[i][2] = st->ind_bias;
            tev_ind_a[i][3] = st->ind_mtx;
            tev_ind_b[i][0] = st->ind_wrap_s;
            tev_ind_b[i][1] = st->ind_wrap_t;
            tev_ind_b[i][2] = st->ind_add_prev;
            tev_ind_b[i][3] = st->ind_enable;
        }
        for (i = 0; i < 3; ++i) {
            coord_srtg[i] = s->texgen[i].type == GX_TG_SRTG ? 1 : 0;
        }
        glUniform2iv(u_ind_order, 4, &ind_order[0][0]);
        glUniform2fv(u_ind_scale, 4, &ind_scale[0][0]);
        glUniform4fv(u_ind_mtx0, 4, &ind_mtx0[0][0]);
        glUniform4fv(u_ind_mtx1, 4, &ind_mtx1[0][0]);
        glUniform4iv(u_tev_ind_a, MAX_TEV_STAGES, &tev_ind_a[0][0]);
        glUniform4iv(u_tev_ind_b, MAX_TEV_STAGES, &tev_ind_b[0][0]);
        glUniform1i(u_num_ind_stages, s->num_ind_stages);
        glUniform3iv(u_coord_srtg, 1, coord_srtg);
    }
}

/* The live viewer keeps R4 shadow copies on the GPU.  HSD only samples these
 * buffers as textures, so materializing 32 KB of tiled I4 RAM and decoding it
 * straight back into a GL texture was a synchronous GPU->CPU->GPU round trip
 * twice per frame.  The headless EGL path retains that materialized fallback
 * because decomp_efb verifies the emulated RAM bytes directly. */
static int efb_copy_r4_gpu(const GxHleDraw* d, int src_x, int gl_y, int src_w,
                           int src_h)
{
    GlDynamicCopy* e = NULL;
    GLboolean scissor_enabled;
    GLenum error;
    size_t i;

    for (i = 0; i < dynamic_copy_count; ++i) {
        if (dynamic_copies[i].image == d->copy_dest) {
            e = &dynamic_copies[i];
            break;
        }
    }
    if (e == NULL) {
        if (dynamic_copy_count >= MAX_DYNAMIC_COPIES) {
            return 0;
        }
        e = &dynamic_copies[dynamic_copy_count++];
        memset(e, 0, sizeof(*e));
        e->image = d->copy_dest;
        glGenTextures(1, &e->name);
        glGenFramebuffers(1, &e->framebuffer);
    }
    glBindTexture(GL_TEXTURE_2D, e->name);
    if (e->width != d->copy_dst_w || e->height != d->copy_dst_h) {
        e->width = d->copy_dst_w;
        e->height = d->copy_dst_h;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, e->width, e->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, e->framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, e->name, 0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
    {
        e->width = 0;
        e->height = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    /* glBlitFramebuffer is clipped by the current scissor.  At this point it
     * still describes the GX draw that produced the shadow map in the main
     * EFB, not the small destination texture. */
    scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    /* Reverse the source Y endpoints.  The CPU path changes GL bottom-up EFB
     * rows into GX top-down I4 rows, then GL uploads those rows bottom-up;
     * this flipped blit produces the same sampled orientation directly. */
    glBlitFramebuffer(src_x, gl_y + src_h, src_x + src_w, gl_y, 0, 0,
                      e->width, e->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (scissor_enabled) {
        glEnable(GL_SCISSOR_TEST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    error = glGetError();
    if (error != GL_NO_ERROR) {
        /* Prevent texture_for() from selecting a partial/failed GPU copy;
         * the caller will materialize the normal CPU I4 fallback. */
        e->width = 0;
        e->height = 0;
        return 0;
    }
    return 1;
}

/* P-674: EFB colour -> GX tiled texture encoders.  The intensity/luma
 * conversion is Aurora's `tex_copy_conv.cpp` preamble (ITU-R BT.601,
 * `intensity()`) and `quantize4()`; the nibble/byte/word placement matches
 * the matching decoder in native/gx/texture.c. */
static unsigned copy_quantize4(float v)
{
    /* Aurora: floor(v * 16) / 15 clamped by the R8Unorm target; as a nibble
     * that is floor(v * 16) clamped to 15. */
    unsigned n;
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    n = (unsigned) (v * 16.0f);
    return n > 15 ? 15 : n;
}

static unsigned char copy_intensity8(const unsigned char* p)
{
    /* BT.601 luma with the 16/255 pedestal. */
    float v = (0.257f * (float) p[0] + 0.504f * (float) p[1] +
               0.098f * (float) p[2]) +
              16.0f;
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 255.0f) {
        v = 255.0f;
    }
    return (unsigned char) (v + 0.5f);
}

static size_t copy_tex_dest_size(unsigned int fmt, int w, int h)
{
    switch (fmt) {
    case GX_TF_I4:
    case GX_CTF_R4:
        return (size_t) ((w + 7) / 8) * ((h + 7) / 8) * 32;
    case GX_TF_I8:
    case GX_TF_IA4:
        return (size_t) ((w + 7) / 8) * ((h + 3) / 4) * 32;
    case GX_TF_IA8:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
        return (size_t) ((w + 3) / 4) * ((h + 3) / 4) * 32;
    case GX_TF_RGBA8:
        return (size_t) ((w + 3) / 4) * ((h + 3) / 4) * 64;
    default:
        return 0;
    }
}

/* Encode the (bottom-up GL) RGBA source into the destination's GX tiling.
 * Every destination texel is written from the scaled source, so the hardware
 * clear colour only shows where a copy would not cover, which cannot happen
 * here (GXCopyTex always writes the full destination). */
static void copy_tex_encode(unsigned int fmt, unsigned char* dest,
                            int dst_w, int dst_h,
                            const unsigned char* rgba, int src_w, int src_h)
{
    int x, y;
    if (dst_w <= 0 || dst_h <= 0) {
        return;
    }
    for (y = 0; y < dst_h; ++y) {
        int sy = y * src_h / dst_h;
        for (x = 0; x < dst_w; ++x) {
            int sx = x * src_w / dst_w;
            /* GL row 0 is the bottom; GX rows are top-down. */
            const unsigned char* p =
                rgba + (((size_t) (src_h - 1 - sy)) * src_w + sx) * 4;
            switch (fmt) {
            case GX_TF_RGB565: {
                unsigned short v = (unsigned short)
                    (((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
                size_t off = ((size_t) (y / 4) * (dst_w / 4) + (x / 4)) * 32 +
                             (size_t) (y % 4) * 8 + (size_t) (x % 4) * 2;
                dest[off] = (unsigned char) (v >> 8);
                dest[off + 1] = (unsigned char) (v & 0xFF);
                break;
            }
            case GX_TF_RGB5A3: {
                unsigned short v;
                if (p[3] >= 0xE0) {
                    v = (unsigned short) (0x8000 | ((p[0] >> 3) << 10) |
                                          ((p[1] >> 3) << 5) | (p[2] >> 3));
                } else {
                    v = (unsigned short) (((p[3] >> 5) << 12) |
                                          ((p[0] >> 4) << 8) |
                                          ((p[1] >> 4) << 4) | (p[2] >> 4));
                }
                {
                    size_t off =
                        ((size_t) (y / 4) * (dst_w / 4) + (x / 4)) * 32 +
                        (size_t) (y % 4) * 8 + (size_t) (x % 4) * 2;
                    dest[off] = (unsigned char) (v >> 8);
                    dest[off + 1] = (unsigned char) (v & 0xFF);
                }
                break;
            }
            case GX_TF_I4: {
                unsigned n = copy_quantize4(
                    (float) copy_intensity8(p) / 255.0f);
                size_t off = ((size_t) (y / 8) * ((dst_w + 7) / 8) +
                              (size_t) (x / 8)) *
                                 32 +
                             (size_t) (y % 8) * 4 + (size_t) (x % 8) / 2;
                if ((x & 1) == 0) {
                    dest[off] = (unsigned char) ((dest[off] & 0x0F) | (n << 4));
                } else {
                    dest[off] = (unsigned char) ((dest[off] & 0xF0) | n);
                }
                break;
            }
            case GX_TF_I8: {
                size_t off = ((size_t) (y / 4) * ((dst_w + 7) / 8) +
                              (size_t) (x / 8)) *
                                 32 +
                             (size_t) (y % 4) * 8 + (size_t) (x % 8);
                dest[off] = copy_intensity8(p);
                break;
            }
            case GX_TF_IA4: {
                unsigned i = copy_quantize4(
                    (float) copy_intensity8(p) / 255.0f);
                unsigned a = copy_quantize4((float) p[3] / 255.0f);
                size_t off = ((size_t) (y / 4) * ((dst_w + 7) / 8) +
                              (size_t) (x / 8)) *
                                 32 +
                             (size_t) (y % 4) * 8 + (size_t) (x % 8);
                /* decode_ia4: low nibble intensity, high nibble alpha. */
                dest[off] = (unsigned char) ((a << 4) | i);
                break;
            }
            case GX_TF_IA8: {
                size_t off = ((size_t) (y / 4) * (dst_w / 4) + (x / 4)) * 32 +
                             (size_t) (y % 4) * 8 + (size_t) (x % 4) * 2;
                /* decode_ia8 reads alpha first, then intensity. */
                dest[off] = p[3];
                dest[off + 1] = copy_intensity8(p);
                break;
            }
            case GX_TF_RGBA8: {
                size_t tile = ((size_t) (y / 4) * ((dst_w + 3) / 4) + x / 4);
                size_t ar = tile * 64 + (size_t) (y % 4) * 4 + (x % 4);
                size_t gb = tile * 64 + 32 + (size_t) (y % 4) * 8 +
                            (size_t) (x % 4) * 2;
                dest[ar * 2 + 0] = p[3];
                dest[ar * 2 + 1] = p[0];
                dest[gb] = p[1];
                dest[gb + 1] = p[2];
                break;
            }
            case GX_CTF_R4: {
                unsigned v = p[0] >> 4;
                size_t off = ((size_t) (y / 8) * ((dst_w + 7) / 8) + x / 8) *
                                 32 +
                             (size_t) (y % 8) * 4 + (size_t) (x % 8) / 2;
                if ((x & 1) == 0) {
                    dest[off] = (unsigned char) ((dest[off] & 0x0F) | (v << 4));
                } else {
                    dest[off] = (unsigned char) ((dest[off] & 0xF0) | v);
                }
                break;
            }
            default:
                break;
            }
        }
    }
}

/* P-615: GXCopyTex.  Reads the EFB colour (scaled to the GL surface) and
 * re-encodes it into GX tiled texture memory so the existing CPU texture
 * decoder sees a normal texture. */
static void efb_copy_tex(const GxHleDraw* d)
{
    int src_x;
    int src_y;
    int src_w;
    int src_h;
    int dst_w;
    int dst_h;
    int gl_y;
    unsigned char* rgba;
    unsigned char* dest = (unsigned char*) d->copy_dest;

    if (dest == NULL || d->copy_dst_w == 0 || d->copy_dst_h == 0) {
        return;
    }
    src_x = (int) d->copy_left * gl_width / 640;
    src_y = (int) d->copy_top * gl_height / 480;
    src_w = (int) d->copy_w * gl_width / 640;
    src_h = (int) d->copy_h * gl_height / 480;
    if (src_w < 1) src_w = 1;
    if (src_h < 1) src_h = 1;
    if (src_x + src_w > gl_width) src_w = gl_width - src_x;
    if (src_x < 0) { src_w += src_x; src_x = 0; }
    if (src_y + src_h > gl_height) src_h = gl_height - src_y;
    if (src_y < 0) { src_h += src_y; src_y = 0; }
    if (src_w < 1 || src_h < 1) {
        return;
    }
    dst_w = d->copy_dst_w;
    dst_h = d->copy_dst_h;
    gl_y = gl_height - src_y - src_h; /* GL origin is bottom-left */

    if (gpu_efb_copies && d->copy_fmt == GX_CTF_R4 &&
        efb_copy_r4_gpu(d, src_x, gl_y, src_w, src_h))
    {
        gl_texture_cache_invalidate(dest);
        return;
    }

    rgba = (unsigned char*) malloc((size_t) src_w * src_h * 4);
    if (rgba == NULL) {
        return;
    }
    glReadPixels(src_x, gl_y, src_w, src_h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    memset(dest, 0, copy_tex_dest_size(d->copy_fmt, dst_w, dst_h));
    copy_tex_encode(d->copy_fmt, dest, dst_w, dst_h, rgba, src_w, src_h);
    free(rgba);
    gl_texture_cache_invalidate(dest);
}

/* P-615: depth-only Z-texture pass (GX_ZT_REPLACE/ADD). */
static void draw_ztex(const GxHleDraw* d, const GxHleDrawState* s,
                      const GxHleTexture* textures, size_t texture_count)
{
    GLuint tex = 0;
    int i;

    for (i = 0; i < s->num_stages && i < GX_HLE_MAX_STAGES; i++) {
        int map = s->stages[i].order_map;
        if (map >= 0 && map < 8) {
            if (s->texmap[map] >= 0 &&
                (size_t) s->texmap[map] < texture_count) {
                tex = texture_for(&textures[s->texmap[map]]);
            }
            break;
        }
    }
    glUseProgram(ztex_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(u_ztex_sampler, 0);
    glUniform1i(u_ztex_op_loc, (int) s->ztex_op);
    glUniform1f(u_ztex_bias_loc, s->ztex_bias);
    apply_viewport(s);
    apply_draw_state(s);
    glDrawArrays(GL_TRIANGLES, (GLint) d->first_vertex,
                 (GLsizei) d->vertex_count);
}

int gx_gl_render_frame(void)
{
    const GxHleVertex* vertices = NULL;
    const GxHleDraw* draws = NULL;
    GxHleTexture* textures = NULL;
    size_t vertex_count = 0;
    size_t draw_count = 0;
    size_t texture_count = 0;
    size_t i;

    if (program == 0) {
        return -1;
    }
    gx_hle_get_frame(&vertices, &vertex_count, &draws, &draw_count,
                     &textures, &texture_count);
    glViewport(0, 0, gl_width, gl_height);
    glDepthRangef(0.0, 1.0);
    /* The previous frame's GX state may have masked alpha writes or depth
     * writes (RENDER_NO_ZUPDATE); without restoring both masks first, glClear
     * leaves stale alpha/depth behind and rotating cameras lose geometry. */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    /* ...and the scissor test from the last draw would clip the clear. */
    glDisable(GL_SCISSOR_TEST);
    glClearColor(clear_color[0], clear_color[1], clear_color[2],
                 clear_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glFrontFace(GL_CW); /* GX front faces are clockwise */

    glUseProgram(program);
    glBindVertexArray(vertex_vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_vbo);
    {
        size_t bytes = vertex_count * sizeof(GxHleVertex);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) bytes, vertices,
                     GL_STREAM_DRAW);
        vertex_vbo_capacity = bytes;
        (void) vertex_vbo_capacity;
    }
    for (i = 0; i < draw_count; ++i) {
        const GxHleDraw* d = &draws[i];
        const GxHleDrawState* s = &d->state;
        const GxHleTexture* t0 = NULL;
        const GxHleTexture* t1 = NULL;
        const GxHleTexture* t2 = NULL;
        GLuint tex0 = 0;
        if (gl_options.only_draw >= 0 &&
            (size_t) gl_options.only_draw != i) {
            continue;
        }
        if (gl_options.hide_draw >= 0 &&
            (size_t) gl_options.hide_draw == i) {
            continue;
        }
        if (d->kind == GX_HLE_DRAW_COPY_TEX) {
            efb_copy_tex(d);
            continue;
        }
        if (d->vertex_count != 0 && s->ztex_op != 0 && ztex_program != 0) {
            draw_ztex(d, s, textures, texture_count);
            glUseProgram(program);
            continue;
        }
        GLuint tex1 = 0;
        GLuint tex2 = 0;

        if (d->vertex_count == 0) {
            continue;
        }
        if (s->cull_mode == 3) {
            continue;
        }
        upload_draw_uniforms(s);

        if (s->texmap[0] >= 0 && (size_t) s->texmap[0] < texture_count) {
            t0 = &textures[s->texmap[0]];
            tex0 = texture_for(t0);
        }
        if (s->texmap[1] >= 0 && (size_t) s->texmap[1] < texture_count) {
            t1 = &textures[s->texmap[1]];
            tex1 = texture_for(t1);
        }
        if (s->texmap[2] >= 0 && (size_t) s->texmap[2] < texture_count) {
            t2 = &textures[s->texmap[2]];
            tex2 = texture_for(t2);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, tex2);
        glActiveTexture(GL_TEXTURE0);
        {
            GLfloat bias[3] = { t0 != NULL ? t0->lod_bias : 0.0f,
                                t1 != NULL ? t1->lod_bias : 0.0f,
                                t2 != NULL ? t2->lod_bias : 0.0f };
            GLint dynamic_i4[3] = { texture_is_dynamic_i4(t0),
                                    texture_is_dynamic_i4(t1),
                                    texture_is_dynamic_i4(t2) };
            /* P-672: destination sizes convert the indirect offset from GX
             * texels to this renderer's normalized UVs. */
            GLfloat sizes[3][3] = { { 1.0f, 1.0f, 0.0f },
                                    { 1.0f, 1.0f, 0.0f },
                                    { 1.0f, 1.0f, 0.0f } };
            if (t0 != NULL) {
                sizes[0][0] = (GLfloat) t0->width;
                sizes[0][1] = (GLfloat) t0->height;
            }
            if (t1 != NULL) {
                sizes[1][0] = (GLfloat) t1->width;
                sizes[1][1] = (GLfloat) t1->height;
            }
            if (t2 != NULL) {
                sizes[2][0] = (GLfloat) t2->width;
                sizes[2][1] = (GLfloat) t2->height;
            }
            glUniform3fv(u_tex_lod_bias, 1, bias);
            glUniform3iv(u_tex_dynamic_i4, 1, dynamic_i4);
            glUniform3fv(u_tex_size, 3, &sizes[0][0]);
        }

        apply_viewport(s);
        apply_draw_state(s);
        /* GX scissor is in 640x480 EFB pixels; scale it onto the surface the
         * same way the projection/viewport stretch is applied. */
        {
            GLfloat sx = (GLfloat) gl_width / 640.0f;
            GLfloat sy = (GLfloat) gl_height / 480.0f;
            GLint sc_x = (GLint) (s->scissor_x * sx + 0.5f);
            GLint sc_y = (GLint) (s->scissor_y * sy + 0.5f);
            GLint sc_w = (GLint) (s->scissor_w * sx + 0.5f);
            GLint sc_h = (GLint) (s->scissor_h * sy + 0.5f);
            if (sc_w < 0) {
                sc_w = 0;
            }
            if (sc_h < 0) {
                sc_h = 0;
            }
            glEnable(GL_SCISSOR_TEST);
            glScissor(sc_x, gl_height - sc_y - sc_h, sc_w, sc_h);
        }

        /* P-680: a draw snapshot may mix triangle strips, lines and points;
         * each run carries one topology.  Draws without runs are all
         * triangles (pre-P-680 captures). */
        if (d->run_count > 0) {
            int r;
            for (r = 0; r < d->run_count; ++r) {
                const GxHleRun* run = &d->runs[r];
                if (run->vertex_count == 0) {
                    continue;
                }
                if (run->mode == GX_HLE_MODE_LINES) {
                    if (!gl_options.wireframe) {
                        glDrawArrays(GL_LINES, (GLint) run->first_vertex,
                                     (GLsizei) run->vertex_count);
                    }
                } else if (run->mode == GX_HLE_MODE_POINTS) {
                    if (!gl_options.wireframe) {
                        glDrawArrays(GL_POINTS, (GLint) run->first_vertex,
                                     (GLsizei) run->vertex_count);
                    }
                } else if (gl_options.wireframe) {
                    size_t k;
                    for (k = 0; k + 3 <= run->vertex_count; k += 3) {
                        glDrawArrays(GL_LINE_LOOP,
                                     (GLint) (run->first_vertex + k), 3);
                    }
                } else {
                    glDrawArrays(GL_TRIANGLES, (GLint) run->first_vertex,
                                 (GLsizei) run->vertex_count);
                }
            }
        } else if (gl_options.wireframe) {
            size_t k;
            for (k = 0; k + 3 <= d->vertex_count; k += 3) {
                glDrawArrays(GL_LINE_LOOP, (GLint) (d->first_vertex + k), 3);
            }
        } else {
            glDrawArrays(GL_TRIANGLES, (GLint) d->first_vertex,
                         (GLsizei) d->vertex_count);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    return (int) draw_count;
}

/* -------------------------------------------------------------- BMP out */

static void write_u16le(FILE* f, unsigned int v)
{
    fputc((int) (v & 0xFF), f);
    fputc((int) ((v >> 8) & 0xFF), f);
}

static void write_u32le(FILE* f, unsigned int v)
{
    fputc((int) (v & 0xFF), f);
    fputc((int) ((v >> 8) & 0xFF), f);
    fputc((int) ((v >> 16) & 0xFF), f);
    fputc((int) ((v >> 24) & 0xFF), f);
}

int gx_gl_save_bmp(const char* path)
{
    unsigned char* pixels;
    unsigned char* rgba_pixels;
    size_t stride;
    size_t row;
    FILE* f;
    unsigned int data_size;

    if (program == 0) {
        return 0;
    }
    stride = ((size_t) gl_width * 3 + 3) & ~(size_t) 3;
    data_size = (unsigned int) (stride * (size_t) gl_height);
    pixels = (unsigned char*) malloc(data_size);
    rgba_pixels = (unsigned char*) malloc((size_t) gl_width * gl_height * 4);
    if (pixels == NULL || rgba_pixels == NULL) {
        free(pixels);
        free(rgba_pixels);
        return 0;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFinish();
    /* EGL pbuffers here only accept GL_RGBA readback. */
    glReadPixels(0, 0, gl_width, gl_height, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba_pixels);
    /* GL rows are bottom-up; BMP is bottom-up too.  Convert RGBA to BGR. */
    for (row = 0; row < (size_t) gl_height; ++row) {
        const unsigned char* src =
            rgba_pixels + row * (size_t) gl_width * 4;
        unsigned char* p = pixels + row * ((size_t) gl_width * 3);
        int x;
        for (x = 0; x < gl_width; ++x) {
            p[0] = src[2];
            p[1] = src[1];
            p[2] = src[0];
            p += 3;
            src += 4;
        }
    }

    f = fopen(path, "wb");
    if (f == NULL) {
        free(pixels);
        free(rgba_pixels);
        return 0;
    }
    fputc('B', f);
    fputc('M', f);
    write_u32le(f, 14 + 40 + data_size);
    write_u16le(f, 0);
    write_u16le(f, 0);
    write_u32le(f, 54);
    write_u32le(f, 40);
    write_u32le(f, (unsigned int) gl_width);
    write_u32le(f, (unsigned int) gl_height);
    write_u16le(f, 1);
    write_u16le(f, 24);
    write_u32le(f, 0);
    write_u32le(f, data_size);
    write_u32le(f, 2835);
    write_u32le(f, 2835);
    write_u32le(f, 0);
    write_u32le(f, 0);

    /* BMP rows are padded per stride; glReadPixels wrote tightly packed
     * rows, so copy row by row. */
    for (row = 0; row < (size_t) gl_height; ++row) {
        size_t row_bytes = (size_t) gl_width * 3;
        size_t pad = stride - row_bytes;
        fwrite(pixels + row * row_bytes, 1, row_bytes, f);
        while (pad-- > 0) {
            fputc(0, f);
        }
    }
    fclose(f);
    free(pixels);
    free(rgba_pixels);
    return 1;
}

int gx_gl_write_ppm(FILE* f)
{
    unsigned char* rgba_pixels;
    size_t row;
    int ok = 1;

    if (program == 0 || f == NULL) {
        return 0;
    }
    rgba_pixels = (unsigned char*) malloc((size_t) gl_width * gl_height * 4);
    if (rgba_pixels == NULL) {
        return 0;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFinish();
    glReadPixels(0, 0, gl_width, gl_height, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba_pixels);
    if (fprintf(f, "P6\n%d %d\n255\n", gl_width, gl_height) < 0) {
        ok = 0;
    }
    /* GL rows are bottom-up; PPM rows are top-down. */
    for (row = (size_t) gl_height; row-- > 0 && ok;) {
        const unsigned char* src = rgba_pixels + row * (size_t) gl_width * 4;
        size_t x;
        for (x = 0; x < (size_t) gl_width; ++x) {
            if (fputc(src[0], f) == EOF || fputc(src[1], f) == EOF ||
                fputc(src[2], f) == EOF)
            {
                ok = 0;
                break;
            }
            src += 4;
        }
    }
    free(rgba_pixels);
    return ok;
}

void gx_gl_shutdown(void)
{
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display, egl_surface);
        }
        if (egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display, egl_context);
        }
        eglTerminate(egl_display);
    }
    egl_display = EGL_NO_DISPLAY;
    egl_context = EGL_NO_CONTEXT;
    egl_surface = EGL_NO_SURFACE;
}
