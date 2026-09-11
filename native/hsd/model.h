#ifndef MELEE_NATIVE_HSD_MODEL_H
#define MELEE_NATIVE_HSD_MODEL_H

/*
 * Raw HSD model reader for the native port.
 *
 * Melee character archives store GameCube GPU data.  This module expands the
 * bind-pose geometry, texture coordinates, material colors and the embedded
 * textures into a renderer-independent representation.  It does not run the
 * HSD engine and does not evaluate skinning or animation.
 */
#include <stddef.h>
#include <stdint.h>

#define HSD_MAX_TEXTURES 128
#define HSD_MAX_BATCHES 512
#define HSD_MAX_DOBJS 256
#define HSD_MAX_JOINTS 512
#define HSD_MAX_ENV_GROUPS 10
#define HSD_MAX_ENV_INFLUENCES 8
#define HSD_MAX_TOBJS 2

/* HSD_Joint flags (jobj.h). */
#define HSD_JOBJ_SKELETON 0x1u
#define HSD_JOBJ_SKELETON_ROOT 0x2u
#define HSD_JOBJ_CLASSICAL_SCALE 0x8u
#define HSD_JOBJ_HIDDEN 0x10u
#define HSD_JOBJ_INSTANCE 0x1000u

typedef struct HsdVertex {
    float position[3];
    float normal[3];
    float uv[2];  /* GX_VA_TEX0 */
    float uv2[2]; /* GX_VA_TEX1, used by two-texture TEV stages */
    uint8_t color[4];
    int16_t texture; /* index into HsdModel::textures, -1 when untextured */
} HsdVertex;

typedef struct HsdTexture {
    uint8_t *rgba;
    uint16_t width;
    uint16_t height;
    size_t source_offset;  /* data-relative offset of the GX image */
    size_t palette_offset; /* data-relative offset of the TLUT, 0 when none */
    uint32_t format;
    uint8_t mipmap; /* HSD_ImageDesc.mipmap */
} HsdTexture;

/* One TObjDesc from the material's texture chain (mobj->texdesc->next...).
 * `flags` is HSD_TObjDesc.blend_flags: colormap/alphamap/lightmap bits. */
typedef struct HsdTobj {
    int16_t texture;     /* index into HsdModel::textures, -1 when untagged */
    uint8_t id;          /* GXTexMapID */
    uint8_t src;         /* GXTexGenSrc */
    uint8_t wrap_s, wrap_t;
    uint8_t magfilt;     /* GXTexFilter */
    uint8_t minfilt;     /* GXTexFilter; default GX_LIN_MIP_LIN */
    uint8_t anisotropy;  /* GXAnisotropy (0 = 1x) */
    uint8_t bias_clamp, edge_lod;
    float lod_bias;
    uint32_t flags;
    float blending;
    uint8_t has_tev;
    /* HSD_TObjTevDesc: explicit per-texture TEV override. */
    uint8_t tev_color_op, tev_alpha_op;
    uint8_t tev_color_bias, tev_alpha_bias;
    uint8_t tev_color_scale, tev_alpha_scale;
    uint8_t tev_color_clamp, tev_alpha_clamp;
    uint8_t tev_color_a, tev_color_b, tev_color_c, tev_color_d;
    uint8_t tev_alpha_a, tev_alpha_b, tev_alpha_c, tev_alpha_d;
    uint8_t tev_konst[4], tev_tev0[4], tev_tev1[4];
    uint32_t tev_active;
} HsdTobj;

/* Everything HSD_MObjSetup feeds to the GX state for one material. */
typedef struct HsdMaterial {
    uint32_t rendermode; /* HSD RENDER_* bits, including the forced RENDER_TOON */
    uint8_t ambient[4];
    uint8_t diffuse[4];
    uint8_t specular[4];
    float alpha;
    float shininess;
    uint8_t pe_present;
    uint8_t pe_flags, pe_ref0, pe_ref1, pe_dst_alpha, pe_type;
    uint8_t pe_src_factor, pe_dst_factor, pe_logic_op, pe_z_comp;
    uint8_t pe_alpha_comp0, pe_alpha_op, pe_alpha_comp1;
    uint8_t tobj_count;
    HsdTobj tobjs[HSD_MAX_TOBJS];
    /*
     * Derived GX state, all from the decomp:
     *   channel_lit   HSD_SetupChannelMode(rendermode & 7) == 4 -> lit channel
     *   initial_ras   MObjMakeTExp uses RAS as the initial TEV input when
     *                 RENDER_VERTEX is set, else the material constant
     *   diffuse_mul   RENDER_DIFFUSE adds a final TEV stage multiplying by RAS
     *   specular      RENDER_SPECULAR adds the specular TEV path
     *   alpha_test/z/blend come from HSD_SetupPEMode (or HSD_PEDesc when set)
     */
    uint8_t channel_lit;
    uint8_t initial_ras;
    uint8_t diffuse_mul;
    uint8_t specular_tev;
    uint8_t z_enable;
    uint8_t z_func;  /* GXCompare */
    uint8_t z_update;
    uint8_t alpha_test;
    uint8_t alpha_comp0, alpha_ref0, alpha_op, alpha_comp1, alpha_ref1;
    uint8_t blend;   /* GXBlendMode */
    uint8_t blend_src, blend_dst; /* GXBlendFactor */
    uint8_t blend_op; /* GXBlendOp for GX_BM_SUBTRACT (rare) */
} HsdMaterial;

/* One drawable piece, usually a single PObj display list.  The viewer can
 * isolate these to inspect individual body/face parts. */
