#include "hsd/anim.h"

#include "platform/disc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HSD_DATA_BASE 0x20u
#define HSDA_MAX_PUBLIC 4096

static uint32_t rb16(const uint8_t *d, size_t n, size_t o)
{
    if (o > n - 2) {
        return 0;
    }
    return ((uint32_t) d[o] << 8) | d[o + 1];
}

static uint32_t rb32(const uint8_t *d, size_t n, size_t o)
{
    if (o > n - 4) {
        return 0;
    }
    return ((uint32_t) d[o] << 24) | ((uint32_t) d[o + 1] << 16) |
           ((uint32_t) d[o + 2] << 8) | d[o + 3];
}

static float rf32(const uint8_t *d, size_t n, size_t o)
{
    uint32_t bits = rb32(d, n, o);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int range_ok(size_t o, size_t bytes, size_t n)
{
    return o <= n && bytes <= n - o;
}

static void seterr(char *dst, size_t n, const char *msg)
{
    if (dst != NULL && n != 0) {
        size_t length = strlen(msg);
        if (length >= n) {
            length = n - 1;
        }
        memcpy(dst, msg, length);
        dst[length] = 0;
    }
}

/* File offset of a public symbol's data, or SIZE_MAX. */
static size_t find_public_symbol(const uint8_t *d, size_t n, size_t base,
                                 const char *name)
{
    uint32_t data_size = rb32(d, n, base + 4);
    uint32_t nb_reloc = rb32(d, n, base + 8);
    uint32_t nb_public = rb32(d, n, base + 0xc);
    uint32_t nb_extern = rb32(d, n, base + 0x10);
    size_t public_table;
    size_t strings;
    uint32_t i;
    if (nb_public == 0 || nb_public > HSDA_MAX_PUBLIC) {
        return SIZE_MAX;
    }
    public_table = base + HSD_DATA_BASE + data_size + (size_t) nb_reloc * 4;
    if (!range_ok(public_table, (size_t) nb_public * 8, n)) {
        return SIZE_MAX;
    }
    strings = public_table + (size_t) nb_public * 8 + (size_t) nb_extern * 8;
    if (strings >= n) {
        return SIZE_MAX;
    }
    for (i = 0; i < nb_public; ++i) {
        uint32_t data_offset = rb32(d, n, public_table + i * 8);
        uint32_t symbol_offset = rb32(d, n, public_table + i * 8 + 4);
        size_t at = strings + symbol_offset;
        size_t max;
        size_t length;
        if (at >= n) {
            continue;
        }
        max = n - at;
        length = strnlen((const char *) (d + at), max);
        if (length < max && length == strlen(name) &&
            memcmp(d + at, name, length) == 0) {
            return base + HSD_DATA_BASE + data_offset;
        }
    }
    return SIZE_MAX;
}

/* `PlyMario5K_Share_ACTION_Wait1_figatree` -> `Wait1`. */
static void clip_display_name(const char *symbol, char *out, size_t outn)
{
    const char *start = strstr(symbol, "_ACTION_");
    const char *end = strstr(symbol, "_figatree");
    size_t length;
    if (start != NULL) {
        start += 8;
    } else {
        start = symbol;
    }
    length = (end != NULL && end > start) ? (size_t) (end - start)
                                          : strlen(start);
    if (length >= outn) {
        length = outn - 1;
    }
    memcpy(out, start, length);
    out[length] = 0;
}

/*
 * Pl<Char>AJ.dat is a sequence of 32-byte aligned HSD sub-archives, one per
 * clip, each exposing a `<name>_figatree` public symbol.
 */
static int walk_clips(Anim *anim, char *err, size_t errn)
{
    size_t off = 0;
    while (off + HSD_DATA_BASE <= anim->size) {
        uint32_t file_size = rb32(anim->data, anim->size, off);
        uint32_t data_size;
        uint32_t nb_reloc;
        uint32_t nb_public;
        uint32_t nb_extern;
        size_t public_table;
        size_t strings;
        uint32_t i;
        if (file_size < HSD_DATA_BASE || off + file_size > anim->size) {
            break;
        }
        data_size = rb32(anim->data, anim->size, off + 4);
        nb_reloc = rb32(anim->data, anim->size, off + 8);
        nb_public = rb32(anim->data, anim->size, off + 0xc);
        nb_extern = rb32(anim->data, anim->size, off + 0x10);
        public_table =
            off + HSD_DATA_BASE + data_size + (size_t) nb_reloc * 4;
        if (nb_public <= HSDA_MAX_PUBLIC &&
            range_ok(public_table, (size_t) nb_public * 8, anim->size)) {
            strings =
                public_table + (size_t) nb_public * 8 + (size_t) nb_extern * 8;
            for (i = 0; i < nb_public && strings < anim->size; ++i) {
                uint32_t data_offset = rb32(anim->data, anim->size,
                                            public_table + i * 8);
                uint32_t symbol_offset =
                    rb32(anim->data, anim->size, public_table + i * 8 + 4);
                size_t at = strings + symbol_offset;
                const char *symbol;
                ClipInfo *clip;
                if (at >= anim->size ||
                    memchr(anim->data + at, 0, anim->size - at) == NULL) {
                    continue;
                }
                symbol = (const char *) (anim->data + at);
                if (strstr(symbol, "_figatree") == NULL) {
                    continue;
                }
                if (anim->clip_count >= HSD_MAX_CLIPS) {
                    break;
                }
                clip = &anim->clips[anim->clip_count++];
                memset(clip, 0, sizeof(*clip));
                snprintf(clip->symbol, sizeof(clip->symbol), "%s", symbol);
                clip_display_name(symbol, clip->name, sizeof(clip->name));
                clip->container_offset = off;
                clip->figa_offset = off + HSD_DATA_BASE + data_offset;
            }
        }
        off = (off + file_size + 0x1f) & ~(size_t) 0x1f;
    }
    if (anim->clip_count == 0) {
        seterr(err, errn, "no figatree clips found");
        return -1;
    }
    return 0;
}

/*
 * PlCo.dat ftLoadCommonData -> pData[4] (ftPartsTable) and pData[5]
 * (Fighter_804D6540 skip lists), copied so the asset can be freed.
 */
static int load_common_tables(Anim *anim, const char *disc, char *err,
                              size_t errn)
{
    DiscFile asset = { 0 };
    const uint8_t *d;
    size_t n;
    size_t symbol;
    size_t pdata;
    uint32_t parts_value;
    uint32_t skip_value;
    int kind;
    if (disc_load(disc, "PlCo.dat", &asset, err, errn) !=
        DISC_OK) {
        return -1;
    }
    d = asset.data;
    n = asset.size;
    symbol = find_public_symbol(d, n, 0, "ftLoadCommonData");
    if (symbol == SIZE_MAX || !range_ok(symbol, 23 * 4, n)) {
        seterr(err, errn, "PlCo.dat has no ftLoadCommonData");
        disc_free(&asset);
        return -1;
    }
    pdata = symbol;
    parts_value = rb32(d, n, pdata + 4 * 4);
    skip_value = rb32(d, n, pdata + 5 * 4);
    if (parts_value != 0 && parts_value + HSD_DATA_BASE < n) {
        size_t table = (size_t) parts_value + HSD_DATA_BASE;
        for (kind = 0; kind < 64; ++kind) {
            uint32_t entry_value = rb32(d, n, table + (size_t) kind * 4);
            size_t entry;
            if (entry_value == 0) {
                continue;
            }
            entry = (size_t) entry_value + HSD_DATA_BASE;
            if (!range_ok(entry, 12, n)) {
                continue;
            }
            anim->parts_num[kind] = (int) rb32(d, n, entry + 8);
        }
    }
    if (skip_value != 0 && skip_value + HSD_DATA_BASE < n) {
        size_t table = (size_t) skip_value + HSD_DATA_BASE;
        for (kind = 0; kind < 64; ++kind) {
            uint32_t entry_value = rb32(d, n, table + (size_t) kind * 4);
            size_t entry;
            uint32_t count;
            uint32_t list_value;
            size_t list;
            uint32_t i;
            if (entry_value == 0) {
                continue;
            }
            entry = (size_t) entry_value + HSD_DATA_BASE;
            if (!range_ok(entry, 8, n)) {
                continue;
            }
            count = rb32(d, n, entry + 4);
            list_value = rb32(d, n, entry + 0);
            if (list_value == 0 || count == 0) {
                continue;
            }
            list = (size_t) list_value + HSD_DATA_BASE;
            if (count > 64) {
                count = 64;
            }
            for (i = 0; i < count; ++i) {
                if (!range_ok(list + (size_t) i * 4, 1, n)) {
                    count = i;
                    break;
                }
                anim->skip_ids[kind][i] = d[list + (size_t) i * 4];
            }
            anim->skip_count[kind] = (uint8_t) count;
        }
    }
    disc_free(&asset);
    return 0;
}

static int kind_for_model(const char *model_file)
{
    static const struct {
        const char *prefix;
        int kind;
    } kinds[] = {
        { "Mr", 0 },  { "Fx", 1 },  { "Ca", 2 },  { "Dk", 3 },  { "Kb", 4 },
        { "Kp", 5 },  { "Lk", 6 },  { "Sk", 7 },  { "Ns", 8 },  { "Pe", 9 },
        { "Pp", 10 }, { "Nn", 11 }, { "Pk", 12 }, { "Ss", 13 }, { "Ys", 14 },
        { "Pr", 15 }, { "Mt", 16 }, { "Lg", 17 }, { "Ms", 18 }, { "Zd", 19 },
        { "Cl", 20 }, { "Dr", 21 }, { "Fc", 22 }, { "Pc", 23 }, { "Gw", 24 },
        { "Gn", 25 }, { "Fe", 26 }, { "Mh", 27 }, { "Ch", 28 }, { "Bo", 29 },
        { "Gl", 30 }, { "Gk", 31 }, { "Sb", 32 },
    };
    char prefix[3];
    size_t i;
    if (model_file == NULL || model_file[0] != 'P' || model_file[1] != 'l' ||
        model_file[2] == 0 || model_file[3] == 0) {
        return 0;
    }
    prefix[0] = model_file[2];
    prefix[1] = model_file[3];
    prefix[2] = 0;
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        if (strcmp(prefix, kinds[i].prefix) == 0) {
            return kinds[i].kind;
        }
    }
    return 0;
}

