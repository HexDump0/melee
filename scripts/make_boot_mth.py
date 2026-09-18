#!/usr/bin/env python3
"""Pack a video into a Melee ``.mth`` movie the game's own player can stream.

The port links no video decoder and is not getting one (see
``native/AI/reference/branding.md``).  It does not need one: Melee already
ships an MTH player (``lbmthp.c``) and the port already has a working THP
video decoder (``native/decomp/thp_dec.c``, P-685).  An MTH file is a 0x40
byte header followed by a chain of JPEG frames, so the cheapest way to put a
new movie on screen natively is to *write one*, not to decode MP4 at runtime.

    python3 scripts/make_boot_mth.py            # mp4 -> mods/unbound/files/MvUnbound.mth

Needs ffmpeg (for the decode and the baseline-JPEG encode) and nothing else.
The output is committed, so this script only runs when the source animation
changes.

The format, read out of ``MvOpen.mth`` and cross-checked against
``lbmthp.c``'s ``fn_8001EB14`` / ``fn_8001ECF4``:

    0x00  "MTHP"
    0x04  0x00080000        fixed; every retail .mth carries it
    0x08  version           2
    0x0C  buf_size          player's per-frame buffer stride (32 of them)
    0x10  x_size            640
    0x14  y_size            480
    0x18  frame_rate        30
    0x1C  num_frames
    0x20  first_frame       offset of block 0, i.e. 0x40
    0x24  frame_offsets     0; the player warns and ignores any other value
    0x28  first_frame_size  size of block 0
    0x2C  zero to 0x40

Each block is ``u32 be size-of-the-NEXT-block`` followed by that frame's
JPEG, zero-padded to a multiple of 32.  The player walks the chain: it reads
``ALIGN_32(currPackedSize)`` bytes into a buffer of stride ``buf_size``, then
takes the next size from the first word of what it just read.  Two
consequences the writer has to respect:

  * ``buf_size`` must be at least the largest block, or a long frame reads
    past the end of its buffer and into the next one.  Retail cuts this fine
    (MvOpen.mth's buf_size is 32 bytes under its largest block); we do not.
  * the last block's "next" field wraps to the first block's size, which is
    what makes ``loop=1`` work.  Retail does this and so do we, even though
    the Unbound intro plays once.

The JPEG itself must be 4:2:0 baseline: ``thp_dec.c`` rejects anything whose
luma sampling factor is not 0x22 with the chroma planes at 0x11.  That is
exactly what ffmpeg's mjpeg encoder emits for ``-pix_fmt yuvj420p``.

**THP entropy data is not byte-stuffed, and that is the whole difference
between a JPEG and a THP frame.**  A standard JPEG escapes every 0xFF in the
scan as ``FF 00`` so a decoder can find markers; THP does not, and
``thp_dec.c``'s bit reader indexes the scan bytes directly
(``reader->data[bit_position >> 3]``) with no unescaping anywhere.  Measured
on the disc: MvOpen.mth's first frames carry 241, 507 and 648 *raw* 0xFF
bytes in their scans and no ``FF 00`` pairs at all.  Feed the same decoder a
correctly stuffed JPEG and every frame with enough contrast to produce an
0xFF desynchronises one bit-run in and ``THPVideoDecode`` returns -1 -- which
``lbmthp.c`` then hands to ``THPDec_80331340`` as a state pointer.  So the
packer strips the stuffing back out, which is what makes the output a THP
frame rather than a JPEG that looks like one.
"""

import argparse
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_IN = ROOT / "assets" / "melee-unbound-boot.mp4"
DEFAULT_OUT = ROOT / "mods" / "unbound" / "files" / "MvUnbound.mth"

MAGIC = b"MTHP"
HEADER_SIZE = 0x40
VERSION = 2
# 0x04 is the same in every retail .mth on the disc; copied rather than
# guessed at, because nothing in lbmthp.c reads it.
FIELD_04 = 0x00080000

