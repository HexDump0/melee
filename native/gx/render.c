#include "gx/render.h"
#include "gx/gl.h"
#include "gx/overlay.h"
#include "gx/shader.h"
#include "platform/disc.h"
#include "hsd/parts.h"
#include "hsd/light.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Scene lights/fog for the next draw (HSD_LObj set loaded by the app). */
static SceneLights g_lights;
static int g_lights_loaded = 0;
#define MAX_SHADER_LIGHTS 4
#define GL_ANISO_EXT 0x84FEu
static int g_aniso_supported = 0;

void render_set_lights(const SceneLights *lights)
{
    if (lights != NULL) {
        g_lights = *lights;
        g_lights_loaded = 1;
    } else {
        g_lights_loaded = 0;
    }
}
/* -------------------------------------------------------------------------
 * Shaders.  Bodies are ES3-portable: the desktop build prepends
 * "#version 330"; a GLES build would prepend "#version 300 es" plus precision.
 */

/*
 * Model shader, a direct transcription of the common HSD/GX material paths:
 *   - MObjMakeTExp initial stage: RAS when RENDER_VERTEX, else the material
 *     diffuse/alpha constant.
 *   - TObjMakeTExp colormap/alphamap per texture (TEX_COLORMAP_* values).
 *   - RENDER_DIFFUSE final stage multiplies by the raster colour.
 *   - HSD_SetupPEMode alpha compare (GXCompare/GXAlphaOp) with discard.
 * The channel raster is `mat_ambient * light_ambient + light_diffuse*N.L`
 * (HSD_SetupChannelMode case 4 -> GX_Chan _60), not vertex_color * light:
 * the GX channel uses the registered material colour, not per-vertex colour.
 */
static const char *MODEL_VS =
    "layout(location=0) in vec3 a_position;\n"
    "layout(location=1) in vec3 a_normal;\n"
    "layout(location=2) in vec4 a_color;\n"
    "layout(location=3) in vec2 a_uv;\n"
    "layout(location=4) in vec2 a_uv2;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_modelview;\n"
    "uniform mat4 u_light_mv;\n"
    "uniform mat3 u_normal_mtx;\n"
    "uniform mat4 u_texmtx[2];\n"
    "uniform vec3 u_ambient_light;\n"
    "uniform vec3 u_mat_ambient;\n"
    "uniform float u_shininess;\n"
    "uniform int u_lighting;\n"
    "uniform int u_light_count;\n"
    "uniform int u_light_type[4];\n"
    "uniform vec4 u_light_color[4];\n"
    "uniform vec3 u_light_pos[4];\n"
    "uniform int u_spec_count;\n"
    "uniform int u_spec_type[4];\n"
    "uniform vec4 u_spec_color[4];\n"
    "uniform vec3 u_spec_pos[4];\n"
    "out vec4 v_vertex;\n"
    "out vec3 v_lit_front;\n"
    "out vec3 v_lit_back;\n"
    "out vec3 v_spec_front;\n"
    "out vec3 v_spec_back;\n"
    "out vec2 v_uv;\n"
    "out vec2 v_uv2;\n"
    "out float v_fog_dist;\n"
    /* HSD_SetupChannelMode case 4: ras = mat_ambient*ambient + sum(light*N.L)
     * over the diffuse mask.  Specular uses the same channel with the
     * material shininess (GX's specular pow is approximated). */
    "void main() {\n"
    "    vec4 view = u_modelview * vec4(a_position, 1.0);\n"
    "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "    v_fog_dist = -view.z;\n"
    "    v_uv = (u_texmtx[0] * vec4(a_uv, 0.0, 1.0)).xy;\n"
    "    v_uv2 = (u_texmtx[1] * vec4(a_uv2, 0.0, 1.0)).xy;\n"
    "    v_vertex = a_color;\n"
    "    if (u_lighting != 0) {\n"
    "        vec3 n = normalize(u_normal_mtx * a_normal);\n"
    "        vec3 amb = u_mat_ambient * u_ambient_light;\n"
    "        vec3 litf = amb;\n"
    "        vec3 litb = amb;\n"
    "        vec3 specf = vec3(0.0);\n"
    "        vec3 specb = vec3(0.0);\n"
    "        for (int i = 0; i < 4; ++i) {\n"
    "            if (i >= u_light_count) break;\n"
    "            vec3 lp = u_light_pos[i];\n"
    "            vec3 lv = (u_light_type[i] == 1) ? normalize(lp)\n"
    "                                             : normalize(lp - view.xyz);\n"
    "            litf += u_light_color[i].rgb * max(dot(n, lv), 0.0);\n"
    "            litb += u_light_color[i].rgb * max(dot(-n, lv), 0.0);\n"
    "        }\n"
    "        for (int i = 0; i < 4; ++i) {\n"
    "            if (i >= u_spec_count) break;\n"
    "            vec3 lp = u_spec_pos[i];\n"
    "            vec3 lv = (u_spec_type[i] == 1) ? normalize(lp)\n"
    "                                            : normalize(lp - view.xyz);\n"
    "            vec3 h = normalize(lv + vec3(0.0, 0.0, 1.0));\n"
    "            float p = pow(max(dot(n, h), 0.0), max(u_shininess, 1.0));\n"
    "            float pb = pow(max(dot(-n, h), 0.0), max(u_shininess, 1.0));\n"
    "            specf += u_spec_color[i].rgb * p;\n"
    "            specb += u_spec_color[i].rgb * pb;\n"
    "        }\n"
    "        v_lit_front = clamp(litf, 0.0, 1.0);\n"
    "        v_lit_back = clamp(litb, 0.0, 1.0);\n"
    "        v_spec_front = clamp(specf, 0.0, 1.0);\n"
    "        v_spec_back = clamp(specb, 0.0, 1.0);\n"
    "    } else {\n"
    "        v_lit_front = vec3(1.0);\n"
    "        v_lit_back = vec3(1.0);\n"
    "        v_spec_front = vec3(0.0);\n"
    "        v_spec_back = vec3(0.0);\n"
    "    }\n"
    "}\n";

