#ifndef MELEE_NATIVE_DEMO_MODEL_H
#define MELEE_NATIVE_DEMO_MODEL_H

/*
 * Raw HSD model reader for the native demo.
 *
 * Melee character archives store GameCube GPU data.  This module expands the
 * bind-pose geometry, texture coordinates, material colors and the embedded
 * textures into a renderer-independent representation.  It does not run the
 * HSD engine and does not evaluate skinning or animation.
 */
#include <stddef.h>
#include <stdint.h>

#define DEMO_MAX_TEXTURES 128
#define DEMO_MAX_BATCHES 512
#define DEMO_MAX_DOBJS 256
#define DEMO_MAX_JOINTS 512
#define DEMO_MAX_ENV_GROUPS 10
#define DEMO_MAX_ENV_INFLUENCES 8

/* HSD_Joint flags (jobj.h). */
#define DEMO_JOBJ_SKELETON 0x1u
#define DEMO_JOBJ_SKELETON_ROOT 0x2u
#define DEMO_JOBJ_CLASSICAL_SCALE 0x8u
#define DEMO_JOBJ_HIDDEN 0x10u
#define DEMO_JOBJ_INSTANCE 0x1000u

typedef struct DemoModelVertex {
    float position[3];
    float normal[3];
    float uv[2];
    uint8_t color[4];
    int16_t texture; /* index into DemoModel::textures, -1 when untextured */
} DemoModelVertex;

typedef struct DemoModelTexture {
    uint8_t *rgba;
    uint16_t width;
    uint16_t height;
    size_t source_offset;  /* data-relative offset of the GX image */
    size_t palette_offset; /* data-relative offset of the TLUT, 0 when none */
    uint32_t format;
} DemoModelTexture;

/* One drawable piece, usually a single PObj display list.  The viewer can
 * isolate these to inspect individual body/face parts. */
typedef struct DemoModelBatch {
    size_t first_vertex;
    size_t vertex_count;
    size_t object_index;
    size_t dobj_index;
    int16_t texture;
    uint8_t cull_mode;   /* 0 none, 1 front, 2 back, 3 both (not drawn) */
    uint32_t rendermode; /* HSD RENDER_* bits (z-mode, xlu, ...) */
    float texmtx[16];    /* HSD MakeTextureMtx result, column-major for GL */
    uint8_t wrap_s;      /* GX wrap: 0 clamp, 1 repeat, 2 mirror */
    uint8_t wrap_t;
    uint8_t translucent;
} DemoModelBatch;

/*
 * One envelope group from HSD_PObjDesc: a list of {joint, weight} influences.
 * `rigid` (first weight >= 1) means only the first influence is used and the
 * group matrix is that joint's world matrix, exactly like SetupEnvelopeModelMtx.
 */
typedef struct DemoEnvGroup {
    uint8_t rigid;
    uint8_t count;
    int16_t joints[DEMO_MAX_ENV_INFLUENCES];
    float weights[DEMO_MAX_ENV_INFLUENCES];
} DemoEnvGroup;

/* Per-PObj runtime matrix inputs, kept so animation can re-skin every frame. */
typedef struct DemoBatchSkin {
    uint8_t pobj_type; /* 0 skin, 1 shapeanim, 2 envelope */
    int16_t current_joint;
    int16_t shared_joint; /* POBJ_SKIN second matrix slot, -1 when unused */
    uint8_t group_count;
    DemoEnvGroup groups[DEMO_MAX_ENV_GROUPS];
} DemoBatchSkin;

typedef struct DemoJoint {
    size_t offset; /* host offset of the HSD_Joint in the archive */
    int parent;    /* index into DemoModel::joints, -1 for the root */
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
} DemoJoint;

typedef struct DemoModel {
    DemoModelVertex *vertices; /* three entries per triangle */
    size_t vertex_count;
    size_t vertex_capacity;
    float bounds_min[3];
    float bounds_max[3];
    size_t object_count;
    size_t triangle_count;
    size_t skipped_primitives;
    size_t texture_count;
    DemoModelTexture textures[DEMO_MAX_TEXTURES];
    size_t batch_count;
    DemoModelBatch batches[DEMO_MAX_BATCHES];
    DemoBatchSkin batch_skin[DEMO_MAX_BATCHES];
    size_t dobj_count; /* DObjs in HSD traversal order (parts visibility) */
    uint8_t dobj_hidden[DEMO_MAX_DOBJS];
    size_t pobj_type_count[3]; /* skin, shapeanim, envelope */
    DemoJoint joints[DEMO_MAX_JOINTS];
    size_t joint_count;
    size_t instance_count;
    /* Raw per-vertex skinning inputs: 6 floats (position, normal in the
     * group's stored space) and one selector byte per vertex. */
    float *raw;
    uint8_t *skin;
} DemoModel;

/* Forward declaration keeps the native build independent of the HSD headers. */
struct HSD_JObj;

void demo_model_init(DemoModel *model, DemoModelVertex *storage,
                     size_t storage_vertices);

/* Parses a raw HSD archive. root_offset is a data-relative offset; pass 0 to
 * select the first model joint from the archive's public symbol table. */
int demo_model_load(DemoModel *model, const uint8_t *data, size_t size,
                    size_t root_offset, char *error, size_t error_size);

/* Releases decoded texture data. Vertex storage is owned by the caller. */
void demo_model_free(DemoModel *model);

/* 1 when the batch's drawable object is hidden by the model's parts table. */
int demo_model_batch_visible(const DemoModel *model, size_t batch_index);

/* Static parts visibility plus the animation-driven joint hidden state. */
int demo_model_batch_pose_visible(const DemoModel *model, size_t batch_index);

/*
 * Pose evaluation.  demo_model_pose_reset() restores every joint's local SRT
 * to the archive bind values, demo_model_pose_channel() applies one HSD_A_J_*
 * channel (JObjUpdateFunc semantics), and demo_model_pose_apply() recomputes
 * all world matrices and re-skins the vertex buffer.
 */
void demo_model_pose_reset(DemoModel *model);
void demo_model_pose_channel(DemoModel *model, size_t joint, int channel,
                             float value);
void demo_model_pose_apply(DemoModel *model);

#endif