static void derive_anim_name(const char *model_file, char *out, size_t outn)
{
    snprintf(out, outn, "%.4sAJ.dat", model_file != NULL ? model_file : "PlMr");
}

int anim_load(Anim *anim, const char *disc, const char *model_file,
                   const char *anim_file, char *err, size_t errn)
{
    char name[32];
    DiscFile asset = { 0 };
    if (anim == NULL) {
        return -1;
    }
    memset(anim, 0, sizeof(*anim));
    if (err != NULL && errn != 0) {
        err[0] = 0;
    }
    if (anim_file != NULL) {
        snprintf(name, sizeof(name), "%s", anim_file);
    } else {
        derive_anim_name(model_file, name, sizeof(name));
    }
    if (disc_load(disc, name, &asset, err, errn) != DISC_OK) {
        return -1;
    }
    anim->data = asset.data;
    anim->size = asset.size;
    anim->kind = kind_for_model(model_file);
    if (walk_clips(anim, err, errn) != 0) {
        anim_free(anim);
        return -1;
    }
    if (load_common_tables(anim, disc, err, errn) != 0) {
        anim_free(anim);
        return -1;
    }
    {
        int kind = anim->kind;
        fprintf(stderr, "Animation: %s, %zu clips, kind %d (%d parts)\n",
                name, anim->clip_count, kind, anim->parts_num[kind]);
    }
    return 0;
}

