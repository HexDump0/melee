/*
 * Shared plumbing for the asset checks (see test_decomp_assets.c).
 *
 * The checks were one 3,465-line file; they are 30 independent probes with no
 * coupling beyond these helpers, so they now live one domain per translation
 * unit -- fighters, items, stages, scenes -- with the driver in
 * test_decomp_assets.c.  Nothing here changed behaviour; the split was
 * verified by reassembling the chunks byte-for-byte before moving them.
 */
#ifndef MELEE_TESTS_ASSET_COMMON_H
#define MELEE_TESTS_ASSET_COMMON_H

#include "decomp/assets/hsd_convert.h"
#include "platform/disc.h"

#include <dolphin/os.h>
#include <math.h>
#include <melee/gr/types.h>
#include <melee/it/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/robj.h>

void JObjInfoInit(void);

typedef struct {
    char name[64];
    char root[128];
    unsigned joints;
    unsigned hidden;
    HsdConvertStats stats;
    int ok;
} ModelResult;

unsigned char* load_archive(const char* image, const char* path, const char* root_hint, size_t* size, char* error, size_t error_size);
void* read_host_ptr(const unsigned char* p);
uint32_t read_host_u32(const unsigned char* p);
uint16_t read_host_u16(const unsigned char* p);
uint16_t read_be_u16(const unsigned char* p);
uint32_t read_be_u32(const unsigned char* p);
float read_host_f32(const unsigned char* p);
int archive_has_reloc(const HSD_Archive* archive, const unsigned char* field);
int check_reloc_integrity(const char* path, const unsigned char* raw, const unsigned char* buffer, const HSD_Archive* archive);
int check_link_dynamics(const char* image);
int check_ft_part_anims(const char* image, const char* path, const char* symbol);
int ptr_in_buffer(const void* p, const unsigned char* base, size_t size);
float read_be_f32(const unsigned char* p);
int check_ft_data_tables(const char* image, const char* path, const char* symbol);
int check_hidden_parts_none_slot(const char* image);
int check_orphan_matanims(const char* image);
int check_kraid_param(const char* image);
int check_unk_flag_bit_order(void);
int check_item_attr_bits(unsigned char** articles, unsigned count);
int check_ifall_hud_modelsets(const char* image);
int check_staffroll_modelset(const char* image);
int check_kumite_tables(const char* image);
int check_converter_sweep(const char* image);
int check_yorster_param(const char* image);
int check_stage_params(const char* image);
int check_stage_display_lists(const char* image);
int check_castle_dynamics(const char* image);
int check_castle_param(const char* image);
int check_pstadium_param(const char* image, const char* file);
int check_scene_root(const char* image, const char* path, const char* symbol);
int check_intro_easy(const char* image);
int check_event_levels(const char* image);
int check_ty_sobj_backgrounds(const char* image);
int check_ty_datai_tables(const char* image);
int check_ty_data_tables(const char* image);
int check_item_models(const char* image);
int check_stage_matanims(const char* image, const char* name);
int check_stage_item_articles(const char* image, const char* path, unsigned min_articles);
unsigned pose_tree(HSD_JObj* root, float* out_min, float* out_max);
int check_converted_joint(const char* tag, const unsigned char* rdata, const unsigned char* cdata, uint32_t host_base, uint32_t field_off, uint32_t raw_joint);
int check_respawn_platform(const char* image);
int check_cpu_attack_tables(const char* image);
int check_kirby_hats(const char* image);
int check_archive(const char* image, const char* path, ModelResult* result, int require_public);
int cache_check(const char* image, const char* path);

#endif