static const char *MODEL_FS =
    "uniform sampler2D u_tex0;\n"
    "uniform sampler2D u_tex1;\n"
    "uniform int u_tex_count;\n"
    "uniform int u_texsrc[2];\n"
    "uniform int u_cmap[2];\n"
    "uniform int u_amap[2];\n"
    "uniform float u_tex_blend[2];\n"
    "uniform float u_tex_lod[2];\n"
    "uniform int u_ras_lit;\n"
    "uniform int u_initial_ras;\n"
    "uniform int u_diffuse_mul;\n"
    "uniform int u_specular_tev;\n"
    "uniform vec4 u_material;\n"
    "uniform vec3 u_mat_specular;\n"
    "uniform int u_tex_phase[2];\n" /* 0 diffuse/ambient, 1 specular, 2 ext */
    "uniform int u_tex_repeat[2];\n" /* lightmap_done: skip the alpha map */
    "uniform int u_alpha_test;\n"
    "uniform int u_acomp[2];\n"
    "uniform float u_aref[2];\n"
    "uniform int u_aop;\n"
    "uniform int u_fog_enable;\n"
    "uniform int u_fog_type;\n"
    "uniform float u_fog_start;\n"
    "uniform float u_fog_end;\n"
    "uniform vec3 u_fog_color;\n"
    "in vec4 v_vertex;\n"
    "in vec3 v_lit_front;\n"
    "in vec3 v_lit_back;\n"
    "in vec3 v_spec_front;\n"
    "in vec3 v_spec_back;\n"
    "in vec2 v_uv;\n"
    "in vec2 v_uv2;\n"
    "in float v_fog_dist;\n"
    "out vec4 frag_color;\n"
    /* GX TEV: out = (d + (1-c)*a + c*b), clamped; the HSD colormap/alphamap
     * branches map to a=prev, b=texture, c=texture/alpha/blend as below. */
    "vec3 tev_colormap(int mode, float blend, vec3 prev, vec4 t) {\n"
    "    if (mode == 1) return clamp(mix(prev, t.rgb, t.a), 0.0, 1.0);\n"
    "    if (mode == 2) return clamp(mix(prev, t.rgb * t.rgb, t.rgb), 0.0, 1.0);\n"
    "    if (mode == 3) return clamp(mix(prev, t.rgb, blend), 0.0, 1.0);\n"
    "    if (mode == 4) return clamp(prev * t.rgb, 0.0, 1.0);\n"
    "    if (mode == 5) return t.rgb;\n"
    "    if (mode == 7) return clamp(prev + t.rgb, 0.0, 1.0);\n"
    "    if (mode == 8) return clamp(prev - t.rgb, 0.0, 1.0);\n"
    "    return prev;\n"
    "}\n"
    "float tev_alphamap(int mode, float blend, float prev, vec4 t) {\n"
    "    if (mode == 1) return clamp(mix(prev, t.a * t.a, t.a), 0.0, 1.0);\n"
    "    if (mode == 2) return clamp(mix(prev, t.a, blend), 0.0, 1.0);\n"
    "    if (mode == 3) return clamp(prev * t.a, 0.0, 1.0);\n"
    "    if (mode == 4) return t.a;\n"
    "    if (mode == 6) return clamp(prev + t.a, 0.0, 1.0);\n"
    "    if (mode == 7) return clamp(prev - t.a, 0.0, 1.0);\n"
    "    return prev;\n"
    "}\n"
    "bool gx_compare(int func, int a, int ref) {\n"
    "    if (func == 0) return false;\n"
    "    if (func == 1) return a < ref;\n"
    "    if (func == 2) return a == ref;\n"
    "    if (func == 3) return a <= ref;\n"
    "    if (func == 4) return a > ref;\n"
    "    if (func == 5) return a != ref;\n"
    "    if (func == 6) return a >= ref;\n"
    "    return true;\n"
    "}\n";

