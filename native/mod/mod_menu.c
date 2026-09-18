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
#include <melee/mn/inlines.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sislib.h>
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

/* A row of `mn_803EB6B0` the game leaves empty, used as our page's menu
 * kind.  Any kind renders, because `mn_8022B3A0` hardcodes the panel
 * model rather than choosing one per kind. */
#define UNBOUND_MENU_KIND 33

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
    if (entry_enabled && jobj != NULL && frame == UNBOUND_LABEL_FRAME &&
        (mn_804A04F0.cur_menu == MENU_KIND_MAIN ||
         mn_804A04F0.cur_menu == UNBOUND_MENU_KIND))
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
static int unbound_menu_confirm_pressed(void)
{
    return (gm_GetButtonsTriggered(4) & PAD_CONFIRM) != 0;
}

/*
 * Start, edge-detected here rather than taken from the engine.
 *
 * Neither source reports it: `gm_GetButtonsTriggered(4)` carries Confirm but
 * not Start, and `HSD_PadCopyStatus[].trigger` reads 0 on the very frame the
 * held mask shows 0x1000 -- traced, and it is why two earlier attempts at
 * this did nothing at all.  The held mask is reliable, so the rising edge is
 * computed from it.
 */
static unsigned start_held_last;

static int unbound_menu_start_pressed(void)
{
    unsigned held = 0;
    unsigned edge;
    int port;

    for (port = 0; port < 4; ++port) {
        held |= mod_engine_buttons_held(port) & PAD_BUTTON_START;
    }
    edge = held & ~start_held_last;
    start_held_last = held;
    return edge != 0;
}

/*
 * A real menu page, not an overlay.
 *
 * Entering a submenu is five calls, all of them reachable: set prev/cur menu
 * and selection, rebuild the panel with `mn_8022B3A0` (that is the
 * transition), free the think that is running, and start the new menu's
 * think.  This is exactly what selecting "Options" does.
 *
 * It works for a menu kind the game never used because `mn_8022B3A0`
 * hardcodes `model = &MenMainConTop_Top` -- every menu kind renders through
 * the same panel, and what differs is the animation range, the option count
 * and the labels.  So there is no missing-panel problem for a new page.
 */
/*
 * The menu's SIS context, taken from the description box as it is built.
 *
 * `HSD_SisLib_803A611C` binds a context to a GObj and `mn_804D6BB4` holds
 * the menu's, but that is a static in mnmain.c.  The description is created
 * with it, so intercepting that call is how we get one -- and text created
 * with the same context renders with the menu.
 */
static s32 menu_text_ctx;
static int menu_text_ctx_valid;

static int page_open;

/*
 * The credits, drawn by the engine's own text renderer.
 *
 * `HSD_SisLib_803A6B98` is printf-shaped and renders in the game's font, but
 * only into an object from `HSD_SisLib_803A6754`, which allocates the block
 * it writes through -- the menu's own description comes from
 * `HSD_SisLib_803A5ACC` and has none, so printing into that one faults at
 * 0x8.  So the page makes its own.
 *
 * The lines live here rather than in the mod because the ABI has no way to
 * ask for a native text object yet; that is the gap this feature found and
 * P-839 records.
 */
static HSD_Text* page_text;

static const char* const credit_lines[] = {
    "MELEE UNBOUND",
    "A native port of Melee",
    "",
    "Port     HexDump0",
    "Built on doldecomp",
    "Runtime  WAMR + SDL3",
    "",
    "No game data ships",
    "Bring your own disc",
};

/*
 * Behind MELEE_MENU_NATIVE_TEXT=1 while the coordinate space is worked out.
 *
 * What is established: the text renders, in the game's own font, so this is
 * the right road.  `pos_x/pos_y/pos_z` place the object (the description's
 * -9.5, 9.1, 17 puts it in the bottom box) and `HSD_SisLib_803A6B98`'s x,y
 * offset each string within it.  What is not: the mapping from those units to
 * a block of lines centred in the panel.
 *
 * Also established the hard way: the engine encodes through a 128-byte stack
 * buffer, so a long line smashes the stack rather than truncating.  Keep
 * lines short until that is bounded properly.
 */
/*
 * Screen placement for the engine's text, in the authored 640x480 space.
 * Derived by measuring probe markers; see page_text_create.
 */