typedef struct HsdBatch {
    size_t first_vertex;
    size_t vertex_count;
    size_t object_index;
    size_t dobj_index;
    int16_t texture;
    uint8_t cull_mode;   /* 0 none, 1 front, 2 back, 3 both (not drawn) */
    uint32_t rendermode; /* HSD RENDER_* bits (z-mode, xlu, ...) */
    float texmtx[16];    /* HSD MakeTextureMtx result, column-major for GL */
    float texmtx2[16];   /* second TObj's matrix, for TEX1 stages */
    uint8_t wrap_s;      /* GX wrap: 0 clamp, 1 repeat, 2 mirror */
    uint8_t wrap_t;
    uint8_t translucent;
    HsdMaterial material;
} HsdBatch;

/*
 * One envelope group from HSD_PObjDesc: a list of {joint, weight} influences.
 * `rigid` (first weight >= 1) means only the first influence is used and the
 * group matrix is that joint's world matrix, exactly like SetupEnvelopeModelMtx.
 */
typedef struct HsdEnvGroup {
    uint8_t rigid;
    uint8_t count;
    int16_t joints[HSD_MAX_ENV_INFLUENCES];
    float weights[HSD_MAX_ENV_INFLUENCES];
} HsdEnvGroup;

/* Per-PObj runtime matrix inputs, kept so animation can re-skin every frame. */
typedef struct HsdBatchSkin {
    uint8_t pobj_type; /* 0 skin, 1 shapeanim, 2 envelope */
    int16_t current_joint;
    int16_t shared_joint; /* POBJ_SKIN second matrix slot, -1 when unused */
    uint8_t group_count;
    HsdEnvGroup groups[HSD_MAX_ENV_GROUPS];
} HsdBatchSkin;

typedef struct HsdJoint {
    size_t offset; /* host offset of the HSD_Joint in the archive */
    int parent;    /* index into HsdModel::joints, -1 for the root */
    uint32_t flags;
    float rotation[3]; /* animated local SRT (Euler radians) */
    float scale[3];
    float position[3];
    float rotation_bind[3]; /* archive values, used to reset a pose */
    float scale_bind[3];
    float position_bind[3];
    float inv_bind[3][4];  /* joint->mtx: inverse bind world matrix */
    int has_inv_bind;
    float scale_world[3];  /* accumulated scale (animated) */
    float world_bind[3][4]; /* bind world matrix (matrix at rest) */
    float world[3][4];     /* current world matrix (animated) */
    uint8_t hidden_dyn;    /* animation NODE/BRANCH hidden state */
} HsdJoint;

typedef struct HsdModel {
    HsdVertex *vertices; /* three entries per triangle */
    size_t vertex_count;
    size_t vertex_capacity;
    float bounds_min[3];
    float bounds_max[3];
    size_t object_count;
    size_t triangle_count;
    size_t skipped_primitives;
    size_t texture_count;
    HsdTexture textures[HSD_MAX_TEXTURES];
    size_t batch_count;
    HsdBatch batches[HSD_MAX_BATCHES];
    HsdBatchSkin batch_skin[HSD_MAX_BATCHES];
    size_t dobj_count; /* DObjs in HSD traversal order (parts visibility) */
    uint8_t dobj_hidden[HSD_MAX_DOBJS];
    size_t pobj_type_count[3]; /* skin, shapeanim, envelope */
    HsdJoint joints[HSD_MAX_JOINTS];
    size_t joint_count;
    size_t instance_count;
    /* Fighter_UpdateModelScale: the game sets the root joint scale to
     * x34_scale.y * co_attrs.model_scaling.  parts_apply fills this from
     * ftData<Char>'s attribute table; 1.0 when unavailable.  model_scale_x is
     * the X override (x34_scale.z, nonzero only for Mr. Game & Watch). */
    float model_scale;
    float model_scale_x;
    /* Runtime material overrides (ftMaterial_800BFB4C): Mr. Game & Watch's
     * costume colour replaces every MObj diffuse. */
    uint8_t override_diffuse[4];
    uint8_t has_override_diffuse;
    /* Raw per-vertex skinning inputs: 6 floats (position, normal in the
     * group's stored space) and one selector byte per vertex. */
    float *raw;
    uint8_t *skin;
} HsdModel;

/* Forward declaration keeps the native build independent of the HSD headers. */
struct HSD_JObj;

void hsd_model_init(HsdModel *model, HsdVertex *storage,
                     size_t storage_vertices);

/* Parses a raw HSD archive. root_offset is a data-relative offset; pass 0 to
 * select the first model joint from the archive's public symbol table. */
int hsd_model_load(HsdModel *model, const uint8_t *data, size_t size,
                    size_t root_offset, char *error, size_t error_size);

/* Releases decoded texture data. Vertex storage is owned by the caller. */
void hsd_model_free(HsdModel *model);

/* 1 when the batch's drawable object is hidden by the model's parts table. */
int hsd_model_batch_visible(const HsdModel *model, size_t batch_index);

/* Static parts visibility plus the animation-driven joint hidden state. */
int hsd_model_batch_pose_visible(const HsdModel *model, size_t batch_index);

/*
 * Pose evaluation.  hsd_model_pose_reset() restores every joint's local SRT
 * to the archive bind values, hsd_model_pose_channel() applies one HSD_A_J_*
 * channel (JObjUpdateFunc semantics), and hsd_model_pose_apply() recomputes
 * all world matrices and re-skins the vertex buffer.
 */
void hsd_model_pose_reset(HsdModel *model);
void hsd_model_pose_channel(HsdModel *model, size_t joint, int channel,
                             float value);
void hsd_model_pose_apply(HsdModel *model);

#endif