WIDTH, HEIGHT = 640, 480
FPS = 30


def align32(value):
    return (value + 31) & ~31


def extract_jpegs(source, work, width, height, fps, quality):
    """Decode `source` and re-encode it as one baseline JPEG per frame."""
    pattern = str(work / "f%05d.jpg")
    cmd = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(source),
        # The movie plane is a 640x480 quad, so a 16:9 source is letterboxed
        # rather than squashed -- the mark is a circle and has to stay one.
        # The bars cost nothing: the animation is flat black to its edges.
        "-vf",
        "scale=%d:%d:flags=lanczos:force_original_aspect_ratio=decrease,"
        "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:black,fps=%d"
        % (width, height, width, height, fps),
        "-pix_fmt", "yuvj420p",
        "-c:v", "mjpeg",
        # The standard Huffman tables, which is what every retail .mth
        # carries (DHT segments of 31/181/31/181 bytes).  ffmpeg's default is
        # `optimal`, whose per-frame tables can hold a single symbol -- legal
        # JPEG that the port's decoder returns -1 for, and -1 is then
        # dereferenced as a state pointer by THPDec_80331340.  Matching
        # retail's tables is both the fix and the stronger claim.
        "-huffman", "default",
        "-q:v", str(quality),
        "-an",
        pattern,
    ]
    subprocess.run(cmd, check=True)
    frames = sorted(work.glob("f*.jpg"))
    if not frames:
        raise SystemExit("make_boot_mth: ffmpeg produced no frames")
    return [f.read_bytes() for f in frames]


def check_baseline_420(jpeg, index):
    """Assert what thp_dec.c asserts, here rather than on the player's frame."""
    tables = set()
    pos = 2
    while pos + 4 <= len(jpeg):
        if jpeg[pos] != 0xFF:
            raise SystemExit("make_boot_mth: frame %d is not JPEG" % index)
        marker = jpeg[pos + 1]
        length = struct.unpack_from(">H", jpeg, pos + 2)[0]
        if marker == 0xC4:  # DHT; one segment may carry several tables
            p = pos + 4
            end = pos + 2 + length
            while p < end:
                tables.add(jpeg[p])
                p += 1 + 16 + sum(jpeg[p + 1:p + 17])
        if marker == 0xC0:  # SOF0, baseline
            components = jpeg[pos + 9]
            if components != 3:
                raise SystemExit(
                    "make_boot_mth: frame %d has %d components, need 3"
                    % (index, components))
            sampling = [jpeg[pos + 11 + i * 3] for i in range(3)]
            if sampling != [0x22, 0x11, 0x11]:
                raise SystemExit(
                    "make_boot_mth: frame %d sampling %s, need 4:2:0 "
                    "(thp_dec.c:166)" % (index, sampling))
            return tables
        if marker in (0xC2, 0xC1, 0xC3):
            raise SystemExit(
                "make_boot_mth: frame %d is not a baseline JPEG" % index)
        pos += 2 + length
    raise SystemExit("make_boot_mth: frame %d has no SOF0" % index)


def unstuff(jpeg):
    """Undo JPEG byte stuffing in the scan, which THP does not use.

    The entropy data runs from the end of the SOS header to the EOI marker.
    Finding that marker is done *before* unstuffing, while `FF D9` can still
    only be the real EOI -- afterwards a raw 0xFF in the scan may be followed
    by anything.
    """
    pos = 2
    while True:
        marker = jpeg[pos + 1]
        length = struct.unpack_from(">H", jpeg, pos + 2)[0]
        pos += 2 + length
        if marker == 0xDA:
            break
    eoi = jpeg.rfind(b"\xff\xd9")
    if eoi < pos:
        raise SystemExit("make_boot_mth: no EOI after the scan")
    scan = jpeg[pos:eoi]
    return jpeg[:pos] + scan.replace(b"\xff\x00", b"\xff") + jpeg[eoi:]


