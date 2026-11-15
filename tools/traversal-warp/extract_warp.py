"""Traversal warp data: GASP's montage notifies + the sequence FBX's `attach` bone, as curves in the retargeted clips.

GASP's traversal montages hold the warp windows (MotionWarping notify states: FrontLedge / BackLedge / BackFloor), the
collision window, the blend-out notify and the montage's end; the warp point is the mannequin's `attach` bone, a fixed
point of the clip's world at the front ledge. The Skyrim retarget kept none of it. This reads them from the GASP export
and writes them into each clip unit's `curves:` map, in the clip's own space and game units:

  WarpFrontLedge, WarpBackLedge, WarpBackFloor   step: the k-th window of that target holds k, 0 outside
  WarpFrontLedgeRotation                         step: the FrontLedge window's rotation time multiplier (0 = none)
  TraversalCollisionOff                          step: 1 inside GASP's DisableCollision window
  TraversalBlendOut                              step: from the blend-out notify, 1 always / 2 on movement input / 3 if falling
  TraversalBlendOutTime                          constant: the blend-out's seconds
  TraversalEnd                                   constant: the montage's end (the rest of the clip is never played)
  MovingTraversal                                step: 1 over the montage (GASP's own curve)
  LedgeForward, LedgeHeight                      constant: the front ledge (the attach marker) in clip space
  LedgeDistance                                  linear: forward distance from the root to the ledge, per frame

The scale from GASP's centimetres to the clip's units is fitted per clip from the two root tracks (the retarget's).

usage:
  py -3 -I extract_warp.py --gasp "<UE AS Export staging>/Game" --bundle <unpacked bundle> [--json out.json]
                           [--apply --backup <dir>]
Without --apply it only reports. --json writes the curves as an inject config ({"targets": [...]}) for a bundle that
is assembled from source. Re-running replaces the curves it owns and keeps every other curve.
"""

import argparse
import bisect
import json
import os
import re
import shutil
import sys

TICKS = 46186158000.0
OWNED = ("WarpFrontLedge", "WarpBackLedge", "WarpBackFloor", "WarpFrontLedgeRotation", "TraversalCollisionOff",
         "TraversalBlendOut", "TraversalBlendOutTime", "TraversalEnd", "MovingTraversal", "LedgeForward", "LedgeHeight",
         "LedgeDistance")
BLEND_CONDITION = {"": 1, "NewEnumerator0": 1, "NewEnumerator1": 2, "NewEnumerator2": 3}
TRAVERSAL_DIR = "Characters/UEFN_Mannequin/Animations/Traversal"
BUNDLE_DIR = "meshes/actors/character/animations/GASP/Traversal"


def fbx_tracks(path, bones):
    """Each bone's local translation curves {axis: (times, values)} from an ASCII FBX."""
    t = open(path, encoding="utf-8", errors="replace").read()
    nodes = {m.group(1): m.group(2) for m in re.finditer(r'AnimationCurveNode: (\d+), "AnimCurveNode::(\w+)"', t)}
    curves = {}
    for m in re.finditer(r'AnimationCurve: (\d+), "AnimCurve::", "" \{(.*?)\n\t\}', t, re.S):
        kt = re.search(r"KeyTime: \*\d+ \{\s*a: ([^}]*)\}", m.group(2), re.S)
        kv = re.search(r"KeyValueFloat: \*\d+ \{\s*a: ([^}]*)\}", m.group(2), re.S)
        if kt and kv:
            curves[m.group(1)] = ([int(x) / TICKS for x in kt.group(1).replace("\n", "").split(",")],
                                  [float(x) for x in kv.group(1).replace("\n", "").split(",")])
    models = {m.group(2): m.group(1) for m in re.finditer(r'Model: (\d+), "Model::([^"]+)"', t)}
    conn = re.findall(r'C: "OP",(\d+),(\d+), "([^"]+)"', t)
    out = {}
    for b in bones:
        if b not in models:
            continue
        for a, owner, prop in conn:
            if owner == models[b] and a in nodes and prop == "Lcl Translation":
                out[b] = {p[-1]: curves[c] for c, n, p in conn if n == a and c in curves}
    return out


def sample(track, axis, time):
    if axis not in track:
        return 0.0
    ts, vs = track[axis]
    if len(ts) == 1:
        return vs[0]
    i = bisect.bisect_left(ts, time)
    if i <= 0:
        return vs[0]
    if i >= len(ts):
        return vs[-1]
    f = (time - ts[i - 1]) / (ts[i] - ts[i - 1])
    return vs[i - 1] + (vs[i] - vs[i - 1]) * f