static const char *MODEL_FS_MAIN =
    "void main() {\n"
    "    vec3 lit = gl_FrontFacing ? v_lit_front : v_lit_back;\n"
    "    vec3 spec_lit = gl_FrontFacing ? v_spec_front : v_spec_back;\n"
    "    vec4 ras = vec4(u_ras_lit != 0 ? lit : v_vertex.rgb, v_vertex.a);\n"
    "    vec4 color = u_initial_ras != 0 ? ras : u_material;\n"
    "    vec4 t0 = vec4(1.0);\n"
    "    vec4 t1 = vec4(1.0);\n"
    "    if (u_tex_count > 0)\n"
    "        t0 = texture(u_tex0, (u_texsrc[0] == 5) ? v_uv2 : v_uv, u_tex_lod[0]);\n"
    "    if (u_tex_count > 1)\n"
    "        t1 = texture(u_tex1, (u_texsrc[1] == 5) ? v_uv2 : v_uv, u_tex_lod[1]);\n"
    /* MObjMakeTExp phase order: DIFFUSE/AMBIENT textures, then the
     * RENDER_DIFFUSE raster multiply, then SPECULAR lightmaps accumulated
     * into mat.specular and multiplied by the specular channel, then EXT. */
    "    if (u_tex_count > 0 && u_tex_phase[0] == 0) {\n"
    "        color.rgb = tev_colormap(u_cmap[0], u_tex_blend[0], color.rgb, t0);\n"
    "        if (u_tex_repeat[0] == 0) color.a = tev_alphamap(u_amap[0], u_tex_blend[0], color.a, t0);\n"
    "    }\n"
    "    if (u_tex_count > 1 && u_tex_phase[1] == 0) {\n"
    "        color.rgb = tev_colormap(u_cmap[1], u_tex_blend[1], color.rgb, t1);\n"
    "        if (u_tex_repeat[1] == 0) color.a = tev_alphamap(u_amap[1], u_tex_blend[1], color.a, t1);\n"
    "    }\n"
    "    if (u_diffuse_mul != 0) {\n"
    "        color.rgb = clamp(color.rgb * ras.rgb, 0.0, 1.0);\n"
    "        color.a = clamp(color.a * ras.a, 0.0, 1.0);\n"
    "    }\n"
    "    if (u_specular_tev != 0) {\n"
    "        vec3 spec = u_mat_specular;\n"
    "        if (u_tex_count > 0 && u_tex_phase[0] == 1)\n"
    "            spec = tev_colormap(u_cmap[0], u_tex_blend[0], spec, t0);\n"
    "        if (u_tex_count > 1 && u_tex_phase[1] == 1)\n"
    "            spec = tev_colormap(u_cmap[1], u_tex_blend[1], spec, t1);\n"
    "        spec *= spec_lit;\n"
    "        color.rgb = clamp(color.rgb + spec, 0.0, 1.0);\n"
    "    }\n"
    "    if (u_tex_count > 0 && u_tex_phase[0] == 2) {\n"
    "        color.rgb = tev_colormap(u_cmap[0], u_tex_blend[0], color.rgb, t0);\n"
    "        if (u_tex_repeat[0] == 0) color.a = tev_alphamap(u_amap[0], u_tex_blend[0], color.a, t0);\n"
    "    }\n"
    "    if (u_tex_count > 1 && u_tex_phase[1] == 2) {\n"
    "        color.rgb = tev_colormap(u_cmap[1], u_tex_blend[1], color.rgb, t1);\n"
    "        if (u_tex_repeat[1] == 0) color.a = tev_alphamap(u_amap[1], u_tex_blend[1], color.a, t1);\n"
    "    }\n"
    "    if (u_alpha_test != 0) {\n"
    "        int av = int(color.a * 255.0 + 0.5);\n"
    "        bool p0 = gx_compare(u_acomp[0], av, int(u_aref[0] + 0.5));\n"
    "        bool p1 = gx_compare(u_acomp[1], av, int(u_aref[1] + 0.5));\n"
    "        bool pass = (u_aop == 1) ? (p0 || p1) :\n"
    "                    (u_aop == 2) ? (p0 != p1) :\n"
    "                    (u_aop == 3) ? (p0 == p1) : (p0 && p1);\n"
    "        if (!pass) discard;\n"
    "    }\n"
    /* HSD_FogSet -> GXSetFog: linear perspective fog uses the view distance;
     * the exponential modes use the same distance with a derived density. */
    "    if (u_fog_enable != 0) {\n"
    "        float d = v_fog_dist;\n"
    "        float f;\n"
    "        if (u_fog_type == 2) {\n"
    "            f = clamp((u_fog_end - d) / max(u_fog_end - u_fog_start, 1e-4), 0.0, 1.0);\n"
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
    "    frag_color = color;\n"
    "}\n";

typedef struct ModelShader {
    GLuint program;
    GLint mvp;
    GLint normal_mtx;
    GLint texmtx;        /* u_texmtx[0], two matrices per draw */
    GLint tex[2];
    GLint tex_count;
    GLint texsrc;        /* u_texsrc[0], two ints */
    GLint cmap;
    GLint amap;
    GLint tex_blend;
    GLint tex_lod;
    GLint modelview;
    GLint light_mv;
    GLint ambient_light;
    GLint light_count;
    GLint light_type;
    GLint light_color;
    GLint light_pos;
    GLint spec_count;
    GLint spec_type;
    GLint spec_color;
    GLint spec_pos;
    GLint mat_ambient;
    GLint shininess;
    GLint lighting;
    GLint ras_lit;
    GLint initial_ras;
    GLint diffuse_mul;
    GLint specular_tev;
    GLint material;
    GLint mat_specular;
    GLint tex_phase;
    GLint tex_repeat;
    GLint alpha_test;
    GLint acomp;
    GLint aref;
    GLint aop;
    GLint fog_enable;
    GLint fog_type;
    GLint fog_start;
    GLint fog_end;
    GLint fog_color;
} ModelShader;

static ModelShader g_model;

