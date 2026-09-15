"""cb_control_rig.rigcore — pure-Python core for the Community Behaviors control-rig importer.

NO dependencies: no `bpy`, no PyYAML. This module parses a CB `control/rig.yaml`, composes
the LOCAL node poses into WORLD transforms (forward kinematics), and scans a load order for
available rigs. The Blender addon (`__init__.py`) imports this and turns the flat world-pose
list into an armature; everything here is engine-agnostic and unit-testable headlessly.

The `rig.yaml` contract (emitted by `havok-core-cli control-decompile`):
  nodes:
    - name: "<node>"
      transform: { translation: [x,y,z], rotation: [x,y,z,w], scale: [x,y,z] }   # LOCAL, or the string `derive`
      poseSource: skeleton | nif | derived            # present when posed
      poseHint:   nif | aux | convention              # present when transform is `derive`
      handles: [ { name, variant }, ... ]             # optional bound objects
      children: [ <node>, ... ]                        # recursive

A `transform: derive` node has no known pose; we treat its LOCAL as identity so it sits on its
parent (visible + tagged) rather than vanishing.
"""

from __future__ import annotations
import math
import os

# ─────────────────────────────────────────────────────────────────────────────
# Dependency-free YAML reader (the exact subset the emitter produces)
#
# Prefers PyYAML when present (e.g. running headless in CPython); falls back to a
# focused parser for Blender's bundled Python, which ships no PyYAML. The fallback
# is validated byte-for-byte against PyYAML over the whole vanilla bestiary in
# test_rigcore.py, so the two agree on every shape the emitter can produce.
# ─────────────────────────────────────────────────────────────────────────────

def load_rig(path):
    with open(path, "r", encoding="utf-8") as fh:
        return parse_rig_yaml(fh.read())


def parse_rig_yaml(text):
    try:
        import yaml  # type: ignore
        return yaml.safe_load(text)
    except Exception:
        return _mini_parse(text)


def _strip_comment(line):
    """Drop an inline/# full-line comment that is outside quotes and flow brackets."""
    out, i, n = [], 0, len(line)
    inq = None
    while i < n:
        c = line[i]
        if inq:
            out.append(c)
            if c == inq:
                inq = None
        elif c in ("'", '"'):
            inq = c
            out.append(c)
        elif c == "#" and (i == 0 or line[i - 1] in " \t"):
            break
        else:
            out.append(c)
        i += 1
    return "".join(out).rstrip()


def _scalar(tok):
    tok = tok.strip()
    if tok == "" or tok == "~" or tok == "null":
        return None
    if tok == "true":
        return True
    if tok == "false":
        return False
    if len(tok) >= 2 and tok[0] in ("'", '"') and tok[-1] == tok[0]:
        return tok[1:-1]
    try:
        return int(tok)
    except ValueError:
        pass
    try:
        return float(tok)
    except ValueError:
        pass
    return tok


def _parse_flow(s):
    """Parse a flow scalar / `{...}` map / `[...]` seq. Returns (value, rest_index)."""
    val, idx = _flow_value(s, 0)
    return val


def _flow_value(s, i):
    while i < len(s) and s[i] in " \t":
        i += 1
    if i >= len(s):
        return None, i
    c = s[i]
    if c == "{":
        d, i = {}, i + 1
        while True:
            while i < len(s) and s[i] in " \t,":
                i += 1
            if i < len(s) and s[i] == "}":
                return d, i + 1
            key, i = _flow_token(s, i)
            while i < len(s) and s[i] in " \t":
                i += 1
            if i < len(s) and s[i] == ":":
                i += 1
            v, i = _flow_value(s, i)
            d[str(key)] = v
    if c == "[":
        a, i = [], i + 1
        while True:
            while i < len(s) and s[i] in " \t,":
                i += 1
            if i < len(s) and s[i] == "]":
                return a, i + 1
            v, i = _flow_value(s, i)
            a.append(v)
    return _flow_token(s, i)


def _flow_token(s, i):
    while i < len(s) and s[i] in " \t":
        i += 1
    if i < len(s) and s[i] in ("'", '"'):
        q = s[i]
        j = i + 1
        while j < len(s) and s[j] != q:
            j += 1
        return s[i + 1:j], j + 1
    j = i
    while j < len(s) and s[j] not in ",:{}[]":
        j += 1
    return _scalar(s[i:j]), j


def _mini_parse(text):
    rows = []
    for raw in text.splitlines():
        line = _strip_comment(raw)
        if line.strip() == "":
            continue
        indent = len(line) - len(line.lstrip(" "))
        rows.append((indent, line.strip()))
    val, _ = _parse_block(rows, 0, -1)
    return val