def montage(path):
    """The montage's sequence, end and notifies: [(name, start, end, props)]."""
    t = open(path, encoding="utf-8", errors="replace").read()
    props, stack = {}, []
    for line in t.splitlines():
        s = line.strip()
        m = re.match(r'Begin Object (?:Class=\S+ )?Name="([^"]+)"', s)
        if m:
            stack.append(m.group(1))
            continue
        if s == "End Object":
            if stack:
                stack.pop()
            continue
        if stack and "=" in s:
            for k in stack:
                props.setdefault(k, []).append(s)
    seq = re.search(r"AnimReference=\"[^']*'([^']+)'", t).group(1).split(".")[0]
    end = float(re.search(r"AnimEndTime=([0-9.]+)", t).group(1))
    notifies = []
    for m in re.finditer(r"^\s*Notifies\(\d+\)=\((.*)\)\s*$", t, re.M):
        body = re.sub(r'Linked(Sequence|Montage)="[^"]*"', "", m.group(1))
        name = re.search(r'NotifyName="([^"]+)"', body).group(1)
        cls = re.search(r"NotifyStateClass=\"[^']*'([^']+)'", body)
        dur = float(re.search(r"Duration=([0-9.]+)", body).group(1)) if "Duration=" in body else 0.0
        outer = re.sub(r"EndLink=\([^)]*\)", "", body)
        start = re.search(r"LinkValue=([0-9.]+)", outer)
        st = float(start.group(1)) if start else 0.0
        p = {}
        for line in props.get(cls.group(1), []) if cls else []:
            k, _, v = line.partition("=")
            p.setdefault(k, v.strip('"'))
        notifies.append((name, st, st + dur, p))
    return seq, end, notifies


def clip_root(path):
    """The clip unit's duration, frame time and [Root] track [(time, (x, y, z))]."""
    dur = dt = None
    track, inroot, mode, t = [], False, None, None
    num = r"[-+0-9.eE]+"
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if dur is None and line.startswith("  duration:"):
                dur = float(line.split(":")[1])
            elif dt is None and line.startswith("  frameDuration:"):
                dt = float(line.split(":")[1])
            elif line.startswith("    - bone:"):
                if inroot:
                    break
                inroot = "[Root]" in line
            elif inroot:
                s = line.strip()
                if s in ("translation:", "rotation:", "scale:") or s.startswith("scale"):
                    mode = "t" if s == "translation:" else None
                elif s.startswith("- time:"):
                    t = float(s.split(":")[1])
                elif s.startswith("value:") and mode == "t":
                    track.append((t, tuple(float(x) for x in re.findall(num, s.split(":", 1)[1]))))
    return dur, dt, track


def at(track, time):
    ts = [p[0] for p in track]
    i = bisect.bisect_left(ts, time)
    if i <= 0:
        return track[0][1]
    if i >= len(track):
        return track[-1][1]
    (t0, a), (t1, b) = track[i - 1], track[i]
    f = (time - t0) / (t1 - t0)
    return tuple(a[k] + (b[k] - a[k]) * f for k in range(3))


def step(windows, value_of):
    """CONSTANT keys: 0 from time 0, each window's value inside it, 0 after (adjacent windows share an edge)."""
    keys = [[0.0, 0.0, "CONSTANT"]]
    for i, (s, e) in enumerate(windows):
        if keys[-1][0] == s:
            keys[-1][1] = value_of(i)
        else:
            keys.append([s, value_of(i), "CONSTANT"])
        keys.append([e, 0.0, "CONSTANT"])
    if len(keys) > 1 and keys[0][0] == keys[1][0]:
        keys.pop(0)
    return keys


def simplify(points, tol):
    """Linear keys within `tol` of `points` [(t, v)] (Douglas-Peucker)."""
    if len(points) <= 2:
        return points
    (t0, v0), (t1, v1) = points[0], points[-1]
    worst, wi = 0.0, 0
    for i in range(1, len(points) - 1):
        t, v = points[i]
        d = abs(v - (v0 + (v1 - v0) * (t - t0) / (t1 - t0)))
        if d > worst:
            worst, wi = d, i
    if worst <= tol:
        return [points[0], points[-1]]
    return simplify(points[: wi + 1], tol)[:-1] + simplify(points[wi:], tol)