int renderer_init(void)
{
    GLuint vs,fs;
    g_aniso_supported=
        SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")?1:0;
    vs=gx_compile_shader(GL_VERTEX_SHADER,MODEL_VS,NULL);
    fs=gx_compile_shader(GL_FRAGMENT_SHADER,MODEL_FS,MODEL_FS_MAIN);
    if(!vs||!fs)return 0;
    g_model.program=gx_link_program(vs,fs,"model");
    if(!g_model.program)return 0;
    g_model.mvp=glGetUniformLocation(g_model.program,"u_mvp");
    g_model.normal_mtx=glGetUniformLocation(g_model.program,"u_normal_mtx");
    g_model.texmtx=glGetUniformLocation(g_model.program,"u_texmtx[0]");
    g_model.tex[0]=glGetUniformLocation(g_model.program,"u_tex0");
    g_model.tex[1]=glGetUniformLocation(g_model.program,"u_tex1");
    g_model.tex_count=glGetUniformLocation(g_model.program,"u_tex_count");
    g_model.texsrc=glGetUniformLocation(g_model.program,"u_texsrc[0]");
    g_model.cmap=glGetUniformLocation(g_model.program,"u_cmap[0]");
    g_model.amap=glGetUniformLocation(g_model.program,"u_amap[0]");
    g_model.tex_blend=glGetUniformLocation(g_model.program,"u_tex_blend[0]");
    g_model.tex_lod=glGetUniformLocation(g_model.program,"u_tex_lod[0]");
    g_model.modelview=glGetUniformLocation(g_model.program,"u_modelview");
    g_model.light_mv=glGetUniformLocation(g_model.program,"u_light_mv");
    g_model.ambient_light=glGetUniformLocation(g_model.program,"u_ambient_light");
    g_model.light_count=glGetUniformLocation(g_model.program,"u_light_count");
    g_model.light_type=glGetUniformLocation(g_model.program,"u_light_type[0]");
    g_model.light_color=glGetUniformLocation(g_model.program,"u_light_color[0]");
    g_model.light_pos=glGetUniformLocation(g_model.program,"u_light_pos[0]");
    g_model.spec_count=glGetUniformLocation(g_model.program,"u_spec_count");
    g_model.spec_type=glGetUniformLocation(g_model.program,"u_spec_type[0]");
    g_model.spec_color=glGetUniformLocation(g_model.program,"u_spec_color[0]");
    g_model.spec_pos=glGetUniformLocation(g_model.program,"u_spec_pos[0]");
    g_model.mat_ambient=glGetUniformLocation(g_model.program,"u_mat_ambient");
    g_model.shininess=glGetUniformLocation(g_model.program,"u_shininess");
    g_model.lighting=glGetUniformLocation(g_model.program,"u_lighting");
    g_model.ras_lit=glGetUniformLocation(g_model.program,"u_ras_lit");
    g_model.initial_ras=glGetUniformLocation(g_model.program,"u_initial_ras");
    g_model.diffuse_mul=glGetUniformLocation(g_model.program,"u_diffuse_mul");
    g_model.specular_tev=glGetUniformLocation(g_model.program,"u_specular_tev");
    g_model.material=glGetUniformLocation(g_model.program,"u_material");
    g_model.mat_specular=glGetUniformLocation(g_model.program,"u_mat_specular");
    g_model.tex_phase=glGetUniformLocation(g_model.program,"u_tex_phase[0]");
    g_model.tex_repeat=glGetUniformLocation(g_model.program,"u_tex_repeat[0]");
    g_model.alpha_test=glGetUniformLocation(g_model.program,"u_alpha_test");
    g_model.acomp=glGetUniformLocation(g_model.program,"u_acomp[0]");
    g_model.aref=glGetUniformLocation(g_model.program,"u_aref[0]");
    g_model.aop=glGetUniformLocation(g_model.program,"u_aop");
    g_model.fog_enable=glGetUniformLocation(g_model.program,"u_fog_enable");
    g_model.fog_type=glGetUniformLocation(g_model.program,"u_fog_type");
    g_model.fog_start=glGetUniformLocation(g_model.program,"u_fog_start");
    g_model.fog_end=glGetUniformLocation(g_model.program,"u_fog_end");
    g_model.fog_color=glGetUniformLocation(g_model.program,"u_fog_color");
    glUseProgram(g_model.program);
    glUniform1i(g_model.tex[0],0);
    glUniform1i(g_model.tex[1],1);

    if (!overlay_init()) return 0;
    return 1;
}

/* Light colours are the viewer's stand-in light set; the GX channel consumes
 * them as ambient/diffuse light colours (HSD_SetupChannelMode). */
/*
 * Builds the GX channel inputs from the loaded HSD_LObj set
 * (HSD_LObjSetupInit + HSD_SetupChannelMode: ambient slot, diffuse mask,
 * specular mask) and transforms them into the light matrix's space.
 * Falls back to the pre-scene stand-in light if the archive is unavailable.
 */
