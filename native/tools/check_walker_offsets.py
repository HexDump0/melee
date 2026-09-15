#!/usr/bin/env python3
"""
P-757: cross-check the converter's hand-written field offsets against DWARF.

The 130 walkers in native/decomp/assets/hsd_convert.c transcribe offsets the
compiler already knows exactly, and a wrong one is silent -- it byte-swaps the
wrong four bytes and corrupts a struct in whatever scene happens to use it.
P-746 was exactly that.  This turns the transcription into something a test
checks.

A walker opts in with a `DWARF: <TypeName>` comment above it:

    /* DWARF: HSD_PObjDesc */
    static void conv_pobj(Conv* c, uint32_t off)

For each opted-in walker this asserts:

  * the size it bounds itself with (`in_data(c, off, HSD_POBJDESC_SIZE)`)
    equals `sizeof` the type;
  * every `conv_u32/conv_u16/rd32/rd16(c, off + N)` lands on a real field,
    with the access width matching the field's width;
  * no swapping access lands on a pointer field, since on-disc pointers are
    relocation targets and are already host order -- swapping one again is
    the P-746 shape of bug.

Accesses through a computed base (`e = off + i * 0x10`, the array walks) are
counted and reported but cannot be checked this way; so is a walker with no
annotation.  Both numbers are printed so the gate has its own denominator,
which is the point of ADR-0023.

**Bit-fields are deliberately not checked.**  The native build's DWARF records
GCC's allocation, which puts the first bit-field at the LSB; MWCC puts it at
the MSB, so the disc's bit numbering is the mirror image (G-180/G-181/G-188).
The byte offset is still right, so an access landing *on* a bit-field storage
unit is accepted, but this tool will never be the thing that says which bit is
which -- `check_unk_flag_bit_order` and the `PORT_BF_BE` sites own that.

    check_walker_offsets.py LAYOUT.json hsd_convert.c [MIN_WALKERS]

`MIN_WALKERS` is a ratchet in the same spirit as `MELEE_COVERAGE_FLOOR`: the
number of cross-checked walkers may go up and must never go down, so a walker
cannot silently lose its annotation.  Raise it when annotations land; never
lower it to make a change pass.  `--list-unannotated` prints the walkers that
still have none, which is the worklist.
"""

import json
import re
import sys

FUNC_RE = re.compile(
    r"^static\s+void\s+(conv_\w+)\s*\(\s*Conv\*\s*c\s*,\s*uint32_t\s+(\w+)")
DEFINE_RE = re.compile(r"^#define\s+([A-Z][A-Z_0-9]*)\s+(0x[0-9a-fA-F]+|\d+)")
ANNOT_RE = re.compile(r"DWARF:\s*([A-Za-z_]\w*)(\s+partial)?")

WIDTH = {"conv_u32": 4, "conv_u16": 2, "rd32": 4, "rd16": 2}
SWAPPERS = ("conv_u32", "conv_u16")


def parse_source(path):
    """Split hsd_convert.c into {defines}, [walkers]."""
    lines = open(path).read().split("\n")
    defines = {}
    walkers = []
    pending_type = None
    cur = None
    depth = 0
    for i, line in enumerate(lines):
        m = DEFINE_RE.match(line)
        if m:
            defines[m.group(1)] = int(m.group(2), 0)

        if cur is None:
            m = FUNC_RE.match(line)
            # The forward-declaration block at the top of the file matches the
            # same shape; a declaration ends in `;` and has no body, and
            # treating one as a definition swallows the next real function.
            if m and line.rstrip().endswith(";"):
                m = None
            if m:
                cur = {"name": m.group(1), "base": m.group(2),
                       "type": pending_type[0] if pending_type else None,
                       "partial": bool(pending_type and pending_type[1]),
                       "line": i + 1, "body": []}
                pending_type = None
                depth = 0
            else:
                a = ANNOT_RE.search(line)
                # Only a comment directly above the function counts, so a
                # mention of DWARF in prose does not silently bind a walker.
                if a and line.lstrip().startswith(("/*", "*", "//")):
                    pending_type = (a.group(1), a.group(2) is not None)
                elif line.strip() == "" or line.startswith("}"):
                    pending_type = pending_type
                continue
        if cur is not None:
            cur["body"].append(line)
            depth += line.count("{") - line.count("}")
            if depth <= 0 and len(cur["body"]) > 1 and line.startswith("}"):
                walkers.append(cur)
                cur = None
    if cur is not None:
        walkers.append(cur)
    return defines, walkers


def accesses(walker):
    """(helper, offset) for every access through the walker's own base."""
    base = re.escape(walker["base"])
    pat = re.compile(
        r"\b(conv_u32|conv_u16|rd32|rd16)\s*\(\s*c\s*,\s*" + base +
        r"\s*(?:\+\s*(0x[0-9a-fA-F]+|\d+)\s*)?\)")
    other = re.compile(r"\b(conv_u32|conv_u16|rd32|rd16)\s*\(\s*c\s*,")
    body = "\n".join(walker["body"])
    direct = [(m.group(1), int(m.group(2), 0) if m.group(2) else 0)
              for m in pat.finditer(body)]
    return direct, len(other.findall(body)) - len(direct)


