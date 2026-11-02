"""Schema coverage check for hk_2010.2.0-r1 64-bit packfiles, independent of CB's reader.

The served-bytes gate re-serializes the source through the same schema it reads with, so a member the
schema drops (empty/skip/pad over real data, an untyped pointer) vanishes on both sides and passes.
This walks the packfile's OWN fixup tables -- the file's ground truth for where every pointer and object
is -- and takes only field offsets from schema/. It reports:

  stray-fixup    a pointer fixup whose source lands on a slot the schema does not read as a pointer
                 (empty: ptr, skip, pad, a scalar) -- the pointer is dropped on serve
  empty-array    an `empty: array` slot with a non-zero count
  unknown-class  an object whose class has no schema descriptor
  size           a descriptor whose field walk disagrees with its stated size

Usage:  py -3 -I schema_coverage.py <schemaDir> <file-or-dir>...   (exit 1 on any finding)
"""
import os
import struct
import sys

import yaml

SCALAR = {"bool": 1, "byte": 1, "int8": 1, "int16": 2, "uint16": 2, "half": 2,
          "int32": 4, "uint32": 4, "float": 4, "int": 4, "int64": 8, "uint64": 8, "ulong": 8}
ARRAYS = {"structarray", "ptrarray", "stringarray", "vec4array", "qstransformarray", "scalararray", "recordarray"}


def load_schema(root):
    classes = {}
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x != "templates"]
        for f in files:
            if not f.endswith(".yaml"):
                continue
            with open(os.path.join(d, f), encoding="utf-8") as fh:
                doc = yaml.safe_load(fh)
            if isinstance(doc, dict) and "kind" not in doc and "name" in doc and "fields" in doc:
                classes[doc["name"]] = doc
    return classes


class Layout:
    """Flattened per-class layout: (offset, width, kind, label, extra) with the parent chain first."""

    def __init__(self, classes):
        self.classes = classes
        self.cache = {}
        self.size_errors = []

    def size(self, name):
        return self.get(name)[1]

    def get(self, name):
        if name in self.cache:
            return self.cache[name]
        self.cache[name] = ([], 0)  # cycle guard
        c = self.classes[name]
        slots, cur = ([], 0)
        if c.get("parent"):
            ps, cur = self.get(c["parent"])
            slots = list(ps)
        for f in c.get("fields") or []:
            label = f"{name}.{f.get('name', '?')}"
            n = int(f.get("count", 1)) if f.get("type") not in ("boolarray",) else 1
            if "vtable" in f:
                slots.append((cur, 8, "vtable", label, None)); cur += 8
            elif "pad" in f:
                a = int(f["pad"]); w = (-cur) % a
                slots.append((cur, w, "dead", f"{name}.<pad>", None)); cur += w
            elif "skip" in f:
                w = int(f["skip"]); slots.append((cur, w, "dead", f"{name}.<skip>", None)); cur += w
            elif "empty" in f:
                if f["empty"] == "ptr":
                    slots.append((cur, 8, "dead", f"{name}.<empty ptr {f.get('name', '')}>".replace(" >", ">"), None)); cur += 8
                else:
                    slots.append((cur, 16, "emptyarray", label, None)); cur += 16
            else:
                t = f["type"]
                if t in SCALAR:
                    w = SCALAR[t] * n; slots.append((cur, w, "dead", label, None)); cur += w
                elif t in ("vector4", "quaternion"):
                    slots.append((cur, 16 * n, "dead", label, None)); cur += 16 * n
                elif t == "qstransform":
                    slots.append((cur, 48 * n, "dead", label, None)); cur += 48 * n
                elif t == "boolarray":
                    w = int(f["count"]); slots.append((cur, w, "dead", label, None)); cur += w
                elif t in ("ptr", "string", "cstring", "stringptr"):
                    for k in range(n):
                        slots.append((cur, 8, "ptr", label, None)); cur += 8
                elif t in ARRAYS:
                    slots.append((cur, 16, "array", label, f)); cur += 16
                elif t == "struct":
                    inner, w = self.get(f["ref"])
                    for (o, iw, k, l, x) in inner:
                        slots.append((cur + o, iw, k, f"{label}.{l.split('.', 1)[1]}", x))
                    cur += w
                else:
                    raise ValueError(f"{label}: unknown field type {t}")
        if c.get("size") is not None and int(c["size"]) != cur:
            self.size_errors.append(f"size {name}: fields walk to {cur}, descriptor says {c['size']}")
        self.cache[name] = (slots, cur)
        return self.cache[name]