void render_set_view(const Mat4 mvp,const Mat4 mv,const Mat4 light_mv,
                           int lighting)
{
    float normal[9];
    float ambient[3]={.78f,.78f,.82f};
    int light_type[MAX_SHADER_LIGHTS]={0};
    float light_color[MAX_SHADER_LIGHTS*4]={0};
    float light_pos[MAX_SHADER_LIGHTS*3]={0};
    int spec_type[MAX_SHADER_LIGHTS]={0};
    float spec_color[MAX_SHADER_LIGHTS*4]={0};
    float spec_pos[MAX_SHADER_LIGHTS*3]={0};
    int light_count=0,spec_count=0;
    size_t i;
    if(g_lights_loaded) {
        lights_ambient(&g_lights,ambient);
        for(i=0;i<g_lights.count&&light_count<MAX_SHADER_LIGHTS;++i) {
            const SceneLight *l=&g_lights.lights[i];
            float p[3];
            int c;
            if((l->flags&3u)==LOBJ_AMBIENT||
               (l->flags&LOBJ_HIDDEN)||!(l->flags&LOBJ_DIFFUSE))
                continue;
            c=light_count++;
            light_type[c]=(int)l->type;
            light_color[c*4+0]=l->color[0]/255.0f;
            light_color[c*4+1]=l->color[1]/255.0f;
            light_color[c*4+2]=l->color[2]/255.0f;
            light_color[c*4+3]=l->color[3]/255.0f;
            if(l->type==LOBJ_INFINITE) {
                m4_transform_dir(p,light_mv,l->position);
            } else if(l->has_position) {
                m4_transform_point(p,light_mv,l->position);
            }
            memcpy(&light_pos[c*3],p,sizeof(p));
        }
        for(i=0;i<g_lights.count&&spec_count<MAX_SHADER_LIGHTS;++i) {
            const SceneLight *l=&g_lights.lights[i];
            float p[3];
            int c;
            if((l->flags&3u)==LOBJ_AMBIENT||
               (l->flags&LOBJ_HIDDEN)||!(l->flags&LOBJ_SPECULAR))
                continue;
            c=spec_count++;
            spec_type[c]=(int)l->type;
            spec_color[c*4+0]=l->color[0]/255.0f;
            spec_color[c*4+1]=l->color[1]/255.0f;
            spec_color[c*4+2]=l->color[2]/255.0f;
            spec_color[c*4+3]=l->color[3]/255.0f;
            if(l->type==LOBJ_INFINITE) {
                m4_transform_dir(p,light_mv,l->position);
            } else if(l->has_position) {
                m4_transform_point(p,light_mv,l->position);
            }
            memcpy(&spec_pos[c*3],p,sizeof(p));
        }
    } else {
        float p[3];
        const float world[3]={.35f,.6f,1.0f};
        light_count=1;
        spec_count=1;
        light_type[0]=LOBJ_INFINITE;
        spec_type[0]=LOBJ_INFINITE;
        light_color[0]=light_color[1]=light_color[2]=.9f;
        spec_color[0]=spec_color[1]=spec_color[2]=.9f;
        light_color[3]=spec_color[3]=1.0f;
        m4_transform_dir(p,light_mv,world);
        memcpy(light_pos,p,sizeof(p));
        memcpy(spec_pos,p,sizeof(p));
    }
    m4_normal_mtx(normal,mv);
    glUseProgram(g_model.program);
    glUniformMatrix4fv(g_model.mvp,1,GL_FALSE,mvp);
    glUniformMatrix4fv(g_model.modelview,1,GL_FALSE,mv);
    glUniformMatrix4fv(g_model.light_mv,1,GL_FALSE,light_mv);
    glUniformMatrix3fv(g_model.normal_mtx,1,GL_FALSE,normal);
    glUniform1i(g_model.lighting,lighting);
    glUniform3f(g_model.ambient_light,ambient[0],ambient[1],ambient[2]);
    glUniform1i(g_model.light_count,light_count);
    glUniform1iv(g_model.light_type,MAX_SHADER_LIGHTS,light_type);
    glUniform4fv(g_model.light_color,MAX_SHADER_LIGHTS,light_color);
    glUniform3fv(g_model.light_pos,MAX_SHADER_LIGHTS,light_pos);
    glUniform1i(g_model.spec_count,spec_count);
    glUniform1iv(g_model.spec_type,MAX_SHADER_LIGHTS,spec_type);
    glUniform4fv(g_model.spec_color,MAX_SHADER_LIGHTS,spec_color);
    glUniform3fv(g_model.spec_pos,MAX_SHADER_LIGHTS,spec_pos);
    if(g_lights_loaded&&g_lights.fog.present) {
        glUniform1i(g_model.fog_enable,1);
        glUniform1i(g_model.fog_type,(int)g_lights.fog.type);
        glUniform1f(g_model.fog_start,g_lights.fog.start);
        glUniform1f(g_model.fog_end,g_lights.fog.end);
        glUniform3f(g_model.fog_color,g_lights.fog.color[0]/255.0f,
                    g_lights.fog.color[1]/255.0f,g_lights.fog.color[2]/255.0f);
    } else {
        glUniform1i(g_model.fog_enable,0);
    }
}

static GLenum gx_blend_factor(uint8_t f)
{
    switch(f) {
    case 0: return GL_ZERO;                /* GX_BL_ZERO */
    case 1: return GL_ONE;                 /* GX_BL_ONE */
    case 2: return GL_SRC_COLOR;           /* GX_BL_SRCCLR */
    case 3: return GL_ONE_MINUS_SRC_COLOR; /* GX_BL_INVSRCCLR */
    case 4: return GL_SRC_ALPHA;           /* GX_BL_SRCALPHA */
    case 5: return GL_ONE_MINUS_SRC_ALPHA; /* GX_BL_INVSRCALPHA */
    case 6: return GL_DST_ALPHA;           /* GX_BL_DSTALPHA */
    case 7: return GL_ONE_MINUS_DST_ALPHA; /* GX_BL_INVDSTALPHA */
    default: return GL_ONE;
    }
}

