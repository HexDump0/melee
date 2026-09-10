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
} DemoModelBatch;

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
    size_t dobj_count; /* DObjs in HSD traversal order (parts visibility) */
    uint8_t dobj_hidden[DEMO_MAX_DOBJS];
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

#endif
