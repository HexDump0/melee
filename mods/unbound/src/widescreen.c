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

/* MELEE_MOD_UNBOUND_WIDESCREEN */
#define WIDESCREEN_OFF 0
#define WIDESCREEN_GAMEPLAY 1 /* default */
#define WIDESCREEN_EVERYWHERE 2

static int mode = WIDESCREEN_GAMEPLAY;
static int scene = UNBOUND_SCENE_UNKNOWN;
static float scale = 1.0f;

/*
 * MELEE_MOD_UNBOUND_ASPECT_CORRECT: draw the world at the shape it is
 * presented at, instead of the shape Melee authored.
 *
 * **Melee's cameras are not 4:3.**  The game camera's descriptor
 * (`cm_803BCB64`, camera.c:76) carries an aspect of **1.2173333**, and the
 * background camera 1.18, while the picture is presented in a 4:3 rect -- so
 * retail is about 9.5% wider than a geometrically neutral projection, on a
 * GameCube and here alike.  That is the look, and it is the default.
 *
 * Widescreen does not add to it: this mod multiplies each camera's aspect by
 * the same factor the presentation rect grew, so the ratio between them --
 * the distortion -- is identical at 4:3 and 16:9.  Measured at 1280x720:
 * every `role=NORMAL` camera goes 1.2173 -> 1.2985 against a 1.4222 display,
 * a stretch of 1.0953, which is what 4:3 gives too.
 *
 * With this on, the world camera is set to the display's own aspect instead,
 * which makes characters ~9.5% narrower than retail.  It is a deliberate
 * departure, which is why it is off unless asked for.
 */
static int correct;

/*
 * Widescreen only where widening the frame shows more *game*.
 *
 * On a menu, splash or results screen there is no world behind the camera --
 * the frame is a composition authored for 4:3, and its backdrop plates carry
 * only about 18% of overscan margin, so at 16:9 they fall short by roughly 6%
 * a side and the background shows through.  That is not a positioning bug
 * this mod can correct: the plates themselves are the wrong size, and fixing
 * them means editing the data (see P-837), which is exactly what the
 * community's ISO patch does across ~200 archives.
 *
 * So those screens get their authored aspect and honest pillarbox bars, which
 * is what the retail game looks like, and gameplay gets the wider view.  Set
 * MELEE_MOD_UNBOUND_WIDESCREEN=2 to widen everything anyway and see the gaps.
 */
static int widescreen_wanted(void)
{
    if (mode == WIDESCREEN_OFF) {
        return 0;
    }
    if (mode == WIDESCREEN_EVERYWHERE) {
        return 1;
    }
    return scene == UNBOUND_SCENE_GAMEPLAY;
}

static void widescreen_recompute(void)
{
    int w = unbound_display_width();
    int h = unbound_display_height();
    float aspect;

    if (!widescreen_wanted() || w <= 0 || h <= 0) {
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
    mode = unbound_config_int("widescreen", WIDESCREEN_GAMEPLAY);
    correct = unbound_config_int("aspect_correct", 0);
    scene = unbound_scene_kind();
    widescreen_recompute();
}

void widescreen_on_display_resized(const UnboundDisplayResized* size)
{
    (void) size;
    widescreen_recompute();
}

void widescreen_on_camera_setup(UnboundCameraSetup* cam)
{
    /*
     * Poll the scene here rather than waiting for a hook.  This runs during
     * the game's own render, so a scene change takes effect on the frame it
     * happens -- the display aspect and the camera factor are both updated
     * before anything in this frame is submitted, and they cannot disagree.
     */
    int now = unbound_scene_kind();
    if (now != scene) {
        scene = now;
        widescreen_recompute();
    }

    /* The correction has work to do even at 4:3, where `scale` is 1: the
     * authored aspect is not the display's there either. */
    if (scale == 1.0f && !(correct && widescreen_wanted())) {
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
        if (correct && widescreen_wanted()) {
            cam->aspect = unbound_display_get_aspect();
        } else {
            cam->aspect *= scale;
        }
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