static GLenum gx_depth_func(uint8_t f)
{
    switch(f) {
    case 0: return GL_NEVER;
    case 1: return GL_LESS;
    case 2: return GL_EQUAL;
    case 3: return GL_LEQUAL;
    case 4: return GL_GREATER;
    case 5: return GL_NOTEQUAL;
    case 6: return GL_GEQUAL;
    case 7: return GL_ALWAYS;
    default: return GL_LEQUAL;
    }
}

/* HSD_MObjSetup -> HSD_SetupRenderModeWithCustomPE -> HSD_SetupPEMode. */
static void apply_material_state(const HsdMaterial *mat)
{
    if(mat->blend==1||mat->blend==3) {
        glDisable(GL_COLOR_LOGIC_OP);
        glEnable(GL_BLEND);
        glBlendFunc(gx_blend_factor(mat->blend_src),
                    gx_blend_factor(mat->blend_dst));
        glBlendEquation(mat->blend==3?GL_FUNC_REVERSE_SUBTRACT:GL_FUNC_ADD);
    } else if(mat->blend==2) {
        glDisable(GL_BLEND);
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp((GLenum)(0x1500+mat->blend_op)); /* GX_LO_* == GL order */
    } else {
        glDisable(GL_BLEND);
        glDisable(GL_COLOR_LOGIC_OP);
        glBlendEquation(GL_FUNC_ADD);
    }
    if(mat->z_enable)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    glDepthFunc(gx_depth_func(mat->z_func));
    glDepthMask(mat->z_update?GL_TRUE:GL_FALSE);
}

/* Restore the default state overlays and the HUD expect. */
void render_reset_state(void)
{
    glDisable(GL_COLOR_LOGIC_OP);
    glBlendEquation(GL_FUNC_ADD);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
}

static int symbol_log(const char *name, unsigned int off, void *user)
{
    (void)user;
    printf("  %s @ 0x%x\n",name,off);
    return 0;
}

int visual_load_model(Visual *v, const char *disc, const char *file,
                      int vis_slot, int vis_variant)
{
    DiscFile asset = {0};
    char error[256];
    if (disc_load(disc,file,&asset,error,sizeof(error))) {
        fprintf(stderr,"%s: %s\n",file,error); return 0;
    }
    printf("Loaded %s: %zu bytes\n",file,asset.size);
    disc_enumerate_public_symbols(&asset,symbol_log,NULL,error,sizeof(error));
    HsdVertex *storage = calloc(300000,sizeof(*storage));
    if (!storage) { disc_free(&asset); return 0; }
    hsd_model_init(&v->model,storage,300000);
    int ok = hsd_model_load(&v->model,asset.data,asset.size,0,error,sizeof(error));
    disc_free(&asset);
    if (!ok || !v->model.vertex_count) {
        fprintf(stderr,"Model %s: %s (%zu vertices)\n",file,error,v->model.vertex_count);
        free(storage); memset(v,0,sizeof(*v)); return 0;
    }
    {
        char parts_error[128];
        int parts = parts_apply(disc,file,&v->model,vis_slot,
                                     vis_variant,parts_error,
                                     sizeof(parts_error));
        /* parts_apply reads ftData<Char>'s model_scaling; re-evaluate the
         * bind pose so the vertices and bounds include it. */
        hsd_model_pose_apply(&v->model);
        printf("Decoded %s: %zu triangles, %zu textures; bounds [%.2f %.2f %.2f] to [%.2f %.2f %.2f]\n",
               file,v->model.vertex_count/3,v->model.texture_count,v->model.bounds_min[0],v->model.bounds_min[1],
               v->model.bounds_min[2],v->model.bounds_max[0],v->model.bounds_max[1],v->model.bounds_max[2]);
        printf("PObj types: skin %zu, shapeanim %zu, envelope %zu; joints %zu, instances %zu\n",
               v->model.pobj_type_count[0],v->model.pobj_type_count[1],
               v->model.pobj_type_count[2],v->model.joint_count,v->model.instance_count);
        if(parts==0)printf("Parts visibility: %zu of %zu objects hidden (neutral pose; model scale %.4f, x %.4f)\n",
                           parts_hidden_count(&v->model),v->model.dobj_count,
                           (double)v->model.model_scale,
                           (double)(v->model.model_scale_x>0.0f
                                        ?v->model.model_scale_x
                                        :v->model.model_scale));
        else if(parts<0)fprintf(stderr,"Parts visibility failed: %s\n",parts_error);
    }
    {
        float height = v->model.bounds_max[1]-v->model.bounds_min[1];
        v->scale = height > .001f ? 11.0f/height : 1;
    }
    return 1;
}

int visual_load_anim(Visual *v,const char *disc,const char *file,const char *anim_file)
{
    char error[256];
    if(v->anim_loaded){anim_free(&v->anim);v->anim_loaded=0;}
    if(anim_load(&v->anim,disc,file,anim_file,error,sizeof(error))!=0) {
        fprintf(stderr,"Animation for %s: %s\n",file,error);
        return 0;
    }
    v->anim_loaded=1;
    return 1;
}