def elem_layout(layout, f):
    t = f["type"]
    if t == "structarray":
        return layout.get(f["ref"])
    if t in ("ptrarray", "stringarray"):
        return ([(0, 8, "ptr", f"{f['name']}[]", None)], 8)
    w = {"vec4array": 16, "qstransformarray": 48}.get(t) or SCALAR.get(f.get("scalar"), 0)
    return ([(0, w, "dead", f"{f['name']}[]", None)], w)


def read_packfile(data):
    if data[:8] != b"\x57\xe0\xe0\x57\x10\xc0\xc0\x10":
        raise ValueError("not a packfile")
    if data[0x10] != 8:
        raise ValueError("not a 64-bit packfile")
    nsec = struct.unpack_from("<i", data, 0x14)[0]
    secs = {}
    for i in range(nsec):
        h = 0x40 + i * 0x30
        tag = data[h:h + 19].split(b"\0")[0].decode()
        start, lf, gf, vf, ex, im, end = struct.unpack_from("<7I", data, h + 0x14)
        secs[i] = dict(tag=tag, start=start, lf=lf, gf=gf, vf=vf, ex=ex, end=end)
    return secs


def check_file(path, layout):
    data = open(path, "rb").read()
    secs = read_packfile(data)
    cls_sec = next(s for s in secs.values() if s["tag"] == "__classnames__")
    di, ds = next((i, s) for i, s in secs.items() if s["tag"] == "__data__")
    base = ds["start"]

    def triples(a, b):
        out = []
        for o in range(base + a, base + b - 11, 12):
            t = struct.unpack_from("<3I", data, o)
            if t[0] != 0xFFFFFFFF:
                out.append(t)
        return out

    local = {}
    for o in range(base + ds["lf"], base + ds["gf"] - 7, 8):
        s, d = struct.unpack_from("<2I", data, o)
        if s != 0xFFFFFFFF:
            local[s] = d
    fixups = set(local)
    fixups.update(s for s, _, _ in triples(ds["gf"], ds["vf"]))
    objects = [(s, data[cls_sec["start"] + n:].split(b"\0")[0].decode()) for s, _, n in triples(ds["vf"], ds["ex"])]

    findings, claimed, dead = [], set(), []

    def walk(at, slots, owner, depth=0):
        for (o, w, kind, label, f) in slots:
            a = at + o
            if kind == "ptr":
                claimed.add(a)
            elif kind == "dead" and w:
                dead.append((a, a + w, owner, label))
            elif kind == "emptyarray":
                n = struct.unpack_from("<i", data, base + a + 8)[0]
                if n:
                    findings.append(f"empty-array  {owner} {label}: count {n}")
                if a in fixups:
                    claimed.add(a)
            elif kind == "array":
                claimed.add(a)
                n = struct.unpack_from("<i", data, base + a + 8)[0]
                if n and a in local and depth < 32:
                    es, ew = elem_layout(layout, f)
                    for i in range(n):
                        walk(local[a] + i * ew, es, owner, depth + 1)

    objects.sort()
    for k, (at, cname) in enumerate(objects):
        if cname not in layout.classes:
            findings.append(f"unknown-class  {cname} @0x{at:x}")
            nxt = objects[k + 1][0] if k + 1 < len(objects) else ds["lf"]
            claimed.update(x for x in fixups if at <= x < nxt)  # no layout: do not blame its pointers
            continue
        walk(at, layout.get(cname)[0], f"{cname}@0x{at:x}")

    dead.sort()
    import bisect
    starts = [d[0] for d in dead]
    for s in sorted(fixups - claimed):
        i = bisect.bisect_right(starts, s) - 1
        if i >= 0 and dead[i][0] <= s < dead[i][1]:
            d = local.get(s)
            findings.append(f"stray-fixup  {dead[i][2]} {dead[i][3]} +0x{s - dead[i][0]:x}"
                            + (f" -> 0x{d:x}" if d is not None else " (global)"))
    return findings


def main(argv):
    if len(argv) < 3:
        print(__doc__); return 2
    layout = Layout(load_schema(argv[1]))
    for name in layout.classes:
        layout.get(name)
    bad = 0
    for e in layout.size_errors:
        print(e); bad += 1
    files = []
    for p in argv[2:]:
        if os.path.isdir(p):
            files += [os.path.join(d, f) for d, _, fs in os.walk(p) for f in fs if f.lower().endswith(".hkx")]
        else:
            files.append(p)
    scanned = 0
    for p in sorted(files):
        try:
            res = check_file(p, layout)
        except ValueError as e:
            continue
        scanned += 1
        for r in res:
            print(f"{p}: {r}"); bad += 1
    print(f"{scanned} packfiles scanned, {bad} findings", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
