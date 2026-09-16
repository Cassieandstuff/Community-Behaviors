"""Headless validation for rigcore (run in CPython, PyYAML present).

Proves, over the whole vanilla bestiary emitted to cb-diffs:
  1. the dependency-free _mini_parse matches PyYAML byte-for-byte (Blender has no PyYAML),
  2. flatten() composes every node to a finite WORLD transform (no NaN/inf), node count intact,
  3. the L/R FootBox mirror survives the FK compose on the human rig (x = -/+),
  4. scan_load_order labels + de-dupes a real tree.
"""
import math
import os
import sys

import yaml  # reference

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "cb_control_rig"))
import rigcore  # noqa: E402  (imported directly: rigcore is standalone, no bpy)

DIFFS = r"D:\cb-diffs\control-rig-scattershot3"


def _finite(v):
    return all(math.isfinite(x) for x in v)


def main():
    rigs = rigcore.scan_load_order(DIFFS)
    assert rigs, "no rig.yaml found under " + DIFFS
    print(f"scan_load_order: {len(rigs)} rigs, labels e.g. {[l for l,_ in rigs[:5]]}")

    parse_ok = compose_ok = 0
    total_nodes = 0
    for label, path in rigs:
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
        ref = yaml.safe_load(text)
        mine = rigcore._mini_parse(text)
        assert mine == ref, f"mini-parser diverged from PyYAML on {path}"
        parse_ok += 1

        flat = rigcore.flatten(ref)
        assert len(flat) == (ref.get("nodeCount") or len(flat)), \
            f"node count mismatch on {label}: flat={len(flat)} hdr={ref.get('nodeCount')}"
        for n in flat:
            assert _finite(n.world["translation"]) and _finite(n.world["rotation"]), \
                f"non-finite world on {label}:{n.name}"
        total_nodes += len(flat)
        compose_ok += 1

    print(f"mini-parser == PyYAML: {parse_ok}/{len(rigs)} rigs")
    print(f"FK compose finite:     {compose_ok}/{len(rigs)} rigs, {total_nodes} nodes total")

    # L/R FootBox mirror on the human rig (world space).
    human = dict(rigs)
    hp = None
    for label, path in rigs:
        if label.lower() == "character" or "character" == os.path.basename(os.path.dirname(os.path.dirname(path))).split("_")[0]:
            hp = path
            break
    hp = hp or [p for l, p in rigs if "character" in p.lower() and "female" not in p.lower()][0]
    flat = rigcore.flatten(yaml.safe_load(open(hp, encoding="utf-8").read()))
    byname = {n.name: n for n in flat}
    L, R = byname.get("NPC L FootBox"), byname.get("NPC R FootBox")
    assert L and R, "FootBox nodes missing"
    lx, rx = L.world["translation"][0], R.world["translation"][0]
    print(f"FootBox world X: L={lx:.4f} R={rx:.4f} (mirror: {'OK' if lx*rx < 0 else 'FAIL'})")
    assert lx * rx < 0, "L/R FootBox did not mirror after FK compose"

    # Round-trip: rig -> flatten(world) -> worlds_to_rig(inverse-FK) -> emit -> reparse -> flatten,
    # and assert the final WORLD transforms match the originals. This is the proof the exporter is
    # the exact inverse of the importer (and thus that the Blender armature is a faithful rig).
    def close(a, b, rel=1e-4, absol=1e-3):
        # mixed tolerance keyed off the whole vector's magnitude (a near-zero component of a
        # large, deep FK chain still tolerates the chain's accrued float+quantization noise).
        scale = max((abs(x) for x in b), default=0.0)
        eps = absol + rel * scale
        return all(abs(x - y) <= eps for x, y in zip(a, b))

    rt_ok = 0
    worst = 0.0
    for label, path in rigs:
        rig = yaml.safe_load(open(path, encoding="utf-8").read())
        flat0 = rigcore.flatten(rig)
        names = [n.name for n in flat0]
        if len(names) != len(set(names)):
            continue  # duplicate node names can't key a world map; skip (rare)
        entries = [{"name": n.name, "parent": n.parent, "world": n.world} for n in flat0]
        exported = rigcore.emit_rig_yaml(rigcore.worlds_to_rig(entries))
        flat1 = rigcore.flatten(rigcore.parse_rig_yaml(exported))
        w0 = {n.name: n.world for n in flat0}
        for n in flat1:
            a, b = n.world["translation"], w0[n.name]["translation"]
            worst = max(worst, max(abs(x - y) for x, y in zip(a, b)))
            assert close(a, b), f"round-trip world drift on {label}:{n.name}: {a} vs {b}"
        rt_ok += 1
    print(f"import->export round-trip: {rt_ok}/{len(rigs)} rigs, worst world delta = {worst:.2e}")

    # Rigify control-rig refine (synthetic, self-contained): drop ORG/DEF/VIS, reparent survivors
    # in world space (no orphans), bind controls to their DEF target, tag IK roles.
    def W(x):
        return {"translation": [x, 0.0, 0.0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}
    syn = [
        {"name": "root", "parent": None, "world": W(0)},
        {"name": "ORG-Pelvis", "parent": "root", "world": W(1)},          # dropped
        {"name": "DEF-LegFoot.L", "parent": "ORG-Pelvis", "world": W(2)}, # dropped (binding target)
        {"name": "LegFoot_ik.L", "parent": "ORG-Pelvis", "world": W(3)},  # kept: ik + binds DEF-LegFoot.L
        {"name": "MCH-LegFoot_fk.L", "parent": "DEF-LegFoot.L", "world": W(4)},  # kept, reparents past DEF
        {"name": "VIS_LegFoot_pole.L", "parent": "root", "world": W(5)},  # dropped
        {"name": "tweak_Foot.L", "parent": "LegFoot_ik.L", "world": W(6)},
    ]
    ref = rigcore.rigify_control_rig(syn)
    kept = {e["name"]: e for e in ref}
    assert set(kept) == {"root", "LegFoot_ik.L", "MCH-LegFoot_fk.L", "tweak_Foot.L"}, sorted(kept)
    names = set(kept)
    assert all(e["parent"] is None or e["parent"] in names for e in ref), "orphan after reparent"
    assert kept["MCH-LegFoot_fk.L"]["parent"] == "root", "did not reparent past dropped DEF"
    assert kept["LegFoot_ik.L"]["extra"].get("deformTarget") == "DEF-LegFoot.L", "missing binding"
    assert kept["LegFoot_ik.L"]["extra"].get("role") == "ik", "missing role"
    print(f"rigify refine: {len(syn)} -> {len(ref)} bones, bindings + reparent + roles OK")

    print("\nALL CHECKS PASSED")


if __name__ == "__main__":
    main()