def _parse_block(rows, i, parent_indent):
    """Parse the block starting at rows[i], whose indent > parent_indent. Returns (value, next_i)."""
    if i >= len(rows):
        return None, i
    indent = rows[i][0]
    if rows[i][1].startswith("- "):
        seq = []
        while i < len(rows) and rows[i][0] == indent and rows[i][1].startswith("- "):
            inner = rows[i][1][2:]
            if inner and (inner[0] in "{[" or (":" not in inner)):
                seq.append(_parse_flow(inner))
                i += 1
            else:
                # Sequence item is a mapping whose first key shares the dash line;
                # continuation keys sit at column indent+2.
                item_rows = [(indent + 2, inner)]
                i += 1
                while i < len(rows) and rows[i][0] > indent:
                    item_rows.append(rows[i])
                    i += 1
                v, _ = _parse_block(item_rows, 0, indent)
                seq.append(v)
        return seq, i
    # Block mapping.
    d = {}
    while i < len(rows) and rows[i][0] == indent:
        line = rows[i][1]
        key, _, rest = line.partition(":")
        key = key.strip()
        rest = rest.strip()
        if rest:
            d[key] = _parse_flow(rest)
            i += 1
        else:
            i += 1
            if i < len(rows) and rows[i][0] > indent:
                v, i = _parse_block(rows, i, indent)
                d[key] = v
            else:
                d[key] = None
    return d, i


# ─────────────────────────────────────────────────────────────────────────────
# Quaternion / vector math (mirrors havok-core sct::SkeletonMath)
# quats are [x, y, z, w]; vectors [x, y, z]
# ─────────────────────────────────────────────────────────────────────────────

IDENTITY = {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]}


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ]


def qconj(q):
    return [-q[0], -q[1], -q[2], q[3]]


def qrot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    # t = 2 * cross(q.xyz, v)
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    # v + w*t + cross(q.xyz, t)
    return [
        vx + w * tx + (y * tz - z * ty),
        vy + w * ty + (z * tx - x * tz),
        vz + w * tz + (x * ty - y * tx),
    ]


def _compose(parent, local):
    """world = parent ∘ local, both {translation, rotation, scale}."""
    ps, pr, pt = parent["scale"], parent["rotation"], parent["translation"]
    ls, lr, lt = local["scale"], local["rotation"], local["translation"]
    scaled = [lt[0] * ps[0], lt[1] * ps[1], lt[2] * ps[2]]
    rotated = qrot(pr, scaled)
    return {
        "translation": [pt[0] + rotated[0], pt[1] + rotated[1], pt[2] + rotated[2]],
        "rotation": qmul(pr, lr),
        "scale": [ps[0] * ls[0], ps[1] * ls[1], ps[2] * ls[2]],
    }


def _world_to_local(parent, world):
    """Inverse of _compose: local = parent^-1 ∘ world. Exact inverse for round-trip."""
    ps, pr, pt = parent["scale"], parent["rotation"], parent["translation"]
    inv_pr = qconj(pr)
    dp = [world["translation"][0] - pt[0], world["translation"][1] - pt[1], world["translation"][2] - pt[2]]
    un = qrot(inv_pr, dp)
    return {
        "translation": [un[0] / ps[0], un[1] / ps[1], un[2] / ps[2]],
        "rotation": qmul(inv_pr, world["rotation"]),
        "scale": [world["scale"][0] / ps[0], world["scale"][1] / ps[1], world["scale"][2] / ps[2]],
    }


def _local_of(node):
    t = node.get("transform")
    if not isinstance(t, dict):
        return dict(IDENTITY)  # `derive` (unposed) → identity, sits on parent
    return {
        "translation": [float(x) for x in t.get("translation", [0, 0, 0])],
        "rotation": [float(x) for x in t.get("rotation", [0, 0, 0, 1])],
        "scale": [float(x) for x in t.get("scale", [1, 1, 1])],
    }


# ─────────────────────────────────────────────────────────────────────────────
# Public API
# ─────────────────────────────────────────────────────────────────────────────

class RigNode:
    __slots__ = ("name", "parent", "world", "local", "posed", "source", "handles", "depth")

    def __init__(self, name, parent, world, local, posed, source, handles, depth):
        self.name = name
        self.parent = parent          # parent name, or None for a root
        self.world = world            # {translation, rotation, scale}
        self.local = local
        self.posed = posed            # True if it carried a real transform
        self.source = source          # 'skeleton'|'nif'|'derived'|'hint:convention'|...
        self.handles = handles        # [{name, variant}, ...]
        self.depth = depth


def flatten(rig):
    """rig (parsed dict) → flat list of RigNode with WORLD transforms in hierarchy order."""
    out = []

    def walk(node, parent_world, parent_name, depth):
        local = _local_of(node)
        world = _compose(parent_world, local)
        posed = isinstance(node.get("transform"), dict)
        if posed:
            source = str(node.get("poseSource", "?"))
        else:
            source = "hint:" + str(node.get("poseHint", "?"))
        out.append(RigNode(
            name=str(node.get("name", "<unnamed>")),
            parent=parent_name,
            world=world,
            local=local,
            posed=posed,
            source=source,
            handles=node.get("handles") or [],
            depth=depth,
        ))
        for child in (node.get("children") or []):
            walk(child, world, str(node.get("name", "")), depth + 1)

    for root in (rig.get("nodes") or []):
        walk(root, IDENTITY, None, 0)
    return out


