#ifndef UNBOUND_ABI_H
#define UNBOUND_ABI_H

/*
 * The Melee Unbound mod ABI (ADR-0026).
 *
 * This header is the contract, and it is included by both sides:
 *
 *   - the host (native/mod/), built -m32 for the desktop and browser ports;
 *   - a mod, built either for wasm32 (a `.wasm` dropped into `mods/`, which
 *     is how every mod including Unbound's own ships on the desktop) or
 *     natively into the binary (which is how Unbound ships in the browser,
 *     where there is no loader and Unbound is the only mod).
 *
 * **Both build modes compile the same mod sources.**  That is the whole point
 * of keeping the ABI in a header rather than in a loader: the browser's
 * precompiled Unbound and the desktop's `unbound.wasm` are the same C, so a
 * feature cannot silently exist on one target and not the other.
 *
 * Layout rules, because the host is x86-32 and a guest is wasm32:
 *
 *   - Only `int`, `unsigned`, `float` and fixed-size arrays of them appear in
 *     a payload.  Both ABIs are 32-bit little-endian with 4-byte alignment
 *     for all of those, so a payload struct has an identical layout on both
 *     sides and can be copied verbatim across the boundary.
 *   - **No pointers ever cross.**  A guest cannot dereference a host address,
 *     so objects are named by opaque `unsigned` handles and strings are
 *     passed as (offset, length) into the guest's own memory.
 *   - Fields are never reordered or removed.  New fields go on the end and
 *     bump UNBOUND_ABI_VERSION.
 */

#define UNBOUND_ABI_VERSION 6u

/* Chains run low priority first; ties break on registration order. */
#define UNBOUND_PRIORITY_EARLY 100
#define UNBOUND_PRIORITY_NORMAL 500
#define UNBOUND_PRIORITY_LATE 900

/*
 * Does a hook change what the game computes, or only what this machine draws?
 *
 * Netplay is not being built yet and nothing here implements it.  This exists
 * now because it is one field today and an archaeology project later: a
 * presentation mod is one two players can pick differently without desyncing,
 * and a simulation mod is not.  Every hook is classified at the point it is
 * defined, and the host reports whether the loaded mod set touches any
 * simulation hook at all.
 */
enum {
    UNBOUND_EFFECT_PRESENT = 0,
    UNBOUND_EFFECT_SIM = 1
};

/*
 * Hooks are named for what happens, not for the engine symbol behind them.
 * The decomp pin moves and functions get renamed or split; when that happens
 * we fix the one interposer in native/mod/mod_cobj.c and every mod that was
 * built against this header keeps working.
 */
enum {
    /*
     * A camera is about to be turned into a GX projection matrix.
     *
     * PRESENT, and the mechanism is what makes that true: whatever a handler
     * writes back is applied for this one setup and then restored, so the
     * camera object the game reads on the next line is byte-for-byte the one
     * it wrote.  The engine's own camera logic -- notably the zoom fit in
     * cm/camera.c, which reads the static descriptor rather than the live
     * camera -- never sees a modified value.
     */
    UNBOUND_HOOK_CAMERA_SETUP = 0,

    /* The drawable changed size.  PRESENT by construction. */
    UNBOUND_HOOK_DISPLAY_RESIZED = 1,

    /*
     * Once per presented frame, after the game's own frame is drawn and
     * before it reaches the screen (ABI 4).  This is where a mod draws an
     * overlay and reads input.
     *
     * PRESENT: it runs after the game has finished deciding what this frame
     * contains, so nothing a handler does here can change what was simulated.
     * A hook that let a mod act *before* the game's frame would be SIM and is
     * deliberately not this one.
     */
    UNBOUND_HOOK_FRAME = 2,

    UNBOUND_HOOK_COUNT = 3
};

/* Mirrors HSD's projection kinds so a mod needs no decomp header. */
enum {
    UNBOUND_PROJECTION_PERSPECTIVE = 1,
    UNBOUND_PROJECTION_FRUSTUM = 2,
    UNBOUND_PROJECTION_ORTHO = 3
};

/* Mirrors HSD_RenderPass. */
enum {
    UNBOUND_PASS_SCREEN = 0,
    UNBOUND_PASS_OFFSCREEN = 1,
    UNBOUND_PASS_TOPHALF = 2,
    UNBOUND_PASS_BOTTOMHALF = 3,
    UNBOUND_PASS_OTHER = 4
};

