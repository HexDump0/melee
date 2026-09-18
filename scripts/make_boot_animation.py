#!/usr/bin/env python3
"""Render the Melee Unbound boot animation from the committed brand SVG.

Everything is derived from assets/melee-unbound-banner.svg, so the animation
can never drift from the logo. Re-run after any change to the mark.

    python3 scripts/make_boot_animation.py                 # -> assets/melee-unbound-boot.mp4
    python3 scripts/make_boot_animation.py --frames out/   # PNG sequence instead

Needs: rsvg-convert, ffmpeg (libx264), python3-pil, python3-numpy.
"""

import argparse
import math
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import wave

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "melee-unbound-banner.svg"
DEFAULT_OUT = ROOT / "assets" / "melee-unbound-boot.mp4"

W, H = 1920, 1080
FPS = 60
SR = 48000                     # audio sample rate
DURATION = 4.0
NFRAMES = int(round(FPS * DURATION))

# Ink bounds of the lockup inside its native 770x320 user space, measured.
INK = (29.0, 81.5, 718.0, 157.5)
INK_TARGET_W = 1100.0          # how wide the logo sits in a 1920-wide frame

# Layer order matches the SVG: disc, then ring over it, then the type.
LAYERS = ("disc", "ring", "melee", "unbound")


# --- timing -----------------------------------------------------------------

def clamp01(x):
    return 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)


def span(t, a, b):
    """Normalised progress through the window [a, b] seconds."""
    return clamp01((t - a) / (b - a)) if b > a else float(t >= b)


def out_cubic(x):
    return 1.0 - (1.0 - x) ** 3


def in_cubic(x):
    return x ** 3


def in_out_cubic(x):
    return 4 * x ** 3 if x < 0.5 else 1 - (-2 * x + 2) ** 3 / 2


# Beats, in seconds. Each starts before the previous has settled so the
# sequence reads as one move rather than four.
T_DISC = (0.00, 0.60)      # scale + fade in, centred in frame
T_RING = (0.35, 1.30)      # sweeps around the disc as if orbiting into place
T_SLIDE = (1.15, 1.80)     # mark slides left, opening the space for the type
T_MELEE = (1.60, 2.15)     # rises + fades in once the mark has cleared it
T_UNBOUND = (1.77, 2.32)   # follows, staggered
T_OUT = (3.30, 4.00)       # fade to black


# --- layer extraction -------------------------------------------------------

def top_level_groups(text):
    """Spans of the outermost <g> elements. The disc group nests a clipped
    group (the wedge fill), so a non-greedy regex would split it wrongly."""
    spans, depth, start = [], 0, None
    for m in re.finditer(r"<g\b[^>]*?(/?)>|</g>", text):
        if m.group(0).startswith("</"):
            depth -= 1
            if depth == 0:
                spans.append((start, m.end()))
        elif m.group(1) != "/":
            if depth == 0:
                start = m.start()
            depth += 1
    return spans


def layer_svgs(svg_text):
    """Split the lockup into one full-frame SVG per coloured group."""
    body = svg_text[svg_text.index(">", svg_text.index("<svg")) + 1: svg_text.rindex("</svg>")]
    body = re.sub(r"<title>.*?</title>", "", body, flags=re.S)
    body = re.sub(r"<rect[^>]*fill=\"#000000\"\s*/>", "", body, count=1)

    # Unwrap the placement <svg> to get back to the lockup's own 770x320 space.
    inner = re.search(r"<svg[^>]*viewBox=\"-?[\d.]+ -?[\d.]+[^\"]*\"[^>]*>(.*)</svg>", body, re.S)
    if inner is None:
        sys.exit("could not unwrap the lockup placement <svg>")
    content = inner.group(1)

    groups = top_level_groups(content)
    if len(groups) != len(LAYERS):
        sys.exit(f"expected {len(LAYERS)} groups in the lockup, found {len(groups)}")

    ink_x, ink_y, ink_w, ink_h = INK
    s = INK_TARGET_W / ink_w
    box_w, box_h = 770 * s, 320 * s
    x = (W - INK_TARGET_W) / 2 - ink_x * s
    y = (H - ink_h * s) / 2 - ink_y * s

    out = {}
    for i, name in enumerate(LAYERS):
        # Keep every wrapper and transform intact; blank the other three groups.
        only = content
        for j, (gs, ge) in reversed(list(enumerate(groups))):
            if j != i:
                only = only[:gs] + only[ge:]
        out[name] = (
            f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
            f'viewBox="0 0 {W} {H}">\n'
            f'<svg x="{x:.3f}" y="{y:.3f}" width="{box_w:.3f}" height="{box_h:.3f}" '
            f'viewBox="0 0 770 320">{only}</svg>\n</svg>\n'
        )
    return out