def analyse(gasp, bundle, montage_path, report):
    seq, end, notifies = montage(montage_path)
    rel = seq.split("/Animations/Traversal/", 1)[1]
    fbx = os.path.join(gasp, seq.replace("/Game/", "", 1) + ".FBX")
    clip = os.path.join(bundle, BUNDLE_DIR, rel + ".hkx")
    if not os.path.exists(clip):
        report.append(f"skip {rel}: no clip unit in the bundle")
        return None
    if not os.path.exists(fbx):
        report.append(f"skip {rel}: no FBX")
        return None
    tracks = fbx_tracks(fbx, ["root", "attach"])
    root, attach = tracks.get("root", {}), tracks.get("attach", {})
    dur, dt, track = clip_root(clip)

    # The retarget's scale: GASP's forward is -Y, up Z; the clip's forward y, up z.
    def g_root(t):
        return (sample(root, "X", t), -sample(root, "Y", t), sample(root, "Z", t))

    g0, c0 = g_root(0.0), track[0][1]
    num = den = 0.0
    for t, c in track:
        g = g_root(t)
        for k in (1, 2):
            num += (c[k] - c0[k]) * (g[k] - g0[k])
            den += (g[k] - g0[k]) ** 2
    scale = num / den if den > 0 else 0.0

    windows = {"FrontLedge": [], "BackLedge": [], "BackFloor": []}
    rotation, collision, blend = {}, [], None
    for name, s, e, p in notifies:
        if name == "MotionWarping" and p.get("WarpTargetName") in windows:
            target = p["WarpTargetName"]
            windows[target].append((s, e))
            if target == "FrontLedge":
                rot = 0.0 if p.get("bWarpRotation") == "False" else float(p.get("WarpRotationTimeMultiplier", "1"))
                rotation[(s, e)] = rot
            if p.get("WarpPointAnimBoneName", "attach" if target == "FrontLedge" else "") not in ("attach", ""):
                report.append(f"note {rel}: {target} warps bone {p['WarpPointAnimBoneName']}, read as the root")
        elif name == "PlayMontageNotifyWindow" and p.get("NotifyName") == "DisableCollision":
            collision.append((s, e))
        elif "BlendOut" in name:
            blend = (s, BLEND_CONDITION.get(p.get("BlendOutCondition", ""), 1), float(p.get("BlendOutTime", "0.3")))
    # Back-to-back windows meet exactly (GASP's are authored a frame-fraction apart), times rounded to the microsecond.
    for key in windows:
        w = sorted((round(s, 6), round(e, 6), rotation.get((s, e), 0.0)) for s, e in windows[key])
        for i in range(1, len(w)):
            if 0.0 < w[i][0] - w[i - 1][1] < 0.01:
                w[i] = (w[i - 1][1], w[i][1], w[i][2])
        if key == "FrontLedge":
            front_rotation = [r for _, _, r in w]
        windows[key] = [(s, e) for s, e, _ in w]
    collision = [(round(s, 6), round(e, 6)) for s, e in collision]
    if blend:
        blend = (round(blend[0], 6), blend[1], blend[2])
    end = round(end, 6)

    # The ledge marker (root + attach, a fixed point) at the last FrontLedge window's end, in the clip's own space.
    te = windows["FrontLedge"][-1][1] if windows["FrontLedge"] else 0.0
    off_fwd = -(sample(attach, "Y", te))
    off_up = sample(attach, "Z", te)
    croot = at(track, te)
    marker = (croot[0], croot[1] + off_fwd * scale, croot[2] + off_up * scale)

    curves = {}
    for target in ("FrontLedge", "BackLedge", "BackFloor"):
        if windows[target]:
            curves["Warp" + target] = step(windows[target], lambda i: float(i + 1))
    if windows["FrontLedge"]:
        curves["WarpFrontLedgeRotation"] = step(windows["FrontLedge"], lambda i: front_rotation[i])
    if collision:
        curves["TraversalCollisionOff"] = step(collision, lambda i: 1.0)
    if blend:
        curves["TraversalBlendOut"] = [[0.0, 0.0, "CONSTANT"], [blend[0], float(blend[1]), "CONSTANT"]]
        curves["TraversalBlendOutTime"] = [[0.0, blend[2], "CONSTANT"]]
    curves["TraversalEnd"] = [[0.0, end, "CONSTANT"]]
    curves["MovingTraversal"] = step([(0.0, end)], lambda i: 1.0)
    curves["LedgeForward"] = [[0.0, round(marker[1], 3), "CONSTANT"]]
    curves["LedgeHeight"] = [[0.0, round(marker[2], 3), "CONSTANT"]]
    curves["LedgeDistance"] = [[round(t, 6), round(v, 3)] for t, v in simplify([(t, marker[1] - c[1]) for t, c in track], 0.5)]

    path = f"{BUNDLE_DIR}/{rel}.hkx"
    report.append(f"{rel}: scale {scale:.4f}, clip {dur:.2f}s / montage end {end:.2f}s, ledge fwd {marker[1]:.1f} up "
                  f"{marker[2] - track[0][1][2]:.1f} from the clip's start, windows "
                  + ", ".join(f"{k} {len(v)}" for k, v in windows.items() if v)
                  + (f", collision {len(collision)}" if collision else "")
                  + (f", blend-out {blend[0]:.2f}s cond {blend[1]}" if blend else ", NO blend-out"))
    if end > dur + 1e-3:
        report.append(f"warn {rel}: montage end {end:.3f} past the clip's {dur:.3f}")
    if not 0.6 < scale < 0.9:
        report.append(f"warn {rel}: scale {scale:.4f} out of the expected 0.74 band")
    return path, clip, curves


