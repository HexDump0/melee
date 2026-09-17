/*
 * Main-menu option labels (P-838).
 *
 * The retail main menu has five options, but the machinery is already written
 * for ten: `mn_803EAE68[] = {4..0xD}` gives ten anchor joints and every loop
 * over options is bounded by `mn_803EB6B0[kind].selection_count`, never by a
 * literal.  `mn_803EB6B0` is a plain writable global, so a sixth option costs
 * an assignment rather than a patch to `decomp/`.
 *
 * What the game does *not* have is a sixth label image.  Each option attaches
 * a `MenMainCursor_Top` instance whose joint 3 is a 176x30 quad, and its
 * texture comes from a 58-entry swap table driven by a TexAnim: option i
 * requests frame `start_frame + 2*i`, and the main menu's frames 0,2,4,6,8
 * land on images 0..4.  Frame 10 -- where a sixth option would land -- still
 * resolves to image 4, because the table is packed and image 5 is already
 * "Regular Match" for the next menu.
 *
 * This file is step one: prove the texture path end to end by replacing an
 * existing label's pixels in the archive before it is parsed.  Same
 * dimensions, same format, same byte count, so nothing downstream has to be
 * told -- which is exactly why it is the cheapest possible proof that
 * generated art reaches the screen.
 *
 * It is deliberately port-side rather than mod-side: there is no ABI for
 * touching an archive yet, and finding out precisely which one a mod would
 * need is the point of doing this first (P-833).
 */
#include "mod/mod.h"

#include <dolphin/pad.h>
#include <melee/gm/gm_1A36.h>
#include <melee/mn/forward.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod/menu_label_data.h"

/*
 * Where the US menu archive keeps its label table.  Offsets are from the
 * probe recorded in learnings/menu_labels.md and are verified below before
 * anything is written: an archive that does not match the expected geometry
 * is left alone rather than corrupted.
 */
#define MNMA_IMAGETBL_US 0x000c0518u /* data-relative, array of ImageDesc* */
#define MNMA_LABEL_W 176u
#define MNMA_LABEL_H 30u
#define MNMA_LABEL_FMT 2u /* GX_TF_IA4 */
#define HSD_DATA_BASE 0x20u

static int patch_enabled = -1;
static int patched;

/*
 * Host order, not big-endian.  The register hook runs *after*
 * hsd_asset_convert and before HSD_ArchiveParse, so the archive's numeric
 * fields and relocation targets have already been swapped -- reading them
 * big-endian here silently finds nothing, which is exactly what it did first.
 */