void anim_free(Anim *anim)
{
    if (anim == NULL) {
        return;
    }
    free(anim->fobjs);
    anim->fobjs = NULL;
    anim->fobj_count = 0;
    free(anim->data);
    anim->data = NULL;
    anim->size = 0;
}

size_t anim_clip_count(const Anim *anim)
{
    return anim->clip_count;
}

const char *anim_clip_name(const Anim *anim, size_t index)
{
    if (index >= anim->clip_count) {
        return "";
    }
    return anim->clips[index].name;
}

float anim_clip_frames(const Anim *anim, size_t index)
{
    size_t at;
    if (index >= anim->clip_count) {
        return 0.0f;
    }
    at = anim->clips[index].figa_offset + 8;
    if (!range_ok(at, 4, anim->size)) {
        return 0.0f;
    }
    return rf32(anim->data, anim->size, at);
}

int anim_clip_find(const Anim *anim, const char *name)
{
    size_t i;
    char *end;
    long wanted;
    if (name == NULL) {
        return -1;
    }
    wanted = strtol(name, &end, 10);
    if (end != name && *end == 0) {
        return (wanted >= 0 && (size_t) wanted < anim->clip_count)
                   ? (int) wanted
                   : -1;
    }
    for (i = 0; i < anim->clip_count; ++i) {
        const char *a = anim->clips[i].name;
        const char *b = name;
        int equal = 1;
        while (*a != 0 && *b != 0) {
            if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
                equal = 0;
                break;
            }
            a++;
            b++;
        }
        if (equal && *a == 0 && *b == 0) {
            return (int) i;
        }
    }
    return -1;
}