def fmt(v):
    return f"{v:.9g}"


def write_curves(clip, curves):
    """Replaces this tool's curves in the unit's `curves:` map (keeping every other curve), appending its own."""
    lines = open(clip, encoding="utf-8", newline="").read().split("\n")
    try:
        start = lines.index("  curves:")
    except ValueError:
        raise RuntimeError(f"{clip}: no curves map")
    stop = start + 1
    while stop < len(lines) and (lines[stop].startswith("    ") or lines[stop] == ""):
        stop += 1
    kept, i, body = [], start + 1, lines[start + 1:stop]
    blocks, cur = [], None
    for line in body:
        m = re.match(r"^    '([^']+)':$", line)
        if m:
            cur = [m.group(1), [line]]
            blocks.append(cur)
        elif cur:
            cur[1].append(line)
    for name, block in blocks:
        if name not in OWNED:
            kept.extend(block)
    for name, keys in curves.items():
        kept.append(f"    '{name}':")
        kept.append("      keys:")
        for k in keys:
            kept.append(f"        - time: {fmt(k[0])}")
            kept.append(f"          value: {fmt(k[1])}")
            kept.append(f"          interp: {k[2] if len(k) > 2 else 'LINEAR'}")
    out = lines[: start + 1] + kept + lines[stop:]
    open(clip, "w", encoding="utf-8", newline="").write("\n".join(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gasp", required=True)
    ap.add_argument("--bundle", required=True)
    ap.add_argument("--json")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--backup")
    a = ap.parse_args()
    if a.apply and not a.backup:
        sys.exit("--apply needs --backup")
    report, by_path = [], {}
    root = os.path.join(a.gasp, TRAVERSAL_DIR)
    for dp, _, fn in sorted(os.walk(root)):
        for f in sorted(fn):
            if f.startswith("AM_") and f.endswith(".T3D"):
                r = analyse(a.gasp, a.bundle, os.path.join(dp, f), report)
                if not r:
                    continue
                # Some montages play another montage's sequence (a Relaxed montage over a Neutral clip, a test one):
                # the montage named for the sequence states it.
                own = f[3:-4] == os.path.basename(r[0])[:-4]
                held = by_path.get(r[0])
                if held and held[1] and not own:
                    report.append(f"note {r[0]}: {f} plays it too; kept the montage named for it")
                    continue
                if held and not own:
                    report.append(f"note {r[0]}: two montages not named for it; kept the first")
                    continue
                by_path[r[0]] = (r, own)
    results = [v[0] for v in by_path.values()]
    have = {os.path.normcase(os.path.normpath(c)) for _, c, _ in results}
    for dp, _, fn in sorted(os.walk(os.path.join(a.bundle, BUNDLE_DIR))):
        for f in sorted(fn):
            if f.endswith(".hkx") and os.path.normcase(os.path.normpath(os.path.join(dp, f))) not in have:
                report.append(f"none {os.path.relpath(os.path.join(dp, f), a.bundle)}: no GASP montage plays it")
    print("\n".join(report))
    print(f"{len(results)} clips")
    if a.json:
        os.makedirs(os.path.dirname(os.path.abspath(a.json)), exist_ok=True)
        targets = [{"path": p, "field": "curves", "values": {n: k for n, k in c.items()}} for p, _, c in results]
        with open(a.json, "w", encoding="utf-8", newline="\n") as fh:
            json.dump({"targets": targets}, fh, indent=1)
    if a.apply:
        for path, clip, curves in results:
            dst = os.path.join(a.backup, path)
            if not os.path.exists(dst):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(clip, dst)
            write_curves(clip, curves)
        print(f"applied to {len(results)} clips; originals in {a.backup}")


if __name__ == "__main__":
    main()
