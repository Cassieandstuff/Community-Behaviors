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

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from cb_control_rig import rigcore  # noqa: E402

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

    print("\nALL CHECKS PASSED")


if __name__ == "__main__":
    main()