#define TEXT_BASE_Y 417.0f  /* where a y offset of 0 lands */
#define TEXT_BASE_X 146.5f  /* where an x offset of 0 puts the left edge */
#define TEXT_UNIT 0.517f    /* screen pixels per text unit at font 0.0278 */
#define TEXT_GLYPH_W 15.0f  /* glyph advance */
#define TEXT_CENTRE_X 320.0f
/* Clear of the panel's header pill, which sits around y 105..140 and is not
 * part of `MainMenuData::tree` -- hiding all 42 of those joints leaves it
 * untouched, so it belongs to a GObj this file has not found yet (P-839). */
#define TEXT_TOP_Y 172.0f
#define TEXT_LINE_STEP 23.0f

static float text_off_y(float screen_y)
{
    return (screen_y - TEXT_BASE_Y) / TEXT_UNIT;
}

static float text_off_x_centred(const char* line)
{
    unsigned len = 0;
    float left;
    while (line[len] != '\0') {
        ++len;
    }
    left = TEXT_CENTRE_X - 0.5f * (float) len * TEXT_GLYPH_W;
    return (left - TEXT_BASE_X) / TEXT_UNIT;
}

/*
 * Hide the panel's own furniture on our page.
 *
 * Every menu kind renders through `MenMainConTop_Top`, so our page inherits
 * the breadcrumb header and the option pills whether or not it wants them --
 * they are driven by per-menu animation state we do not set, and with the
 * mod's overlay plate gone they show through the credits.
 *
 * `mn_8022B3A0` hands back the panel GObj, and `MainMenuData::tree` is the
 * same flattened joint array the menu indexes by `mn_803EAE68`, so the
 * header and the option anchors can simply be marked hidden.
 */
static HSD_GObj* page_panel;

static void page_hide_furniture(HSD_GObj* panel)
{
    MainMenuData* data;
    unsigned i;
    unsigned hide_max;

    if (panel == NULL) {
        return;
    }
    data = (MainMenuData*) HSD_GObjGetUserData(panel);
    if (data == NULL) {
        return;
    }
    if (getenv("MELEE_MENU_FURNITURE_TRACE") != NULL) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr,
                    "[menu] panel data=%p tree3=%p tree4=%p tree14=%p\n",
                    (void*) data, (void*) data->tree[3],
                    (void*) data->tree[4], (void*) data->tree[14]);
        }
    }
    /* 0..3 is the header and breadcrumb, 4..13 the ten option anchors;
     * MELEE_MENU_HIDE_MAX raises the bound while the rest are identified. */
    {
        const char* env = getenv("MELEE_MENU_HIDE_MAX");
        hide_max = env != NULL ? (unsigned) atoi(env) : 13u;
    }
    for (i = 0; i <= hide_max && i < 42u; ++i) {
        if (data->tree[i] != NULL) {
            /* Subtree, not the single joint: the meshes hang below these
             * anchors, and `displayfunc.c` tests the flag on the node it is
             * drawing.  The animation also drives this flag (jobj.c:422), so
             * it has to be re-applied every frame rather than set once. */
            HSD_JObjSetFlagsAll(data->tree[i], JOBJ_HIDDEN);
        }
    }
}