/*
 * What the host knows about this camera beyond its projection.  A mod that
 * widens the view must leave overlays alone: a full-screen flash quad is
 * authored to exactly fill its frustum, so widening the frustum leaves
 * unflashed bars down both sides.  The host identifies those by descriptor
 * identity rather than by guessing from the numbers, so a mod does not have
 * to.
 */
enum {
    UNBOUND_CAMERA_ROLE_NORMAL = 0,
    /* A full-screen overlay (the background flash). Do not widen. */
    UNBOUND_CAMERA_ROLE_OVERLAY = 1,
    /* Renders to a texture, not the screen (shadows, reflections). */
    UNBOUND_CAMERA_ROLE_OFFSCREEN = 2
};

/*
 * UNBOUND_HOOK_CAMERA_SETUP payload.
 *
 * `projection`, `render_pass`, `role` and `camera` are inputs.  `aspect` (for
 * perspective and frustum) and `ortho_left`/`ortho_right` (for ortho) are
 * in/out: write them to change this setup, leave them to change nothing.
 */
typedef struct UnboundCameraSetup {
    unsigned camera; /* opaque handle, stable while the camera lives */
    int projection;
    int render_pass;
    int role;
    float aspect;
    float ortho_left;
    float ortho_right;
    /*
     * The camera's own viewport, in 640x480 EFB pixels (ABI 2).
     *
     * A camera that does not cover the whole EFB is drawing into a fixed
     * sub-rectangle -- the off-screen-player magnifier's bubble, a shadow
     * map -- and that rectangle's shape did not change when the window did,
     * because the EFB is fitted to the window uniformly.  So a display mod
     * must leave those alone, and this is how it tells.  It is a fact about
     * the camera rather than a guess about what it draws.
     */
    float viewport_w;
    float viewport_h;
} UnboundCameraSetup;

/*
 * Controller buttons, matching the console's bits so a mod does not have to
 * learn a second encoding.
 */
enum {
    UNBOUND_BUTTON_DPAD_LEFT = 0x0001,
    UNBOUND_BUTTON_DPAD_RIGHT = 0x0002,
    UNBOUND_BUTTON_DPAD_DOWN = 0x0004,
    UNBOUND_BUTTON_DPAD_UP = 0x0008,
    UNBOUND_BUTTON_Z = 0x0010,
    UNBOUND_BUTTON_R = 0x0020,
    UNBOUND_BUTTON_L = 0x0040,
    UNBOUND_BUTTON_A = 0x0100,
    UNBOUND_BUTTON_B = 0x0200,
    UNBOUND_BUTTON_X = 0x0400,
    UNBOUND_BUTTON_Y = 0x0800,
    UNBOUND_BUTTON_START = 0x1000
};

/*
 * UNBOUND_HOOK_FRAME payload.  Read-only.
 *
 * Overlay drawing is in a fixed 640x480 space whatever the window is doing,
 * the same space the game's own 2D is authored in, so a mod never has to
 * think about the drawable.
 */
typedef struct UnboundFrame {
    unsigned frame;    /* frames presented since boot */
    int scene;         /* UNBOUND_SCENE_*, as of this frame */
    float draw_width;  /* always 640 */
    float draw_height; /* always 480 */
} UnboundFrame;

/* UNBOUND_HOOK_DISPLAY_RESIZED payload.  Read-only. */
typedef struct UnboundDisplayResized {
    int width;
    int height;
} UnboundDisplayResized;

/*
 * What kind of screen the game is showing (ABI 3).
 *
 * GAMEPLAY means there is a 3D world behind the camera, so showing more of it
 * is showing more *game*.  OTHER means a screen composed for 4:3 -- menus,
 * splashes, results, cutscenes -- where the frame is a picture rather than a
 * window, and widening it can only reveal the edge of the composition.  The
 * backdrop plates on those screens are authored with about 18% of overscan
 * margin, which covered 4:3 comfortably and falls short of 16:9 by roughly 6%
 * a side.
 *
 * Fixing those properly means editing the plates themselves, which is what
 * the community's ISO patch does -- ~13 KB of edits across ~200 archives.
 * Until a mod can do that at load time, a display mod is expected to leave
 * OTHER screens at their authored aspect.
 */
enum {
    UNBOUND_SCENE_UNKNOWN = 0,
    UNBOUND_SCENE_GAMEPLAY = 1,
    UNBOUND_SCENE_OTHER = 2
};

/* The largest payload the host will copy into a guest's buffer. */
#define UNBOUND_PAYLOAD_MAX 256

#endif /* UNBOUND_ABI_H */
