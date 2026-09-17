#!/usr/bin/env python3
"""
P-757: read `type -> size, field offsets` out of the port's own debug info.

The converter's 130 walkers hand-transcribe field offsets that the compiler
already knows exactly.  This reads them back out of DWARF so the transcription
can be checked instead of trusted (`check_walker_offsets.py` does the checking;
this file only produces the facts).

Input is object files from the native build, which compiles `src/` with
`-m32 -malign-double` and the layout options in native/CMakeLists.txt -- so
the layouts here are the ones the running port actually uses, which is what
the converter has to agree with.

Output is JSON:

    { "HSD_PObjDesc": { "size": 24,
                        "fields": [ {"name": "flags", "offset": 12,
                                     "size": 2, "kind": "int",
                                     "bits": null }, ... ] } }

Bit-fields carry `bits` and are *not* offsets a walker may convert -- MWCC
allocates the first bit-field at the MSB and GCC at the LSB (G-180/G-181),
so the byte offset here is right but the bit numbering is not the disc's.
Anonymous struct/union members are flattened into the parent at their own
offsets, so a walker converting through one is still checkable.

    dwarf_layout.py OUT.json OBJ [OBJ...]
"""

import json
import re
import subprocess
import sys

DIE_RE = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>:\s+Abbrev Number:\s+\d+\s+\(DW_TAG_(\w+)\)")
ATTR_RE = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$")
REF_RE = re.compile(r"<0x([0-9a-f]+)>")

# Tags whose DIEs we keep.  Everything else (subprograms, variables, and the
# location lists that make up most of the dump) is dropped as it is read.
KEEP = {
    "structure_type", "union_type", "member", "typedef", "base_type",
    "pointer_type", "array_type", "const_type", "volatile_type",
    "enumeration_type", "subrange_type",
}


def attr_value(raw):
    """objdump renders strings as `(indirect string, offset: 0x..): name`."""
    if raw.startswith("("):
        end = raw.find("): ")
        if end != -1:
            return raw[end + 3:].strip()
    return raw.strip()


def attr_int(raw):
    raw = raw.strip()
    # DW_AT_data_member_location is sometimes a location expression:
    #   `2 byte block: 23 4 	(DW_OP_plus_uconst: 4)`
    m = re.search(r"DW_OP_plus_uconst:\s*(\d+)", raw)
    if m:
        return int(m.group(1))
    m = re.match(r"(0x[0-9a-fA-F]+|\d+)", raw)
    if not m:
        return None
    return int(m.group(1), 0)


def read_dies(obj):
    """Parse one object file's .debug_info into {offset: die}."""
    proc = subprocess.Popen(
        ["objdump", "--dwarf=info", obj],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        universal_newlines=True)
    dies = {}
    stack = []
    cur = None
    for line in proc.stdout:
        m = DIE_RE.match(line)
        if m:
            depth = int(m.group(1))
            off = int(m.group(2), 16)
            tag = m.group(3)
            del stack[depth:]
            if tag not in KEEP:
                cur = None
                stack.append(None)
                continue
            cur = {"offset": off, "tag": tag, "name": None, "size": None,
                   "type": None, "loc": None, "bits": None, "upper": None,
                   "children": []}
            dies[off] = cur
            parent = stack[depth - 1] if depth > 0 and len(stack) >= depth else None
            if parent is not None:
                parent["children"].append(cur)
            stack.append(cur)
            continue
        if cur is None:
            continue
        m = ATTR_RE.match(line)
        if not m:
            continue
        name, raw = m.group(1), m.group(2)
        if name == "DW_AT_name":
            cur["name"] = attr_value(raw)
        elif name == "DW_AT_byte_size":
            cur["size"] = attr_int(raw)
        elif name == "DW_AT_data_member_location":
            cur["loc"] = attr_int(raw)
        elif name == "DW_AT_bit_size":
            cur["bits"] = attr_int(raw)
        elif name == "DW_AT_upper_bound":
            cur["upper"] = attr_int(raw)
        elif name == "DW_AT_count":
            # GCC emits one or the other depending on the array form.
            n = attr_int(raw)
            cur["upper"] = None if n is None else n - 1
        elif name == "DW_AT_type":
            ref = REF_RE.search(raw)
            if ref:
                cur["type"] = int(ref.group(1), 16)
    proc.stdout.close()
    proc.wait()
    return dies