def render_layers(tmp):
    """Rasterise each layer, then crop to its ink and remember where it sat."""
    svgs = layer_svgs(SOURCE.read_text())
    layers = {}
    for name, text in svgs.items():
        svg_path = tmp / f"{name}.svg"
        png_path = tmp / f"{name}.png"
        svg_path.write_text(text)
        subprocess.run(
            ["rsvg-convert", "-w", str(W), "-h", str(H), "-o", str(png_path), str(svg_path)],
            check=True,
        )
        im = Image.open(png_path).convert("RGBA")
        bbox = im.getchannel("A").getbbox()
        if bbox is None:
            sys.exit(f"layer '{name}' rendered empty")
        layers[name] = {"img": im.crop(bbox), "pos": (bbox[0], bbox[1])}
        print(f"  {name:8s} bbox={bbox}")
    return layers


# --- the ring sweep ---------------------------------------------------------

def disc_states(layers):
    """Rebuild the disc as the clean four-quadrant mark, and keep the traced one.

    In the artwork the disc is cut where the ring crosses in front of it, so on
    its own it looks sliced. Here the disc is redrawn from its own geometry --
    circle minus the two dividing bars, measured off the artwork -- and the
    traced version is kept alongside. The ring's sweep then blends from clean
    to traced, so the slash is cut in exactly as the violet arrives over it.
    """
    disc = layers["disc"]
    dx, dy = disc["pos"]
    dw, dh = disc["img"].size
    traced_full = np.zeros((H, W), np.uint8)
    traced_full[dy:dy + dh, dx:dx + dw] = np.asarray(disc["img"].getchannel("A"))

    # Top and bottom of the disc are uncut, so its height is the diameter and
    # its left edge fixes the centre.
    rad = dh / 2.0
    cx, cy = dx + rad, dy + dh / 2.0
    yy, xx = np.mgrid[0:H, 0:W]
    inside = (xx - cx) ** 2 + (yy - cy) ** 2 <= rad ** 2
    on = traced_full > 110

    def bar(axis):
        """Columns (or rows) the artwork leaves empty inside the circle."""
        tot = inside.sum(axis=axis).astype(np.float64)
        hit = (on & inside).sum(axis=axis).astype(np.float64)
        cov = np.divide(hit, tot, out=np.ones_like(tot), where=tot > 0.7 * rad)
        idx = np.nonzero(cov < 0.15)[0]
        if idx.size == 0:
            sys.exit("could not locate the disc's dividing bars")
        # Take the longest contiguous run: the slash leaves stray low-coverage
        # columns near the rim that would otherwise stretch the bar.
        runs, start = [], idx[0]
        for a, b in zip(idx, idx[1:]):
            if b != a + 1:
                runs.append((start, a + 1))
                start = b
        runs.append((start, idx[-1] + 1))
        lo, hi = max(runs, key=lambda ab: ab[1] - ab[0])
        return float(lo), float(hi)

    vx0, vx1 = bar(0)
    hy0, hy1 = bar(1)
    print(f"  cross bars: x {vx0:.0f}-{vx1:.0f}, y {hy0:.0f}-{hy1:.0f}")

    # Redraw it, supersampled so the circle edge stays smooth.
    S, pad = 4, 2
    bx0, by0 = int(cx - rad) - pad, int(cy - rad) - pad
    side = int(2 * rad) + 2 * pad + 1
    gy, gx = np.mgrid[0:side * S, 0:side * S]
    px = bx0 + (gx + 0.5) / S
    py = by0 + (gy + 0.5) / S
    circ = (px - cx) ** 2 + (py - cy) ** 2 <= rad ** 2
    gap = ((px >= vx0) & (px < vx1)) | ((py >= hy0) & (py < hy1))
    fine = (circ & ~gap).astype(np.float32)
    clean = (fine.reshape(side, S, side, S).mean(axis=(1, 3)) * 255.0)

    disc["pos"] = (bx0, by0)
    disc["img"] = Image.new("RGBA", (side, side), (255, 255, 255, 255))
    disc["clean"] = clean
    disc["traced"] = traced_full[by0:by0 + side, bx0:bx0 + side].astype(np.float32)
    return (cx, cy)