int anim_set_clip(Anim *anim, size_t index, HsdModel *model,
                       char *err, size_t errn)
{
    const ClipInfo *clip;
    size_t base;
    size_t nodes;
    size_t tracks;
    size_t node_i = 0;
    size_t track_i = 0;
    size_t joint = 0;
    size_t bound = 0;
    Fobj *fobjs = NULL;
    size_t fobj_count = 0;
    if (anim == NULL || model == NULL || index >= anim->clip_count) {
        seterr(err, errn, "invalid clip");
        return -1;
    }
    clip = &anim->clips[index];
    base = clip->container_offset;
    if (!range_ok(clip->figa_offset, 0x14, anim->size)) {
        seterr(err, errn, "figatree out of range");
        return -1;
    }
    anim->frames = rf32(anim->data, anim->size, clip->figa_offset + 8);
    anim->flags = rb32(anim->data, anim->size, clip->figa_offset + 4);
    nodes = base + HSD_DATA_BASE +
            rb32(anim->data, anim->size, clip->figa_offset + 0xc);
    tracks = base + HSD_DATA_BASE +
             rb32(anim->data, anim->size, clip->figa_offset + 0x10);
    if (!range_ok(nodes, 1, anim->size) ||
        !range_ok(tracks, 12, anim->size)) {
        seterr(err, errn, "figatree tables out of range");
        return -1;
    }
    memset(anim->joint_first, 0, sizeof(anim->joint_first));
    memset(anim->joint_tracks, 0, sizeof(anim->joint_tracks));
    /*
     * ftAnim_8006F4C8 walks the figatree node array and fp->parts[] in step:
     * every node lands on the next part whose joint is non-null.  The
     * ftParts skip list only inserts phantom part slots (for alternate
     * models), so the i-th node always drives the i-th joint of the HSD
     * joint tree.
     */
    while (joint < model->joint_count && range_ok(nodes + node_i, 1, anim->size) &&
           anim->data[nodes + node_i] != 0xff) {
        uint8_t count = anim->data[nodes + node_i];
        anim->joint_first[joint] = (uint16_t) fobj_count;
        anim->joint_tracks[joint] = count;
        {
            uint8_t k;
            for (k = 0; k < count; ++k) {
                size_t t = tracks + (track_i + k) * 12;
                uint16_t length;
                uint16_t startframe;
                uint8_t obj_type;
                uint8_t frac_value;
                uint8_t frac_slope;
                uint32_t ad_value;
                size_t ad;
                Fobj *grown;
                if (!range_ok(t, 12, anim->size)) {
                    seterr(err, errn, "figatrack out of range");
                    goto fail;
                }
                length = (uint16_t) rb16(anim->data, anim->size, t);
                startframe = (uint16_t) rb16(anim->data, anim->size, t + 2);
                obj_type = anim->data[t + 4];
                frac_value = anim->data[t + 5];
                frac_slope = anim->data[t + 6];
                ad_value = rb32(anim->data, anim->size, t + 8);
                ad = base + HSD_DATA_BASE + ad_value;
                if (!range_ok(ad, length, anim->size)) {
                    seterr(err, errn, "figatrack data out of range");
                    goto fail;
                }
                if (fobj_count >= 0xffff) {
                    goto fail;
                }
                grown = realloc(fobjs, (fobj_count + 1) * sizeof(*grown));
                if (grown == NULL) {
                    seterr(err, errn, "out of memory for animation tracks");
                    goto fail;
                }
                fobjs = grown;
                fobj_init(&fobjs[fobj_count], anim->data + ad, length,
                               obj_type, (int16_t) startframe, frac_value,
                               frac_slope);
                fobj_count++;
            }
        }
        track_i += count;
        node_i++;
        joint++;
        bound++;
    }
    if (bound != model->joint_count) {
        fprintf(stderr,
                "Animation: clip %s bound %zu joints, model has %zu\n",
                clip->name, bound, model->joint_count);
    }
    free(anim->fobjs);
    anim->fobjs = fobjs;
    anim->fobj_count = fobj_count;
    anim->active = 1;
    return 0;

fail:
    free(fobjs);
    return -1;
}

float anim_end_frame(const Anim *anim)
{
    return anim != NULL ? anim->frames : 0.0f;
}

void anim_apply(Anim *anim, HsdModel *model, float frame)
{
    size_t j;
    size_t k;
    hsd_model_pose_reset(model);
    if (anim != NULL && anim->active) {
        for (j = 0; j < model->joint_count; ++j) {
            size_t first = anim->joint_first[j];
            size_t count = anim->joint_tracks[j];
            if (first + count > anim->fobj_count) {
                continue;
            }
            for (k = 0; k < count; ++k) {
                Fobj *f = &anim->fobjs[first + k];
                float value;
                /* HSD_JObjReqAnimAll(jobj, frame) + HSD_JObjAnimAll. */
                fobj_req_anim(f, frame);
                if (fobj_interpret(f, 0.0f, &value)) {
                    hsd_model_pose_channel(model, j, f->obj_type, value);
                }
            }
        }
    }
    hsd_model_pose_apply(model);
}
