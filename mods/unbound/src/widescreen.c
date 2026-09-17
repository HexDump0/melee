/*
 * Widescreen (P-831).
 *
 * Melee renders into a fixed 640x480 EFB and its cameras are authored for a
 * 4:3 display, so on a 16:9 window something has to give.  The port used to
 * scale x and y independently, which fills the window by stretching the
 * picture.  This feature does it properly instead: it tells the host to
 * present the EFB at the window's own aspect (so the fit is uniform and
 * nothing is stretched) and widens each camera's horizontal field by the same
 * factor, so the extra width is extra *view* rather than extra pixels per
 * unit.
 *
 * Both halves are one number, `scale`, and they must always agree -- a
 * display aspect without the camera widening is a stretched image, and camera
 * widening without the display aspect is a squashed one.
 *
 * Presentation only.  The host applies what this writes for one camera setup
 * and then restores the camera, so the engine's own camera logic never reads
 * a widened value.  That matters beyond tidiness: cm/camera.c fits the shot
 * to the players using the aspect from its *static descriptor*, so leaving
 * the live camera untouched means the camera moves exactly where it would
 * have on a 4:3 screen and the extra width is purely extra view.
 */
#include "unbound_mod.h"

/* What the retail game's cameras are authored against. */
#define NATIVE_DISPLAY_ASPECT (4.0f / 3.0f)

static int enabled = 1;
static float scale = 1.0f;

static void widescreen_recompute(void)
{
    int w = unbound_display_width();
    int h = unbound_display_height();
    float aspect;

    if (!enabled || w <= 0 || h <= 0) {
        unbound_display_set_aspect(NATIVE_DISPLAY_ASPECT);
        scale = 1.0f;
        return;
    }

    aspect = (float) w / (float) h;
    /*
     * Never go narrower than the authored aspect.  A window taller than 4:3
     * would otherwise crop the sides off a shot the game believes is fully
     * visible, which is a gameplay change rather than a presentation one;
     * letterboxing it top and bottom keeps every pixel the game drew.
     */
    if (aspect < NATIVE_DISPLAY_ASPECT) {
        aspect = NATIVE_DISPLAY_ASPECT;
    }

    unbound_display_set_aspect(aspect);
    scale = aspect / NATIVE_DISPLAY_ASPECT;
}

void widescreen_init(void)
{
    enabled = unbound_config_int("widescreen", 1);
    widescreen_recompute();
}

void widescreen_on_display_resized(const UnboundDisplayResized* size)
{
    (void) size;
    widescreen_recompute();
}

void widescreen_on_camera_setup(UnboundCameraSetup* cam)
{
    if (scale == 1.0f) {
        return;
    }
    /*
     * Leave two kinds of camera alone.  An OVERLAY is a full-screen quad
     * authored to exactly fill its frustum -- widen it and the flash gets
     * unflashed bars down both sides.  An OFFSCREEN camera renders into a
     * texture of its own (shadow maps, reflections), which has nothing to do
     * with the shape of the window.
     */
    if (cam->role != UNBOUND_CAMERA_ROLE_NORMAL) {
        return;
    }
    if (cam->render_pass == UNBOUND_PASS_OFFSCREEN) {
        return;
    }
    /*
     * Only cameras that cover the whole EFB.  One that draws into a fixed
     * sub-rectangle -- the off-screen-player magnifier renders its bubble
     * through a camera whose viewport is the bubble's own width and height --
     * is drawing into a shape the window never changed, since the EFB is
     * fitted to the window uniformly.  Widening those squashes their
     * contents.
     */
    if (cam->viewport_w < 639.0f || cam->viewport_h < 479.0f) {
        return;
    }

    switch (cam->projection) {
    case UNBOUND_PROJECTION_PERSPECTIVE:
    case UNBOUND_PROJECTION_FRUSTUM:
        cam->aspect *= scale;
        break;
    case UNBOUND_PROJECTION_ORTHO: {
        /*
         * Widen about the camera's own centre, not about zero.
         *
         * Scaling both edges only works for a camera authored symmetrically;
         * an asymmetric one -- `grpstadium.c:1176` is `left=0, right=250` --
         * has its centre multiplied along with its width, so the view slides
         * sideways instead of widening.  Hold the centre and grow the half
         * width.
         *
         * Widening at all keeps 2D elements the right *shape*: the rect they
         * are presented into got wider by exactly this factor, so an
         * unwidened ortho camera would stretch them.  What it does not do is
         * move them -- they stay at their authored coordinates, so anything
         * anchored to a screen edge now sits inside the new one.  Re-anchoring
         * the HUD is its own task (P-834).
         */
        float centre = 0.5f * (cam->ortho_left + cam->ortho_right);
        float half = 0.5f * (cam->ortho_right - cam->ortho_left) * scale;
        cam->ortho_left = centre - half;
        cam->ortho_right = centre + half;
        break;
    }
    default:
        break;
    }
}