def wedge_table(size, pos, centre, steps=256, start_deg=205.0, total_deg=378.0,
                soft_deg=7.0):
    """A wedge anchored at the disc centre, opening to `total_deg` over `steps`.

    Slightly over a full turn so the tail closes cleanly. Returned as uint8
    coverage, 0 outside the wedge and 255 inside.
    """
    w, h = size
    ox, oy = pos
    cx, cy = centre
    yy, xx = np.mgrid[0:h, 0:w]
    rel = (np.degrees(np.arctan2((yy + oy) - cy, (xx + ox) - cx)) - start_deg) % 360.0

    table = []
    for i in range(steps):
        opened = total_deg * (i / (steps - 1))
        m = np.clip((opened - rel) / soft_deg, 0.0, 1.0)
        m[rel > opened] = 0.0
        if opened >= 360.0:
            m = np.maximum(m, np.clip((opened - 360.0 - rel) / soft_deg, 0.0, 1.0))
        table.append((m * 255.0).astype(np.uint8))
    return table


def sweep_masks(ring, centre, **kw):
    """The ring, revealed by that same wedge."""
    base = np.asarray(ring["img"].getchannel("A"), dtype=np.float32)
    return [(base * (w / 255.0)).astype(np.uint8)
            for w in wedge_table(ring["img"].size, ring["pos"], centre, **kw)]


# --- audio -----------------------------------------------------------------
#
# A short regal fanfare, scored to the picture: a low drone as the mark appears,
# a timpani roll building with the ring, two brass pickups as it closes, then
# the full chord on the wordmark with timpani and a crash. Bright, ceremonial,
# over quickly -- a title sting, not a trailer cue.

CHORD = (293.66, 369.99, 440.00, 587.33)      # D F# A D
PICKUP = ((220.00, 440.00), (293.66, 587.33))  # A then D, each doubled an octave
TOP = 880.00                                   # A5, the answering note
TIMP = 73.42                                   # D2


def _slot(buf, start, dur):
    """Index window for an event, clipped to the buffer."""
    a = int(start * SR)
    b = min(len(buf), a + int(dur * SR))
    return a, b, np.arange(max(0, b - a)) / SR


def _highpass(x, cutoff):
    spec = np.fft.rfft(x)
    spec[:int(cutoff * len(x) / SR)] = 0.0
    return np.fft.irfft(spec, len(x))


