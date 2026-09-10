#!/usr/bin/env bash
# Acceptance gate for the (class,name) editorID migration.
# Compiles a representative unit set from the packed master via the runtime merge path
# (hky-merge-compile), and reports per-unit: OK/FAIL, byte size, md5, and cross-class collision count.
# Usage: gate.sh <label>   (writes /tmp/idgate_<label>/ with hkx + a summary.tsv)
set -u
ROOT="D:/Projects/SKSE/Community Behaviors/.claude/worktrees/custom-havok-class-framework-c490dd"
CLI="$ROOT/build/release/bin/RelWithDebInfo/havok-core-cli.exe"
M="/d/MO2 Data/SSE - Dragon Break Project/mods/Community Behaviors/community_behaviors/plugins/Skyrim.hky"
SCHEMA="$ROOT/Havok"
LABEL="${1:-run}"
OUT="/tmp/idgate_$LABEL"; mkdir -p "$OUT"
UNITS=(
  "meshes/genericbehaviors/behaviors/autoplay/autoplaybehavior.hkx"
  "meshes/actors/character/_1stperson/behaviors/magicbehavior.hkx"
  "meshes/actors/character/behaviors/mt_behavior.hkx"
  "meshes/actors/character/behaviors/weapequip.hkx"
  "meshes/actors/character/behaviors/sprintbehavior.hkx"
  "meshes/actors/canine/behaviors wolf/quadrupedbehavior.hkx"
  "meshes/actors/sabrecat/behaviors/quadrupedbehavior.hkx"
  "meshes/actors/horse/behaviors/horsebehavior.hkx"
  "meshes/actors/character/_1stperson/behaviors/1hm_behavior.hkx"
  "meshes/actors/character/behaviors/1hm_behavior.hkx"
)
printf "unit\tresult\tbytes\tmd5\tcollisions\n" > "$OUT/summary.tsv"
for u in "${UNITS[@]}"; do
  bn=$(echo "$u" | tr '/ ' '__').hkx
  log=$("$CLI" hky-merge-compile "$u" "$M" -o "$OUT/$bn" --schema "$SCHEMA" 2>&1)
  if echo "$log" | grep -q "^OK:"; then res=OK; else res=FAIL; fi
  col=$(echo "$log" | grep -c "claimed by TWO")
  sz=$(stat -c%s "$OUT/$bn" 2>/dev/null || echo 0)
  md=$(md5sum "$OUT/$bn" 2>/dev/null | cut -d' ' -f1)
  printf "%s\t%s\t%s\t%s\t%s\n" "$(basename "$u")" "$res" "$sz" "$md" "$col" >> "$OUT/summary.tsv"
done
column -t -s$'\t' "$OUT/summary.tsv"