static unsigned rd32(const unsigned char* p)
{
    unsigned v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static unsigned rd16(const unsigned char* p)
{
    unsigned short v;
    memcpy(&v, p, sizeof(v));
    return v;
}

/*
 * Replace one label image's pixels in place.
 *
 * `slot` is an index into the swap table.  Everything is checked first --
 * table in range, descriptor in range, dimensions and format exactly what a
 * label is, pixel data in range -- because this runs against whatever archive
 * happens to be passing through, and a wrong guess here would scribble on
 * unrelated asset bytes.  Returns non-zero if it wrote.
 */
static int replace_label(unsigned char* data, size_t size, unsigned slot,
                         const unsigned char* pixels, size_t pixels_len)
{
    unsigned desc_off;
    unsigned image_off;
    unsigned w;
    unsigned h;
    unsigned fmt;
    const unsigned char* entry;
    unsigned char* desc;

    if (size < HSD_DATA_BASE + MNMA_IMAGETBL_US + 4u * (slot + 1u)) {
        return 0;
    }
    entry = data + HSD_DATA_BASE + MNMA_IMAGETBL_US + 4u * slot;
    desc_off = rd32(entry);
    if (desc_off == 0 || HSD_DATA_BASE + desc_off + 0x10u > size) {
        return 0;
    }
    desc = data + HSD_DATA_BASE + desc_off;

    image_off = rd32(desc + 0x00);
    w = rd16(desc + 0x04);
    h = rd16(desc + 0x06);
    fmt = rd32(desc + 0x08);
    if (w != MNMA_LABEL_W || h != MNMA_LABEL_H || fmt != MNMA_LABEL_FMT) {
        return 0;
    }
    /* The descriptor's image pointer is a raw file offset at this point: the
     * archive has been byte-swapped but not yet relocated. */
    if (image_off == 0 || image_off + pixels_len > size) {
        return 0;
    }

    memcpy(data + image_off, pixels, pixels_len);
    return 1;
}

/*
 * Called for every archive as it is converted, before HSD_ArchiveParse runs.
 * Identify the menu archive by its own contents rather than by a filename,
 * because the asset path does not carry one here -- and because "the
 * descriptor at this offset is exactly a 176x30 IA4 label" is a much stronger
 * claim than a name match anyway.
 */
/* ------------------------------------------------------------ the entry */

/*
 * Index of the option we add.  The retail main menu uses 0..4; the machinery
 * goes to 9 (`mn_803EAE68` has ten anchor joints and every loop is bounded by
 * `selection_count`), so 5 is simply the next one.
 */
#define UNBOUND_SELECTION 5

/* Frame the label TexAnim is asked for.  Option i requests
 * `start_frame + 2*i`, and the main menu's start_frame is 0, so our option
 * asks for 10 -- a frame no retail option ever requests. */
#define UNBOUND_LABEL_FRAME 10.0f

extern MenuKindData mn_803EB6B0[];
extern MenuFlow mn_804A04F0;

/* Our label, owned here rather than in the archive: the retail swap table is
 * packed (frame 10 still resolves to image 4, "Data") and growing it in place
 * is not possible, so the texture is substituted after the anim instead. */
static HSD_ImageDesc unbound_label_desc;

/* The retail descriptions plus one for us.  `mn_803EB660` has a sixth slot
 * but it holds 0, and index 0 is a real SIS string rather than "nothing", so
 * the array is replaced rather than written through. */
static u16 unbound_descriptions[6];

static void (*real_main_think)(HSD_GObj*);
static HSD_JObj* pending_label;
static int entry_enabled;
static int activated;

static void label_desc_init(void)
{
    unbound_label_desc.image_ptr = (void*) unbound_menu_label;
    unbound_label_desc.width = (u16) MNMA_LABEL_W;
    unbound_label_desc.height = (u16) MNMA_LABEL_H;
    unbound_label_desc.format = (GXTexFmt) MNMA_LABEL_FMT;
    unbound_label_desc.mipmap = 0;
    unbound_label_desc.minLOD = 0.0f;
    unbound_label_desc.maxLOD = 0.0f;
}

/*
 * Swap the label texture on the joint the TexAnim just wrote.
 *
 * Only the 176x30 IA4 texture is touched: the same cursor model also carries
 * the pill, the arrow cap and the ring arcs, and those must keep whatever the
 * animation gave them.
 */
static void override_label(HSD_JObj* jobj)
{
    HSD_DObj* dobj;

    if (jobj == NULL || (jobj->flags & JOBJ_SPLINE) != 0) {
        return;
    }
    for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
        HSD_TObj* tobj;
        if (dobj->mobj == NULL) {
            continue;
        }
        for (tobj = dobj->mobj->tobj; tobj != NULL; tobj = tobj->next) {
            HSD_ImageDesc* id = tobj->imagedesc;
            if (id != NULL && id->width == MNMA_LABEL_W &&
                id->height == MNMA_LABEL_H &&
                (unsigned) id->format == MNMA_LABEL_FMT)
            {
                tobj->imagedesc = &unbound_label_desc;
            }
        }
    }
}

/*
 * The label joint is identified by the frame it is asked for, not by walking
 * the menu's object graph: frame 10 on the main menu can only be our option,
 * because retail stops at 8 and the next menu's labels start at 19.
 */
HSD_JObj* unbound_hsd_label_target(void) { return pending_label; }

void unbound_HSD_JObjReqAnim(HSD_JObj* jobj, f32 frame)
{
    if (entry_enabled && jobj != NULL &&
        mn_804A04F0.cur_menu == MENU_KIND_MAIN && frame == UNBOUND_LABEL_FRAME)
    {
        pending_label = jobj;
    }
    HSD_JObjReqAnim(jobj, frame);
}

void unbound_HSD_JObjAnim(HSD_JObj* jobj)
{
    HSD_JObjAnim(jobj);
    if (pending_label != NULL && jobj == pending_label) {
        /* After the real anim, so the TexAnim's own choice is overwritten
         * rather than racing it. */
        override_label(jobj);
        pending_label = NULL;
    }
}