def _brass(buf, f0, start, dur, amp, rng, bright=1.0):
    """Additive brass: the spectrum opens on the attack, which is what makes a
    horn read as a horn rather than a sawtooth."""
    a, b, tt = _slot(buf, start, dur + 0.35)
    if tt.size == 0:
        return
    attack = 1.0 - np.exp(-tt / 0.026)
    release = np.clip(1.0 - (tt - dur) / 0.30, 0.0, 1.0)
    env = attack * release * (0.86 + 0.14 * np.exp(-tt / 0.10))
    open_ = (1.0 - np.exp(-tt / 0.042)) * (0.78 + 0.22 * np.exp(-tt / 0.45))
    vib = 1.0 + 0.0035 * np.sin(2 * np.pi * 5.1 * tt) * np.clip((tt - 0.14) / 0.2, 0, 1)

    tone = np.zeros_like(tt)
    norm = 0.0
    for h in range(1, 15):
        level = np.exp(-(h - 1) * (1.15 - open_ * bright) * 0.55) / h
        tone += level * np.sin(2 * np.pi * f0 * h * tt * vib + rng.uniform(0, 6.28))
        norm += 1.0 / h
    buf[a:b] += amp * env * tone / norm


def _timp(buf, f0, start, amp, rng, dur=1.1):
    a, b, tt = _slot(buf, start, dur)
    if tt.size == 0:
        return
    f = f0 * (1.0 + 0.38 * np.exp(-tt / 0.045))          # the pitch drop
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) + \
        0.35 * np.sin(2 * np.pi * np.cumsum(f * 1.58) / SR) * np.exp(-tt / 0.10)
    stick = rng.standard_normal(tt.size) * np.exp(-tt / 0.010) * 0.30
    buf[a:b] += amp * (body * np.exp(-tt / (dur * 0.28)) + stick)


def _roll(buf, start, end, amp, rng):
    """Accelerating timpani roll."""
    t, lo, hi = start, 9.0, 27.0
    while t < end:
        x = (t - start) / (end - start)
        _timp(buf, TIMP, t, amp * (0.22 + 0.78 * x ** 1.6), rng, dur=0.20)
        t += 1.0 / (lo + (hi - lo) * x)


def _crash(buf, start, amp, dur, rng, cutoff=3000.0):
    a, b, tt = _slot(buf, start, dur)
    if tt.size == 0:
        return
    nz = _highpass(rng.standard_normal(tt.size), cutoff)
    nz /= np.abs(nz).max() + 1e-9
    buf[a:b] += amp * nz * np.exp(-tt / (dur * 0.34)) * (1.0 - np.exp(-tt / 0.003))


def _reverb_ir(seconds, decay, rng):
    tt = np.arange(int(seconds * SR)) / SR
    ir = rng.standard_normal(tt.size) * np.exp(-tt / decay)
    ir[:int(0.014 * SR)] = 0.0
    return ir / np.abs(ir).sum() * 0.9