static GLint gx_wrap_to_gl(int wrap)
{
    if(wrap==1)return GL_REPEAT;
    if(wrap==2)return GL_MIRRORED_REPEAT;
    return GL_CLAMP_TO_EDGE;
}

static void apply_wrap(int wrap_s,int wrap_t)
{
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,gx_wrap_to_gl(wrap_s));
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,gx_wrap_to_gl(wrap_t));
}

/* HSD_TObjSetup + GXInitTexObjLOD: default min filter GX_LIN_MIP_LIN, CI
 * textures downgrade it to GX_LIN_MIP_NEAR, and non-mipmapped images keep
 * only the nearest/linear bit. */
static GLenum gx_min_filter(uint8_t f,uint8_t mipmap,uint32_t format)
{
    if(!mipmap)f=(uint8_t)(f&1u);
    if(f==5&&(format==8||format==9||format==10))f=3;
    switch(f) {
    case 0: return GL_NEAREST;
    case 1: return GL_LINEAR;
    case 2: return GL_NEAREST_MIPMAP_NEAREST;
    case 3: return GL_LINEAR_MIPMAP_NEAREST;
    case 4: return GL_NEAREST_MIPMAP_LINEAR;
    default: return GL_LINEAR_MIPMAP_LINEAR;
    }
}

static GLenum gx_mag_filter(uint8_t f)
{
    return f==0?GL_NEAREST:GL_LINEAR;
}

static void apply_tex_filter(const HsdTobj *t,
                             const HsdTexture *tex)
{
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,
                    gx_min_filter(t->minfilt,tex->mipmap,tex->format));
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,
                    gx_mag_filter(t->magfilt));
    if(g_aniso_supported&&t->anisotropy>0&&t->anisotropy<4)
        glTexParameterf(GL_TEXTURE_2D,GL_ANISO_EXT,
                        (float)(1u<<t->anisotropy));
}

void render_apply_cull(int cull)
{
    if(cull==0) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(cull==1?GL_FRONT:GL_BACK);
    }
}

static void bind_batch_attribs(void)
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(HsdVertex),
                          (const void*)(uintptr_t)offsetof(HsdVertex,position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(HsdVertex),
                          (const void*)(uintptr_t)offsetof(HsdVertex,normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(HsdVertex),
                          (const void*)(uintptr_t)offsetof(HsdVertex,color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(HsdVertex),
                          (const void*)(uintptr_t)offsetof(HsdVertex,uv));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4,2,GL_FLOAT,GL_FALSE,sizeof(HsdVertex),
                          (const void*)(uintptr_t)offsetof(HsdVertex,uv2));
}

static void upload_batch(const Visual *v,size_t bi,int pose)
{
    const HsdBatch *b=&v->model.batches[bi];
    glBindBuffer(GL_ARRAY_BUFFER,pose?v->pose_vbo[bi]:v->batch_vbo[bi]);
    glBufferSubData(GL_ARRAY_BUFFER,0,
                    (GLsizeiptr)(b->vertex_count*sizeof(HsdVertex)),
                    v->model.vertices+b->first_vertex);
}

/*
 * One draw per PObj batch.  The material state is HSD_MObjSetup's: textures
 * and per-TObj TEV inputs, the raster colour (channel lit or vertex), the
 * material constant, alpha test and GX blend/z state.
 */