/*
 * Main-menu think, installed by replacing the function pointer in
 * `mn_803EB6B0` rather than through the shim.
 *
 * The shim cannot help here: `mn_8022DB10` is both defined and
 * address-taken inside mnmain.c, and a compiler resolves an intra-TU
 * reference before any rename or linker wrap can see it.  The table entry is
 * a writable global, so swapping the pointer reaches the same call.
 */
static void unbound_main_think(HSD_GObj* gp)
{
    if (entry_enabled && mn_804A04F0.cur_menu == MENU_KIND_MAIN &&
        mn_804A04F0.hovered_selection == UNBOUND_SELECTION &&
        (gm_GetButtonsTriggered(4) & PAD_CONFIRM) != 0)
    {
        /*
         * Handle our option here and do not let the retail think run: its
         * switch covers selections 0..4 only, and falling off the end of it
         * would dispatch an uninitialised menu kind.
         */
        activated = 1;
        return;
    }
    if (real_main_think != NULL) {
        real_main_think(gp);
    }
}

int mod_menu_take_activation(void)
{
    int a = activated;
    activated = 0;
    return a;
}

void mod_menu_init(void)
{
    unsigned i;
    const u16* src;

    if (getenv("MELEE_NO_MENU_ENTRY") != NULL) {
        return;
    }

    label_desc_init();

    src = mn_803EB6B0[MENU_KIND_MAIN].description_indices;
    for (i = 0; i < 5; ++i) {
        unbound_descriptions[i] = src != NULL ? src[i] : 0;
    }
    /* Reuse the last retail description: adding a SIS string of our own is
     * its own job, and an index the table does not have would render
     * whatever happens to sit at 0. */
    unbound_descriptions[5] = unbound_descriptions[4];

    real_main_think = mn_803EB6B0[MENU_KIND_MAIN].think;
    mn_803EB6B0[MENU_KIND_MAIN].think = unbound_main_think;
    mn_803EB6B0[MENU_KIND_MAIN].description_indices = unbound_descriptions;
    mn_803EB6B0[MENU_KIND_MAIN].selection_count = UNBOUND_SELECTION + 1;
    entry_enabled = 1;

    fprintf(stderr, "[menu] main menu extended to %u options\n",
            (unsigned) mn_803EB6B0[MENU_KIND_MAIN].selection_count);
}

/* --------------------------------------------------------- archive hook */

void mod_menu_on_asset(const void* bytes, size_t size)
{
    unsigned char* data = (unsigned char*) bytes;

    if (patch_enabled < 0) {
        patch_enabled = getenv("MELEE_MENU_LABEL_PROOF") != NULL;
    }
    if (!patch_enabled || patched || data == NULL) {
        return;
    }

    /* Slot 4 is "Data", the last main-menu label.  Overwriting it is the
     * proof: if the menu's fifth option reads "Melee Unbound", generated art
     * has reached the screen through the real texture path. */
    if (getenv("MELEE_MENU_LABEL_TRACE") != NULL &&
        size > HSD_DATA_BASE + MNMA_IMAGETBL_US + 20u)
    {
        const unsigned char* e =
            data + HSD_DATA_BASE + MNMA_IMAGETBL_US + 16u;
        unsigned off = rd32(e);
        fprintf(stderr, "[menu] archive %u bytes, slot4 desc off 0x%x\n",
                (unsigned) size, off);
        if (off != 0 && HSD_DATA_BASE + off + 0x10u <= size) {
            const unsigned char* dd = data + HSD_DATA_BASE + off;
            fprintf(stderr, "[menu]   image 0x%x %ux%u fmt %u\n", rd32(dd),
                    rd16(dd + 4), rd16(dd + 6), rd32(dd + 8));
        }
    }

    if (replace_label(data, size, 4, unbound_menu_label,
                      sizeof(unbound_menu_label)))
    {
        patched = 1;
        fprintf(stderr,
                "[menu] replaced label slot 4 with the generated "
                "\"Melee Unbound\" texture (%u bytes)\n",
                (unsigned) sizeof(unbound_menu_label));
    }
}
