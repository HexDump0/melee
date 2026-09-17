/*
 * Camera interposer: the engine side of UNBOUND_HOOK_CAMERA_SETUP.
 *
 * The shim (native/decomp/shim/decomp_shim.h) renames HSD_CObjSetCurrent and
 * HSD_CObjLoadDesc in every compiled decomp TU so the calls arrive here
 * first.  That is the same mechanism the asset converter has used since S3,
 * it costs nothing when no mod is loaded, and it needs no dynamic loader --
 * which is why it works identically in the browser.
 *
 * This file is the only place that knows which engine function is behind
 * UNBOUND_HOOK_CAMERA_SETUP.  When the decomp pin moves and these get
 * renamed or split, this file changes and no mod does.
 *
 * **The camera is restored before the engine sees it again.**  A handler's
 * values are applied, the real setup runs, and the original projection goes
 * straight back.  That is what lets the hook be classified PRESENT: the
 * camera object the game reads on the next line is byte-for-byte the one it
 * wrote, so nothing downstream -- the zoom fit in cm/camera.c, the on-screen
 * tests, the reflection cameras -- can observe that a mod ran.
 */
#include "mod/mod.h"

#include <string.h>

#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/video.h>

/*
 * The background-flash camera.  lbbgflash.c draws a full-screen quad sized
 * to exactly fill this camera's frustum, so widening the frustum leaves
 * unflashed bars down both sides -- the community's console widescreen code
 * carries a dedicated gate for the same camera, reached through a spare
 * register.  We can name it instead.
 */
extern HSD_CameraDescPerspective lbl_803BB028;

/*
 * Cameras built from the overlay descriptor.  lbbgflash.c loads and frees
 * one repeatedly, so this is a small ring rather than a set: a stale entry
 * can only survive until the ring wraps, and the worst it can do is leave one
 * camera unwidened for the frames before it is overwritten.  Identity beats
 * inferring the role from the numbers -- a heuristic that "looks like an
 * overlay" is exactly the shape of bug that cost us P-830.
 */
#define OVERLAY_RING 16
static const HSD_CObj* overlay_ring[OVERLAY_RING];
static unsigned overlay_next;

static void overlay_note(const HSD_CObj* cobj)
{
    overlay_ring[overlay_next] = cobj;
    overlay_next = (overlay_next + 1) % OVERLAY_RING;
}

static int overlay_has(const HSD_CObj* cobj)
{
    unsigned i;
    for (i = 0; i < OVERLAY_RING; ++i) {
        if (overlay_ring[i] == cobj) {
            return 1;
        }
    }
    return 0;
}

/*
 * HSD_RenderPass is ordered SCREEN, TOPHALF, BOTTOMHALF, OFFSCREEN; the ABI
 * numbers its own way and must keep doing so.  Mapping here rather than
 * casting is the point: the ABI is a promise to mods, the engine enum is not.
 */
static int pass_to_abi(HSD_RenderPass pass)
{
    switch (pass) {
    case HSD_RP_SCREEN:
        return UNBOUND_PASS_SCREEN;
    case HSD_RP_TOPHALF:
        return UNBOUND_PASS_TOPHALF;
    case HSD_RP_BOTTOMHALF:
        return UNBOUND_PASS_BOTTOMHALF;
    case HSD_RP_OFFSCREEN:
        return UNBOUND_PASS_OFFSCREEN;
    default:
        return UNBOUND_PASS_OTHER;
    }
}

HSD_CObj* unbound_HSD_CObjLoadDesc(HSD_CObjDesc* desc)
{
    HSD_CObj* cobj = HSD_CObjLoadDesc(desc);
    if (cobj != NULL && desc == (HSD_CObjDesc*) &lbl_803BB028) {
        overlay_note(cobj);
    }
    return cobj;
}

bool unbound_HSD_CObjSetCurrent(HSD_CObj* cobj)
{
    UnboundCameraSetup setup;
    HSD_RenderPass pass;
    bool result;
    union {
        struct {
            f32 fov;
            f32 aspect;
        } perspective;
        struct {
            f32 top;
            f32 bottom;
            f32 left;
            f32 right;
        } frustum;
    } saved;

    if (cobj == NULL || !mod_hook_active(UNBOUND_HOOK_CAMERA_SETUP)) {
        return HSD_CObjSetCurrent(cobj);
    }

    pass = HSD_GetCurrentRenderPass();

    memset(&setup, 0, sizeof(setup));
    /*
     * The handle is the pointer, but it is only ever compared, never
     * dereferenced: it crosses into a sandbox where it is meaningless as an
     * address.  Mods use it to tell one camera from another across frames.
     */
    setup.camera = (unsigned) (uintptr_t) cobj;
    setup.projection = cobj->projection_type;
    setup.render_pass = pass_to_abi(pass);
    if (overlay_has(cobj)) {
        setup.role = UNBOUND_CAMERA_ROLE_OVERLAY;
    } else if (pass == HSD_RP_OFFSCREEN) {
        setup.role = UNBOUND_CAMERA_ROLE_OFFSCREEN;
    } else {
        setup.role = UNBOUND_CAMERA_ROLE_NORMAL;
    }

    switch (cobj->projection_type) {
    case PROJ_PERSPECTIVE:
    case PROJ_FRUSTUM:
        setup.aspect = cobj->projection_param.perspective.aspect;
        break;
    case PROJ_ORTHO:
        setup.ortho_left = cobj->projection_param.ortho.left;
        setup.ortho_right = cobj->projection_param.ortho.right;
        break;
    default:
        return HSD_CObjSetCurrent(cobj);
    }

    mod_dispatch(UNBOUND_HOOK_CAMERA_SETUP, &setup, sizeof(setup));

    memcpy(&saved, &cobj->projection_param, sizeof(saved));
    switch (cobj->projection_type) {
    case PROJ_PERSPECTIVE:
    case PROJ_FRUSTUM:
        cobj->projection_param.perspective.aspect = setup.aspect;
        break;
    case PROJ_ORTHO:
        cobj->projection_param.ortho.left = setup.ortho_left;
        cobj->projection_param.ortho.right = setup.ortho_right;
        break;
    default:
        break;
    }

    result = HSD_CObjSetCurrent(cobj);

    /* Unconditionally, including on the failure path: a camera left widened
     * would be read by the engine's own logic on the next line. */
    memcpy(&cobj->projection_param, &saved, sizeof(saved));
    return result;
}