void render_batch(const Visual *v,size_t bi,int pose,int textured)
{
    const HsdBatch *b=&v->model.batches[bi];
    const HsdMaterial *mat=&b->material;
    int texsrc[2]={4,5};
    int cmap[2]={0,0};
    int amap[2]={0,0};
    int phase[2]={0,0};
    int repeat[2]={0,0};
    int seen_phase[3]={0,0,0};
    float blend[2]={0.0f,0.0f};
    float lod[2]={0.0f,0.0f};
    float mtx[32];
    int acomp[2];
    float aref[2];
    int count=0;
    int i;
    glBindVertexArray(pose?v->pose_vao[bi]:v->batch_vao[bi]);
    if(textured) {
        for(i=0;i<mat->tobj_count&&i<2;++i) {
            const HsdTobj *t=&mat->tobjs[i];
            if(t->texture<0||(size_t)t->texture>=v->model.texture_count||
               !v->textures[t->texture])
                continue; /* HSD skips TObjs with no image (id == NULL) */
            glActiveTexture(GL_TEXTURE0+(GLenum)count);
            glBindTexture(GL_TEXTURE_2D,v->textures[t->texture]);
            apply_wrap(t->wrap_s,t->wrap_t);
            apply_tex_filter(t,&v->model.textures[t->texture]);
            texsrc[count]=t->src==5?5:4; /* GX_TG_TEX0 / GX_TG_TEX1 */
            cmap[count]=(int)((t->flags>>16)&0xf);
            amap[count]=(int)((t->flags>>20)&0xf);
            /* TObjMakeTExp phase: DIFFUSE/AMBIENT, SPECULAR lightmap, EXT.
             * `repeat` (lightmap_done) skips the alpha map for a phase that
             * was already applied. */
            phase[count]=(t->flags&0x20u)?1:((t->flags&0x80u)?2:0);
            repeat[count]=seen_phase[phase[count]]++;
            blend[count]=t->blending;
            lod[count]=t->lod_bias;
            count++;
        }
        glActiveTexture(GL_TEXTURE0);
    }
    memcpy(mtx,b->texmtx,sizeof(b->texmtx));
    memcpy(mtx+16,b->texmtx2,sizeof(b->texmtx2));
    acomp[0]=mat->alpha_comp0;
    acomp[1]=mat->alpha_comp1;
    aref[0]=(float)mat->alpha_ref0;
    aref[1]=(float)mat->alpha_ref1;
    glUniformMatrix4fv(g_model.texmtx,2,GL_FALSE,mtx);
    glUniform1iv(g_model.texsrc,2,texsrc);
    glUniform1iv(g_model.cmap,2,cmap);
    glUniform1iv(g_model.amap,2,amap);
    glUniform1iv(g_model.tex_phase,2,phase);
    glUniform1iv(g_model.tex_repeat,2,repeat);
    glUniform1fv(g_model.tex_blend,2,blend);
    glUniform1fv(g_model.tex_lod,2,lod);
    glUniform1i(g_model.tex_count,count);
    glUniform1i(g_model.ras_lit,mat->channel_lit);
    glUniform1i(g_model.initial_ras,mat->initial_ras);
    glUniform1i(g_model.diffuse_mul,mat->diffuse_mul);
    glUniform1i(g_model.specular_tev,mat->specular_tev);
    {
        const uint8_t *diff=mat->diffuse;
        if(v->model.has_override_diffuse)diff=v->model.override_diffuse;
        glUniform4f(g_model.material,diff[0]/255.0f,diff[1]/255.0f,
                    diff[2]/255.0f,mat->alpha);
    }
    glUniform3f(g_model.mat_ambient,mat->ambient[0]/255.0f,
                mat->ambient[1]/255.0f,mat->ambient[2]/255.0f);
    glUniform3f(g_model.mat_specular,mat->specular[0]/255.0f,
                mat->specular[1]/255.0f,mat->specular[2]/255.0f);
    glUniform1f(g_model.shininess,mat->shininess);
    glUniform1iv(g_model.acomp,2,acomp);
    glUniform1fv(g_model.aref,2,aref);
    glUniform1i(g_model.alpha_test,mat->alpha_test);
    glUniform1i(g_model.aop,mat->alpha_op);
    apply_material_state(mat);
    if(pose)upload_batch(v,bi,1);
    glDrawArrays(GL_TRIANGLES,0,(GLsizei)b->vertex_count);
}

void visual_compile(Visual *v)
{
    size_t i;
    for(i=0;i<v->model.texture_count&&i<HSD_MAX_TEXTURES;++i) {
        HsdTexture *t=&v->model.textures[i];
        if(t->rgba==NULL)continue;
        glGenTextures(1,&v->textures[i]);
        glBindTexture(GL_TEXTURE_2D,v->textures[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t->width,t->height,0,GL_RGBA,
                     GL_UNSIGNED_BYTE,t->rgba);
        /* HSD's default LOD mode is GX_LIN_MIP_LIN (trilinear); the core
         * profile spells GL_GENERATE_MIPMAP as an explicit glGenerateMipmap. */
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    }
    for(i=0;i<v->model.batch_count&&i<HSD_MAX_BATCHES;++i) {
        HsdBatch *b=&v->model.batches[i];
        if(b->cull_mode==3)continue; /* POBJ_CULLFRONT|CULLBACK: never drawn */
        /* Immutable bind-pose copy. */
        glGenVertexArrays(1,&v->batch_vao[i]);
        glGenBuffers(1,&v->batch_vbo[i]);
        glBindVertexArray(v->batch_vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER,v->batch_vbo[i]);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(b->vertex_count*sizeof(HsdVertex)),
                     v->model.vertices+b->first_vertex,GL_STATIC_DRAW);
        bind_batch_attribs();
        /* Per-frame posed copy, refreshed by upload_batch. */
        glGenVertexArrays(1,&v->pose_vao[i]);
        glGenBuffers(1,&v->pose_vbo[i]);
        glBindVertexArray(v->pose_vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER,v->pose_vbo[i]);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(b->vertex_count*sizeof(HsdVertex)),
                     NULL,GL_DYNAMIC_DRAW);
        bind_batch_attribs();
    }
    glBindVertexArray(0);
}

void visual_destroy(Visual *v)
{
    size_t i;
    anim_free(&v->anim);
    v->anim_loaded=0;
    for(i=0;i<v->model.batch_count&&i<HSD_MAX_BATCHES;++i) {
        if(v->batch_vao[i])glDeleteVertexArrays(1,&v->batch_vao[i]);
        if(v->batch_vbo[i])glDeleteBuffers(1,&v->batch_vbo[i]);
        if(v->pose_vao[i])glDeleteVertexArrays(1,&v->pose_vao[i]);
        if(v->pose_vbo[i])glDeleteBuffers(1,&v->pose_vbo[i]);
    }
    for(i=0;i<v->model.texture_count&&i<HSD_MAX_TEXTURES;++i) {
        if(v->textures[i])glDeleteTextures(1,&v->textures[i]);
    }
    hsd_model_free(&v->model);
    free(v->model.vertices);
    v->model.vertices=NULL;
}
