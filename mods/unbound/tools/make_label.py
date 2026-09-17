#!/usr/bin/env python3
"""Generate a main-menu option label in the game's own texture format.

Melee's menu option labels are 176x30 IA4 textures: white fill, black
outline, transparent background.  The material tints the white to yellow at
draw time, so the source art is white -- generating yellow would double up.

Geometry measured from the retail US labels (`MnMaAll.usd`, image table at
data offset 0xc0518):

    image  label           ink x        ink y      height
    0      1-P Mode        0..164       0..26      27
    2      Trophies        35..155      1..29      29
    4      Data            62..129      2..26      25
    5      Regular Match   full width              -- 13 chars, condensed

IA4 is **one byte per pixel** (low nibble intensity, high nibble alpha), GX
tiles it in 8x4 blocks, and the height pads from 30 to 32 -- so a label is
176*32 = 5632 bytes.  Halving it as a 4bpp format is the obvious mistake and
produces a buffer overrun rather than a wrong picture.

Typeface: retail is a heavy, round grotesque.  Liberation Sans Bold has the
right letterforms (Arial metrics, double-storey 'a', round bowls) but is
lighter, so it is emboldened by dilating in supersampled space before the
downsample -- compared side by side against image 5, which is the only retail
label with the same character count as "Melee Unbound".

Usage:  make_label.py "Melee Unbound" out.ia4 [--png preview.png]
"""
import argparse

from PIL import Image, ImageDraw, ImageFilter, ImageFont

WIDTH = 176
HEIGHT = 30
TILE_W = 8
TILE_H = 4
SS = 8  # supersample factor

DEFAULT_FONT = "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"
FALLBACK_FONTS = [
    "/home/ajayanto/.local/share/fonts/Inter-Bold.ttf",
    "/usr/share/fonts/gsfonts/NimbusSans-Bold.otf",
]

TARGET_INK_H = 26  # retail caps run 25..29
MAX_INK_W = 160    # leave room for the 2px outline ring inside 176
CENTRE_X = 88      # centre of the 176-wide quad
EMBOLDEN = 3       # supersampled dilation steps; 3 matches retail weight
OUTLINE = 2        # native-pixel dilation steps for the black ring


def pick_font(path=None):
    for p in ([path] if path else []) + [DEFAULT_FONT] + FALLBACK_FONTS:
        if not p:
            continue
        try:
            ImageFont.truetype(p, 12)
            return p
        except OSError:
            continue
    raise SystemExit("no usable font found")


def render_fill(text, font_path):
    """Glyph coverage at native size, fitted to the label box.

    Long labels are condensed by shrinking rather than truncated, which is
    what the retail set does -- "Regular Match" is the same 13 characters as
    "Melee Unbound" and spans the full width.
    """
    for size in range(TARGET_INK_H * SS * 2, 8, -1):
        font = ImageFont.truetype(font_path, size)
        canvas = Image.new("L", (WIDTH * SS * 2, HEIGHT * SS * 3), 0)
        ImageDraw.Draw(canvas).text((20, 20), text, font=font, fill=255)
        box = canvas.getbbox()
        if box is None:
            continue
        glyphs = canvas.crop(box)
        for _ in range(EMBOLDEN):
            glyphs = glyphs.filter(ImageFilter.MaxFilter(3))
        glyphs = glyphs.crop(glyphs.getbbox())
        ink_w = glyphs.size[0] / SS
        ink_h = glyphs.size[1] / SS
        if ink_h <= TARGET_INK_H and ink_w <= MAX_INK_W:
            return glyphs.resize(
                (max(1, round(ink_w)), max(1, round(ink_h))), Image.LANCZOS
            )
    raise SystemExit("could not fit %r into %dx%d" % (text, WIDTH, HEIGHT))


def compose(glyphs):
    """Place the glyphs and ring them in black."""
    fill = Image.new("L", (WIDTH, HEIGHT), 0)
    w, h = glyphs.size
    fill.paste(glyphs, (round(CENTRE_X - w / 2.0), round((HEIGHT - h) / 2.0)))

    # The outline is a dilation of the glyph coverage rather than a stroke of
    # the text, so it stays even around thin joins.
    solid = fill.point(lambda v: 255 if v > 96 else 0)
    for _ in range(OUTLINE):
        solid = solid.filter(ImageFilter.MaxFilter(3))
    alpha = solid.point(lambda v: 255 if v > 0 else 0)
    return fill, alpha


def encode_ia4(inten, alpha):
    pi = inten.load()
    pa = alpha.load()
    padded_h = (HEIGHT + TILE_H - 1) // TILE_H * TILE_H
    out = bytearray()
    for by in range(0, padded_h, TILE_H):
        for bx in range(0, WIDTH, TILE_W):
            for y in range(by, by + TILE_H):
                for x in range(bx, bx + TILE_W):
                    if x < WIDTH and y < HEIGHT:
                        out.append(
                            ((pa[x, y] * 15 // 255) << 4) | (pi[x, y] * 15 // 255)
                        )
                    else:
                        out.append(0)
    assert len(out) == WIDTH * padded_h, len(out)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("text")
    ap.add_argument("out")
    ap.add_argument("--font")
    ap.add_argument("--png")
    args = ap.parse_args()

    font_path = pick_font(args.font)
    inten, alpha = compose(render_fill(args.text, font_path))
    data = encode_ia4(inten, alpha)
    with open(args.out, "wb") as f:
        f.write(data)

    box = alpha.getbbox()
    print("font  %s" % font_path)
    print("ink   x[%d..%d] y[%d..%d] %dx%d"
          % (box[0], box[2] - 1, box[1], box[3] - 1,
             box[2] - box[0], box[3] - box[1]))
    print("wrote %s (%d bytes)" % (args.out, len(data)))

    if args.png:
        Image.merge("RGBA", [inten, inten, inten, alpha]).resize(
            (WIDTH * 4, HEIGHT * 4), Image.NEAREST
        ).save(args.png)
        print("preview %s" % args.png)


if __name__ == "__main__":
    main()