def build_audio(path):
    """Render the fanfare and write it as a 16-bit stereo WAV."""
    from scipy.signal import fftconvolve

    rng = np.random.default_rng(11)           # seeded: same cue every run
    n = int(SR * DURATION)
    t = np.arange(n) / SR
    dry = np.zeros(n)
    wet = np.zeros(n)

    hit = T_MELEE[0]                          # the downbeat, on the wordmark

    # 1. The mark appears: a low drone, felt more than heard.
    a, b, tt = _slot(dry, T_DISC[0], 1.6)
    drone = np.exp(-((tt - 0.6) ** 2) / 0.34)
    dry[a:b] += 0.075 * drone * (np.sin(2 * np.pi * TIMP * tt) +
                                 0.45 * np.sin(2 * np.pi * TIMP * 2 * tt))

    # 2. The ring sweeps: the roll builds under it, and a cymbal swells in.
    _roll(dry, T_RING[0] + 0.15, hit - 0.05, 0.085, rng)
    a, b, tt = _slot(dry, hit - 0.75, 0.75)
    swell = _highpass(rng.standard_normal(b - a), 2400.0)
    swell /= np.abs(swell).max() + 1e-9
    dry[a:b] += 0.055 * swell * (tt / 0.75) ** 2.3
    wet[a:b] += 0.040 * swell * (tt / 0.75) ** 2.3

    # 3. The ring closes: two brass pickups, A then D, leaning into the hit.
    for i, notes in enumerate(PICKUP):
        when = hit - 0.30 + i * 0.15
        for j, f in enumerate(notes):
            _brass(dry, f, when, 0.12, 0.085 * (1.0 - 0.35 * j), rng, bright=0.85)
            _brass(wet, f, when, 0.12, 0.045 * (1.0 - 0.35 * j), rng, bright=0.85)
    _timp(dry, TIMP, hit - 0.30, 0.16, rng, dur=0.5)

    # 4. The wordmark: the chord lands. Brass, timpani, crash together.
    for i, f in enumerate(CHORD):
        _brass(dry, f, hit + i * 0.006, 1.05, 0.155 - 0.018 * i, rng)
        _brass(wet, f, hit + i * 0.006, 1.05, 0.080 - 0.009 * i, rng)
    _timp(dry, TIMP, hit, 0.34, rng, dur=1.3)
    _timp(dry, TIMP * 2, hit, 0.10, rng, dur=0.8)
    _crash(dry, hit, 0.075, 1.9, rng)
    _crash(wet, hit, 0.065, 2.2, rng, cutoff=2200.0)

    # 5. The answering note on UNBOUND, then the chord rings out.
    _brass(dry, TOP, T_UNBOUND[0], 0.85, 0.075, rng, bright=1.15)
    _brass(wet, TOP, T_UNBOUND[0], 0.85, 0.050, rng, bright=1.15)
    for i, f in enumerate(CHORD):
        _brass(dry, f, hit + 1.35, 1.35, 0.060 - 0.007 * i, rng, bright=0.8)
        _brass(wet, f, hit + 1.35, 1.35, 0.045 - 0.005 * i, rng, bright=0.8)

    # Space. Two decorrelated tails so it opens up in stereo.
    left = dry + 0.80 * fftconvolve(wet, _reverb_ir(1.8, 0.46, rng))[:n]
    right = dry + 0.80 * fftconvolve(wet, _reverb_ir(1.8, 0.46, rng))[:n]

    # Match the picture: silence by the time it has faded out.
    tail = np.clip((t - T_OUT[0]) / (T_OUT[1] - T_OUT[0]), 0.0, 1.0)
    stereo = np.stack([left, right], axis=1) * \
        ((np.cos(tail * np.pi / 2) ** 1.5) * np.clip(t / 0.04, 0.0, 1.0))[:, None]

    peak = np.abs(stereo).max()
    stereo *= (10 ** (-1.5 / 20)) / peak      # leave 1.5 dB of headroom
    print(f"  audio peak before normalise {peak:.3f}, "
          f"rms {np.sqrt((stereo ** 2).mean()):.4f}")

    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((stereo * 32767.0).astype("<i2").tobytes())


# --- frames -----------------------------------------------------------------

def paste(canvas, layer, alpha=1.0, scale=1.0, dx=0.0, dy=0.0, mask=None):
    if alpha <= 0.002:
        return
    im = layer["img"]
    x, y = layer["pos"]
    if mask is None:
        mask = im.getchannel("A")
    if abs(scale - 1.0) > 1e-3:
        w, h = im.size
        nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
        x += (w - nw) / 2.0
        y += (h - nh) / 2.0
        im = im.resize((nw, nh), Image.LANCZOS)
        mask = mask.resize((nw, nh), Image.LANCZOS)
    if alpha < 0.998:
        mask = mask.point(lambda v, a=alpha: int(v * a))
    canvas.paste(im, (round(x + dx), round(y + dy)), mask)