def declared_size(walker, defines):
    base = re.escape(walker["base"])
    m = re.search(r"in_data\s*\(\s*c\s*,\s*" + base + r"\s*,\s*([A-Za-z_0-9]+)\s*\)",
                  "\n".join(walker["body"]))
    if not m:
        return None, None
    token = m.group(1)
    if token in defines:
        return defines[token], token
    try:
        return int(token, 0), token
    except ValueError:
        return None, token


def field_at(layout, off, width):
    """(status, field) for an access of `width` bytes at `off`."""
    exact = [f for f in layout["fields"] if f["offset"] == off]
    for f in exact:
        if f["bits"] is not None:
            return "bitfield", f
        if f["size"] == width:
            return "ok", f
    # Inside an array or a nested aggregate: legitimate, since the walker is
    # converting elements the flattened view does not name individually.
    for f in layout["fields"]:
        size = f["size"] or 0
        if f["offset"] <= off < f["offset"] + size and \
                off + width <= f["offset"] + size and \
                f["kind"] in ("array", "aggregate"):
            return "inside", f
    if exact:
        return "width", exact[0]
    return "missing", None


def main(argv):
    listing = "--list-unannotated" in argv
    argv = [a for a in argv if a != "--list-unannotated"]
    if len(argv) not in (3, 4):
        sys.stderr.write(__doc__)
        return 2
    floor = int(argv[3]) if len(argv) == 4 else 0
    layouts = json.load(open(argv[1]))
    defines, walkers = parse_source(argv[2])

    errors = []
    checked_walkers = 0
    checked_offsets = 0
    inside = 0
    bitfields = 0
    computed = 0
    partials = 0
    unannotated = []
    unknown_type = []

    for w in walkers:
        direct, indirect = accesses(w)
        computed += max(indirect, 0)
        if w["type"] is None:
            unannotated.append(w["name"])
            continue
        layout = layouts.get(w["type"])
        if layout is None:
            unknown_type.append("%s -> %s" % (w["name"], w["type"]))
            continue
        checked_walkers += 1

        size, token = declared_size(w, defines)
        if size is not None and size > layout["size"]:
            errors.append(
                "%s:%d %s bounds itself with %s = 0x%x, past the end of %s "
                "(0x%x)" % (argv[2], w["line"], w["name"], token, size,
                            w["type"], layout["size"]))
        elif size is not None and size < layout["size"] and not w["partial"]:
            # A short bound is safe but usually means the walker or the
            # struct moved.  `DWARF: <Type> partial` is how a walker that
            # deliberately stops early (a trailing byte stream, a union
            # variant) says so, so the shortfall stays a decision on the
            # record rather than a silent one.
            errors.append(
                "%s:%d %s bounds itself with %s = 0x%x but sizeof(%s) is "
                "0x%x; say `DWARF: %s partial` if that is deliberate"
                % (argv[2], w["line"], w["name"], token, size, w["type"],
                   layout["size"], w["type"]))
        elif size is not None and size < layout["size"]:
            partials += 1

        for helper, off in direct:
            width = WIDTH[helper]
            if off + width > layout["size"]:
                errors.append(
                    "%s:%d %s reads %d bytes at +0x%x, past the end of %s "
                    "(0x%x)" % (argv[2], w["line"], w["name"], width, off,
                                w["type"], layout["size"]))
                continue
            status, field = field_at(layout, off, width)
            if status == "ok":
                checked_offsets += 1
                if helper in SWAPPERS and field["kind"] == "pointer":
                    errors.append(
                        "%s:%d %s byte-swaps +0x%x (%s.%s), which is a "
                        "pointer -- relocation targets are already host order"
                        % (argv[2], w["line"], w["name"], off, w["type"],
                           field["name"]))
            elif status == "inside":
                inside += 1
            elif status == "bitfield":
                bitfields += 1
            elif status == "width":
                errors.append(
                    "%s:%d %s reads %d bytes at +0x%x but %s.%s is %s bytes"
                    % (argv[2], w["line"], w["name"], width, off, w["type"],
                       field["name"], field["size"]))
            else:
                errors.append(
                    "%s:%d %s converts +0x%x, which is not a field of %s"
                    % (argv[2], w["line"], w["name"], off, w["type"]))

    for e in errors:
        sys.stderr.write("dwarf_check: %s\n" % e)

    print("dwarf_check: %d/%d walkers cross-checked, %d offsets exact, "
          "%d inside arrays, %d bit-field units"
          % (checked_walkers, len(walkers), checked_offsets, inside,
             bitfields))
    print("dwarf_check: %d walkers unannotated, %d accesses through a "
          "computed base, %d declared partial, %d annotations with no DWARF "
          "type" % (len(unannotated), computed, partials, len(unknown_type)))
    for u in unknown_type:
        sys.stderr.write("dwarf_check: no DWARF for %s\n" % u)
    if listing:
        for name in unannotated:
            print("dwarf_check: unannotated %s" % name)
    if unknown_type:
        print("dwarf_check: FAIL (%d annotations name a type with no DWARF; "
              "add its header to native/tools/dwarf_types.c)"
              % len(unknown_type))
        return 1
    if errors:
        print("dwarf_check: FAIL (%d)" % len(errors))
        return 1
    if checked_walkers < floor:
        print("dwarf_check: FAIL (%d walkers cross-checked, floor is %d -- "
              "an annotation was removed; the floor may only be raised)"
              % (checked_walkers, floor))
        return 1
    print("dwarf_check: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