def strip_type(dies, off):
    """Follow typedef/const/volatile to the type that carries the layout."""
    seen = set()
    while off is not None and off in dies and off not in seen:
        seen.add(off)
        die = dies[off]
        if die["tag"] in ("typedef", "const_type", "volatile_type"):
            off = die["type"]
            continue
        return die
    return None


def type_kind(die):
    if die is None:
        return "unknown"
    tag = die["tag"]
    if tag == "pointer_type":
        return "pointer"
    if tag in ("structure_type", "union_type"):
        return "aggregate"
    if tag == "array_type":
        return "array"
    if tag == "enumeration_type":
        return "int"
    if tag == "base_type":
        return "float" if "float" in (die["name"] or "") or \
                          "double" in (die["name"] or "") else "int"
    return "unknown"


def type_size(dies, die):
    if die is None:
        return None
    if die["size"] is not None:
        return die["size"]
    if die["tag"] == "pointer_type":
        return 4          # the port is 32-bit; so is the disc data
    if die["tag"] == "array_type":
        elem = strip_type(dies, die["type"])
        count = None
        for child in die["children"]:
            if child["tag"] == "subrange_type":
                count = child.get("upper")
        esize = type_size(dies, elem)
        if esize is not None and count is not None:
            return esize * (count + 1)
    return None


def flatten(dies, die, base, out, depth=0):
    """Members of `die` appended to `out`, anonymous aggregates inlined."""
    if depth > 8:
        return
    for m in die["children"]:
        if m["tag"] != "member":
            continue
        off = base + (m["loc"] or 0)
        target = strip_type(dies, m["type"])
        if m["name"] is None and target is not None and \
                target["tag"] in ("structure_type", "union_type"):
            flatten(dies, target, off, out, depth + 1)
            continue
        out.append({
            "name": m["name"] or "<anon>",
            "offset": off,
            "size": type_size(dies, target),
            "kind": type_kind(target),
            "bits": m["bits"],
        })
        # A named union or struct member is itself a place a walker may
        # convert through, so record its interior too -- at its real offsets.
        if target is not None and target["tag"] in ("structure_type",
                                                    "union_type"):
            flatten(dies, target, off, out, depth + 1)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    out_path = argv[1]
    layouts = {}
    for obj in argv[2:]:
        dies = read_dies(obj)
        # Most of these types are `typedef struct _HSD_TObjDesc { ... }
        # HSD_TObjDesc;`, and the tag is what DWARF records.  Index the
        # typedef name too, since that is the name source and comments use.
        aliases = {}
        for die in dies.values():
            if die["tag"] != "typedef" or die["name"] is None:
                continue
            target = strip_type(dies, die["type"])
            if target is not None and \
                    target["tag"] in ("structure_type", "union_type"):
                # Keyed by DIE offset, not tag name: plenty of these types are
                # `typedef struct { ... } ItHurtBoneList;` with no tag at all,
                # and the typedef name is the only name they have.
                aliases.setdefault(target["offset"], set()).add(die["name"])
        for die in dies.values():
            if die["tag"] not in ("structure_type", "union_type"):
                continue
            names = set(aliases.get(die["offset"], ()))
            if die["name"] is not None:
                names.add(die["name"])
            if not names or die["size"] is None:
                continue
            fields = []
            flatten(dies, die, 0, fields)
            if not fields:
                continue
            entry = {"size": die["size"], "fields": fields}
            for name in sorted(names):
                prev = layouts.get(name)
                if prev is None:
                    layouts[name] = entry
                elif prev["size"] != entry["size"]:
                    # Two translation units disagreeing about a layout is
                    # itself a finding -- say so rather than picking one.
                    sys.stderr.write(
                        "dwarf_layout: %s is %d bytes in one TU and %d in "
                        "another\n" % (name, prev["size"], entry["size"]))
                    return 1
                elif len(entry["fields"]) > len(prev["fields"]):
                    layouts[name] = entry
    with open(out_path, "w") as f:
        json.dump(layouts, f, indent=1, sort_keys=True)
    sys.stderr.write("dwarf_layout: %d types from %d objects -> %s\n"
                     % (len(layouts), len(argv) - 2, out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