static void page_text_create(void)
{
    unsigned i;
    float y;

    if (page_text != NULL || !menu_text_ctx_valid) {
        return;
    }
    if (getenv("MELEE_MENU_NO_NATIVE_TEXT") != NULL) {
        return;
    }
    page_text = HSD_SisLib_803A6754(0, menu_text_ctx);
    if (page_text == NULL) {
        return;
    }
    /* `fn_8022AFEC` clears this on the description every frame, so a fresh
     * text object starts hidden and draws nothing until it is cleared. */
    /*
     * A fresh text object starts hidden and at font size 1.0, which is the
     * whole screen per glyph -- `fn_8022AFEC` clears `hidden` on the
     * description every frame and `mn_80229A7C` sets its size to 0.0521, so
     * both are the caller's job rather than the constructor's.
     */
    page_text->hidden = 0;
    page_text->pos_x = -9.5f;
    page_text->pos_y = 9.1f;
    page_text->pos_z = 17.0f;
    page_text->box_size_x = 364.68332f;
    page_text->box_size_y = 38.38772f;
    page_text->font_size.x = 0.0278f;
    page_text->font_size.y = 0.0278f;
    /*
     * `fitting` stretches a string across the box width, which is why short
     * lines came out letter-spaced to the edges.  The box is wide because it
     * was copied from the description; the lines are not, so turn fitting off
     * and let them set naturally.
     */
    page_text->default_fitting = 0;
    page_text->fitting = 0;
    page_text->default_kerning = 1;
    page_text->kerning = 1;
    page_text->text_color.r = 0xFF;
    page_text->text_color.g = 0xFF;
    page_text->text_color.b = 0xFF;
    page_text->text_color.a = 0xFF;

    /*
     * Place the block by measurement, not by guesswork.
     *
     * Four markers drawn at known offsets (MELEE_MENU_TEXT_PROBE=1) and
     * measured off the screenshot give both axes at font size 0.0278:
     *
     *     screen_y = 417.0 + 0.517 * y_offset      (negative offset is up)
     *     left_x   = 146.5 + 0.517 * x_offset
     *     glyph advance ~15 px, glyph height ~11 px, all in 640x480 space
     *
     * The unit scale tracks font_size -- it was 0.975 at 0.0521 -- so these
     * constants belong with that size and move if it does.
     */
    if (getenv("MELEE_MENU_TEXT_PROBE") != NULL) {
        HSD_SisLib_803A6B98(page_text, 0.0f, 0.0f, "AAAA");
        HSD_SisLib_803A6B98(page_text, 100.0f, -40.0f, "BBBB");
        HSD_SisLib_803A6B98(page_text, 0.0f, -300.0f, "CCCCCCCC");
        HSD_SisLib_803A6B98(page_text, -100.0f, -340.0f, "DDDD");
        return;
    }

    y = TEXT_TOP_Y;
    for (i = 0; i < sizeof(credit_lines) / sizeof(credit_lines[0]); ++i) {
        const char* line = credit_lines[i];
        if (line[0] != '\0') {
            HSD_SisLib_803A6B98(page_text, text_off_x_centred(line),
                                text_off_y(y), line);
        }
        y += TEXT_LINE_STEP;
    }
}

static void page_text_destroy(void)
{
    if (page_text != NULL) {
        HSD_SisLib_803A5CC4(page_text);
        page_text = NULL;
    }
}

static void start_menu_think(void (*think)(HSD_GObj*))
{
    HSD_GObjProc* proc;
    HSD_GObj* gobj;

    if (think == NULL) {
        return;
    }
    gobj = GObj_Create(0, 1, 0x80);
    proc = HSD_GObj_SetupProc(gobj, think, 0);
    proc->flags_3 = HSD_GObj_804D783C;
}

static void unbound_page_think(HSD_GObj* gp)
{
    u32 buttons = mn_80229624(4);
    (void) gp;

    mn_804A04F0.buttons = buttons;
    /* Every frame: the panel proc re-animates these joints, so hiding them
     * once at entry does not hold. */
    page_hide_furniture(page_panel);
    if ((buttons & MenuInput_Back) == 0) {
        return;
    }
    /* Back out the way every other submenu does, landing on our own entry. */
    sfxBack();
    page_text_destroy();
    page_panel = NULL;
    page_open = 0;
    mn_804A04F0.entering_menu = 0;
    mn_804D6BC8.cooldown = 5;
    mn_804A04F0.prev_menu = mn_804A04F0.cur_menu;
    mn_804A04F0.cur_menu = MENU_KIND_MAIN;
    mn_804A04F0.hovered_selection = UNBOUND_SELECTION;
    HSD_GObj_80390CD4(mn_8022B3A0(3));
    HSD_GObjFree(HSD_GObj_CurrentInvokedProcGObj);
    start_menu_think(mn_803EB6B0[MENU_KIND_MAIN].think);
}

static void enter_unbound_page(void)
{
    sfxForward();
    page_open = 1;
    page_text_create();
    mn_804D6BC8.cooldown = 5;
    mn_804A04F0.entering_menu = 1;
    mn_804A04F0.prev_menu = mn_804A04F0.cur_menu;
    mn_804A04F0.cur_menu = UNBOUND_MENU_KIND;
    mn_804A04F0.hovered_selection = 0;
    page_panel = mn_8022B3A0(1);
    HSD_GObj_80390CD4(page_panel);
    page_hide_furniture(page_panel);
    HSD_GObjFree(HSD_GObj_CurrentInvokedProcGObj);
    start_menu_think(unbound_page_think);
}

int mod_menu_page_open(void) { return page_open; }