def rig_stats(rig):
    return {
        "nodeCount": rig.get("nodeCount"),
        "posedFromSkeleton": rig.get("posedFromSkeleton"),
        "posedFromNif": rig.get("posedFromNif"),
        "derivedByConvention": rig.get("derivedByConvention"),
        "poseSourceCounts": rig.get("poseSourceCounts"),
        "deriveHintCounts": rig.get("deriveHintCounts"),
    }


def actor_label_from_path(path):
    """Derive a human actor label from a .../actors/<...>/control/rig.yaml path (or a flattened
    cb-diffs dir name like 'canine_character_assets_wolf_skeleton.hkx')."""
    p = path.replace("\\", "/")
    parts = p.split("/")
    if "actors" in parts:
        i = parts.index("actors")
        tail = parts[i + 1:]
        # drop the trailing 'character assets*/control/rig.yaml' noise
        keep = []
        for seg in tail:
            low = seg.lower()
            if low.startswith("character assets") or low in ("characterassets", "control", "rig.yaml", "_1stperson"):
                continue
            keep.append(seg)
        if keep:
            return "/".join(keep)
    # cb-diffs flattened form: '<a>_<b>_..._skeleton.hkx/control/rig.yaml'
    for seg in parts:
        if seg.endswith(".hkx"):
            base = seg[:-4]
            for junk in ("_character_assets", "_characterassets", "_skeleton", "skeleton"):
                base = base.replace(junk, " ")
            return " ".join(base.split()).strip() or seg
    return os.path.basename(os.path.dirname(os.path.dirname(path)))


# ─────────────────────────────────────────────────────────────────────────────
# Export: WORLD-pose entries (from a Blender armature) → control/rig.yaml
# The bpy layer supplies each bone's armature-space (WORLD) rest transform + parent name;
# this inverse-FK's to LOCAL and serializes CB's rig.yaml. The exact inverse of the import
# path, so import→export round-trips (see test_rigcore).
# ─────────────────────────────────────────────────────────────────────────────

def worlds_to_rig(entries, source="authored-blender", pose_source="authored"):
    """entries: ordered list of {name, parent, world:{translation,rotation,scale}} (parents first).
    Returns a rig dict {source, nodeCount, nodes:[nested]} with LOCAL transforms."""
    worlds = {e["name"]: e["world"] for e in entries}
    nodes = {}
    roots = []
    for e in entries:
        pw = worlds[e["parent"]] if e.get("parent") and e["parent"] in worlds else IDENTITY
        local = _world_to_local(pw, e["world"])
        nodes[e["name"]] = {
            "name": e["name"],
            "transform": {
                "translation": local["translation"],
                "rotation": local["rotation"],
                "scale": local["scale"],
            },
            "poseSource": pose_source,
            "children": [],
        }
    for e in entries:
        p = e.get("parent")
        if p and p in nodes:
            nodes[p]["children"].append(nodes[e["name"]])
        else:
            roots.append(nodes[e["name"]])
    return {"source": source, "nodeCount": len(entries), "nodes": roots}


def _fmt(x):
    x = float(x)
    if x == 0.0:
        return "0"
    s = repr(round(x, 6))
    return s[:-2] if s.endswith(".0") else s


def _fmt_vec(v):
    return "[" + ", ".join(_fmt(x) for x in v) + "]"


def emit_rig_yaml(rig):
    """Serialize a rig dict (from worlds_to_rig) to CB rig.yaml text."""
    out = [
        "# Control rig authored in Blender, exported by cb_control_rig.",
        "# Node hierarchy + names + LOCAL transforms (translation, rotation quat [x,y,z,w], scale).",
        "# poseSource: authored. Consumed by the CB scene editor / control-rig pipeline.",
        f"source: {rig.get('source', 'authored-blender')}",
        f"nodeCount: {rig.get('nodeCount', 0)}",
        "nodes:",
    ]

    def emit_node(n, indent):
        pad = " " * indent
        out.append(f'{pad}- name: "{n["name"]}"')
        t = n["transform"]
        out.append(
            f"{pad}  transform: {{ translation: {_fmt_vec(t['translation'])}, "
            f"rotation: {_fmt_vec(t['rotation'])}, scale: {_fmt_vec(t['scale'])} }}"
        )
        out.append(f"{pad}  poseSource: {n.get('poseSource', 'authored')}")
        kids = n.get("children") or []
        if kids:
            out.append(f"{pad}  children:")
            for c in kids:
                emit_node(c, indent + 4)

    for root in rig.get("nodes", []):
        emit_node(root, 2)
    return "\n".join(out) + "\n"


def scan_load_order(root):
    """Find every control/rig.yaml under `root`. Returns [(label, path), ...] sorted by label.
    Works against a deployed load order, a single mod, or the cb-diffs scattershot tree."""
    found = []
    for dirpath, _dirs, files in os.walk(root):
        if "rig.yaml" in files and os.path.basename(dirpath).lower() == "control":
            path = os.path.join(dirpath, "rig.yaml")
            found.append((actor_label_from_path(path), path))
    # de-dupe by label (a later mod overriding the same actor keeps the last seen)
    seen = {}
    for label, path in found:
        seen[label] = path
    return sorted(seen.items(), key=lambda kv: kv[0].lower())