def pack(frames):
    """Blocks, then the header that describes them. Sizes come first because
    each block stores the *next* block's size."""
    sizes = [align32(4 + len(jpeg)) for jpeg in frames]
    blocks = []
    for i, jpeg in enumerate(frames):
        nxt = sizes[(i + 1) % len(sizes)]
        block = bytearray(sizes[i])
        struct.pack_into(">I", block, 0, nxt)
        block[4:4 + len(jpeg)] = jpeg
        blocks.append(bytes(block))

    header = bytearray(HEADER_SIZE)
    header[0:4] = MAGIC
    struct.pack_into(">IIIIIIIIII", header, 4,
                     FIELD_04,
                     VERSION,
                     max(sizes),        # buf_size: >= the largest block
                     WIDTH,
                     HEIGHT,
                     FPS,
                     len(frames),
                     HEADER_SIZE,       # first_frame
                     0,                 # frame_offsets: unsupported, must be 0
                     sizes[0])          # first_frame_size
    return bytes(header) + b"".join(blocks)


def verify(data):
    """Walk the file the way the player does and land exactly on the end."""
    num = struct.unpack_from(">I", data, 0x1C)[0]
    buf_size = struct.unpack_from(">I", data, 0x0C)[0]
    off = struct.unpack_from(">I", data, 0x20)[0]
    size = struct.unpack_from(">I", data, 0x28)[0]
    first = size
    for i in range(num):
        if size % 32:
            raise SystemExit("make_boot_mth: block %d is not 32-aligned" % i)
        if size > buf_size:
            raise SystemExit(
                "make_boot_mth: block %d (%d) exceeds buf_size %d"
                % (i, size, buf_size))
        if data[off + 4:off + 6] != b"\xff\xd8":
            raise SystemExit("make_boot_mth: block %d has no SOI" % i)
        nxt = struct.unpack_from(">I", data, off)[0]
        off += size
        size = nxt
    if off != len(data):
        raise SystemExit(
            "make_boot_mth: chain ends at 0x%x, file is 0x%x"
            % (off, len(data)))
    if size != first:
        raise SystemExit("make_boot_mth: last block does not wrap to the first")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", type=pathlib.Path, default=DEFAULT_IN)
    ap.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    ap.add_argument("--fps", type=int, default=FPS,
                    help="frame rate to resample to (default 30, as MvOpen)")
    ap.add_argument("--quality", type=int, default=3,
                    help="ffmpeg -q:v, 2 (best) to 31 (worst)")
    args = ap.parse_args()

    if not args.input.exists():
        raise SystemExit("make_boot_mth: no such input: %s" % args.input)

    with tempfile.TemporaryDirectory(prefix="mth-") as tmp:
        frames = extract_jpegs(args.input, pathlib.Path(tmp), WIDTH, HEIGHT,
                               args.fps, args.quality)
    for i, jpeg in enumerate(frames):
        tables = check_baseline_420(jpeg, i)
        # DC0/AC0 for luma and DC1/AC1 for chroma, the four the scan header
        # selects.  A frame that defines fewer is legal JPEG and still fails
        # to decode (see --huffman above).
        if tables != {0x00, 0x01, 0x10, 0x11}:
            raise SystemExit(
                "make_boot_mth: frame %d defines Huffman tables %s, need "
                "DC0/DC1/AC0/AC1" % (i, sorted(tables)))

    frames = [unstuff(jpeg) for jpeg in frames]
    data = pack(frames)
    verify(data)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    largest = struct.unpack_from(">I", data, 0x0C)[0]
    print("make_boot_mth: %s" % args.out)
    print("  %d frames at %dx%d, %d fps, %.1f s"
          % (len(frames), WIDTH, HEIGHT, args.fps, len(frames) / args.fps))
    print("  %.2f MiB, largest block %d B, player heap %.2f MiB"
          % (len(data) / (1024 * 1024), largest,
             (32 * largest + WIDTH * HEIGHT * 3 // 2) / (1024 * 1024)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