static void unbound_main_think(HSD_GObj* gp)
{
    int hovered = entry_enabled &&
                  mn_804A04F0.cur_menu == MENU_KIND_MAIN &&
                  mn_804A04F0.hovered_selection == UNBOUND_SELECTION;


    /*
     * Accept Start as well as Confirm: on the keyboard Z is A and Enter is
     * Start, and pressing Enter on an entry and having nothing happen reads
     * as broken rather than as a mapping.
     *
     * Start is read straight off the pad rather than from
     * `gm_GetButtonsTriggered(4)`, which carries Confirm but not Start --
     * tested, and it is why the first attempt at this did nothing.
     */
    if (hovered && (unbound_menu_confirm_pressed() ||
                    unbound_menu_start_pressed()))
    {
        /*
         * Handle our option here and do not let the retail think run: its
         * switch covers selections 0..4 only, and falling off the end of it
         * would dispatch an uninitialised menu kind.
         */
        activated = 1;
        enter_unbound_page();
        return;
    }
    if (real_main_think != NULL) {
        real_main_think(gp);
    }
}

/*
 * The menu's description box, identified by the geometry `mn_80229A7C` builds
 * it with.  82 sites create text objects; matching the box's own width and
 * height is what separates this one from the rest -- gating on "main menu,
 * our entry hovered" instead would be true for every one of them.
 */
#define DESC_BOX_W 364.68332f
#define DESC_BOX_H 38.38772f

/* SIS string 0.  `HSD_SisLib_803A6754` initialises a fresh text object with
 * it, so it is the engine's own idea of "nothing yet". */
#define SIS_EMPTY 0

static HSD_Text* description_text;

HSD_Text* unbound_HSD_SisLib_803A5ACC(int a, s32 b, f32 c, f32 d, f32 e,
                                      f32 w, f32 h)
{
    HSD_Text* text = HSD_SisLib_803A5ACC(a, b, c, d, e, w, h);
    if (w == DESC_BOX_W && h == DESC_BOX_H) {
        description_text = text;
        menu_text_ctx = b;
        menu_text_ctx_valid = 1;
    }
    return text;
}

void unbound_HSD_SisLib_803A6368(HSD_Text* text, s32 index)
{
    if (entry_enabled && text != NULL && text == description_text &&
        ((mn_804A04F0.cur_menu == MENU_KIND_MAIN &&
          mn_804A04F0.hovered_selection == UNBOUND_SELECTION) ||
         mn_804A04F0.cur_menu == UNBOUND_MENU_KIND))
    {
        /*
         * Blank, not printed: `HSD_SisLib_803A6B98` would render our own
         * words in the game's font, but it dereferences `text->alloc_data`
         * and an object from `803A5ACC` has none -- it faults at 0x8.  The
         * mod draws the description instead; see learnings/mod_system.md.
         */
        index = SIS_EMPTY;
    }
    HSD_SisLib_803A6368(text, index);
}

int mod_menu_entry_hovered(void)
{
    return entry_enabled && mn_804A04F0.cur_menu == MENU_KIND_MAIN &&
           mn_804A04F0.hovered_selection == UNBOUND_SELECTION;
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

    /*
     * Our page borrows the main menu's animation range -- `mn_8022B3A0` does
     * pointer arithmetic on `anim_loop` before it looks at anything else, so
     * a NULL there is a crash rather than an empty page.  No options and no
     * description indices: the page is text, which the mod draws.
     */
    mn_803EB6B0[UNBOUND_MENU_KIND].anim_loop =
        mn_803EB6B0[MENU_KIND_MAIN].anim_loop;
    /*
     * One option, not none.  `fn_8022AFEC` fills `sp20` with one entry per
     * selection and then indexes it by the hovered selection regardless, so
     * a page with `selection_count = 0` reads uninitialised stack and
     * segfaults -- SIGSEGV at 0x4d.  One option gives it something real, and
     * the page uses it as its Back pill.
     *
     * `start_frame` is 10 so the pill asks the label TexAnim for the same
     * frame our main-menu entry does, which is the frame the texture
     * override recognises.
     */
    mn_803EB6B0[UNBOUND_MENU_KIND].start_frame = UNBOUND_LABEL_FRAME;
    /*
     * Real indices, not NULL.  `mn_80229A7C` only builds the description's
     * text object when the menu has indices, but `fn_8022AFEC` then writes
     * `final_data->description->hidden` without checking -- so a page with no
     * indices faults on a NULL text object at offset 0x4d, which is exactly
     * where `hidden` sits.  The interposer below blanks the string instead.
     */
    mn_803EB6B0[UNBOUND_MENU_KIND].description_indices = unbound_descriptions;
    mn_803EB6B0[UNBOUND_MENU_KIND].selection_count = 1;
    mn_803EB6B0[UNBOUND_MENU_KIND].think = unbound_page_think;

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