def build_frame(i, layers, ring_table, disc_table, mark_dx):
    t = i / FPS
    canvas = Image.new("RGB", (W, H), (0, 0, 0))

    # The mark opens centred in frame and only steps aside once the ring has
    # closed, so the first beat is balanced instead of hanging off to the left.
    dx = mark_dx * (1.0 - out_cubic(span(t, *T_SLIDE)))

    r = in_out_cubic(span(t, *T_RING))
    idx = min(len(ring_table) - 1, int(round(r * (len(ring_table) - 1))))

    d = out_cubic(span(t, *T_DISC))
    disc = layers["disc"]
    w = disc_table[idx].astype(np.float32) / 255.0
    shape = (disc["traced"] * w + disc["clean"] * (1.0 - w)).astype(np.uint8)
    paste(canvas, disc, alpha=d, scale=0.90 + 0.10 * d, dx=dx,
          mask=Image.fromarray(shape, mode="L"))

    if r > 0.0:
        paste(canvas, layers["ring"], dx=dx,
              mask=Image.fromarray(ring_table[idx], mode="L"))

    m = out_cubic(span(t, *T_MELEE))
    paste(canvas, layers["melee"], alpha=m, dy=16.0 * (1.0 - m))

    u = out_cubic(span(t, *T_UNBOUND))
    paste(canvas, layers["unbound"], alpha=u, dy=10.0 * (1.0 - u))

    fade = 1.0 - in_cubic(span(t, *T_OUT))
    if fade < 0.998:
        return (np.asarray(canvas, dtype=np.uint16) * int(fade * 256) >> 8).astype(np.uint8)
    return np.asarray(canvas)


# --- main -------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--out", type=pathlib.Path, default=DEFAULT_OUT)
    ap.add_argument("--frames", type=pathlib.Path,
                    help="write a numbered PNG sequence to this directory instead of an MP4")
    ap.add_argument("--audio", action="store_true",
                    help="mux the fanfare in; the boot animation ships silent")
    args = ap.parse_args()

    for tool in ("rsvg-convert",) + (() if args.frames else ("ffmpeg",)):
        if shutil.which(tool) is None:
            sys.exit(f"missing required tool: {tool}")

    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        print(f"rendering layers from {SOURCE.relative_to(ROOT)}")
        layers = render_layers(tmp)

        centre = disc_states(layers)
        ring_table = sweep_masks(layers["ring"], centre)
        disc_table = wedge_table(layers["disc"]["img"].size,
                                 layers["disc"]["pos"], centre)

        # Offset that would centre the disc+ring cluster in the frame; the
        # mark starts there and slides back to its lockup position.
        left = min(layers[n]["pos"][0] for n in ("disc", "ring"))
        right = max(layers[n]["pos"][0] + layers[n]["img"].size[0] for n in ("disc", "ring"))
        mark_dx = W / 2.0 - (left + right) / 2.0
        print(f"  disc centre {centre}, mark slide {mark_dx:.1f}px")

        if args.frames:
            args.frames.mkdir(parents=True, exist_ok=True)
            for i in range(NFRAMES):
                Image.fromarray(build_frame(i, layers, ring_table, disc_table, mark_dx)).save(
                    args.frames / f"boot_{i:04d}.png")
            print(f"wrote {NFRAMES} frames to {args.frames}")
            return

        args.out.parent.mkdir(parents=True, exist_ok=True)
        cmd = [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
        ]
        if args.audio:
            wav = tmp / "boot.wav"
            build_audio(wav)
            cmd += ["-i", str(wav), "-c:a", "aac", "-b:a", "192k", "-shortest"]
        else:
            cmd += ["-an"]
        cmd += [
            "-c:v", "libx264", "-preset", "slow", "-crf", "16",
            "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.0",
            "-movflags", "+faststart",
            str(args.out),
        ]
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        for i in range(NFRAMES):
            proc.stdin.write(build_frame(i, layers, ring_table, disc_table, mark_dx).tobytes())
        proc.stdin.close()
        if proc.wait() != 0:
            sys.exit("ffmpeg failed")
        try:
            shown = args.out.resolve().relative_to(ROOT)
        except ValueError:
            shown = args.out            # -o may point outside the repo
        print(f"wrote {shown} "
              f"({args.out.stat().st_size / 1024:.0f} KiB, {DURATION:g}s @ {FPS}fps)")


if __name__ == "__main__":
    main()
