#!/bin/sh
# cbmatch's regression test: deliberately broken copies of the schema runtime, each of which the gate must reject.
#   tools/cbmatch/tests/mutants.sh <cbmatch.exe> <workDir> [seeds]   (Git Bash; same environment as manifests/schema.yaml)
# Every mutant must come out STRUCTURAL except the documented VARIANT blind spots, which must come out VARIANT and then
# DIVERGE under --diff (<seeds> synthetic inputs, default 256): u3 (dropped i--), t3 (two register arguments swapped) and
# k2 (a constant the build already holds in a register: no load, so the changed value is just another register) are
# register dataflow; c1 and y2 (inverted tests) are jz/jnz sharing a condition family because branch layout inverts them.
# Controls (x_*) compile the same functions unmutated: they must come out VARIANT with no divergence, or a harness that
# diverges on everything would pass.
# Families: m activate, u update, d deactivate, g generate, a advanceLocalTime (x*2 == x+x canonicalization),
# r resetAtEnd (callee-saved save/restore), t processTriggers (crossed reads), p processTriggersInRange,
# c processAcyclicTriggers (repeated guard, materialized null test), e getExtractedMotionDelta (crossed stores,
# home-area scratch), s setElapsedTime (frame size), k startEcho, y updateSync (rax frame base, materialized zero test
# with an intervening instruction), j hkbSenseHandleModifier::senseInRigidBodies (switch jump tables). A mutant of a
# header is compiled against a shadowing copy of that header (unit /I first).
set -e
cbmatch=${1:?usage: mutants.sh <cbmatch.exe> <workDir> [seeds]}
work=${2:?usage: mutants.sh <cbmatch.exe> <workDir> [seeds]}
seeds=${3:-256}
repo=$(cd "$(dirname "$0")/../../.." && pwd -W)
havok="$repo/schema/Havok"
clip="Behavior/Generator/Clip/hkbClipGenerator.cpp"
expectVariant=" u3 t3 c1 k2 y2 "

mkdir -p "$work"
work=$(cd "$work" && pwd -W)
rm -rf "$work/mut" && mkdir -p "$work/mut"
units=""
names=""

# target <name>: the class, source file, function and vanilla address a name's family letter stands for.
target() {
    cls=hkbClipGenerator src=$clip
    case "$1" in
        m*) fn=activate va=0x140acd750 ;;           u*) fn=update va=0x140acde40 ;;
        d*) fn=deactivate va=0x140ace1e0 ;;         g*) fn=generate va=0x140ace400 ;;
        a*) fn=advanceLocalTime va=0x140acfde0 ;;   r*) fn=resetAtEnd va=0x140ad1050 ;;
        t*) fn=processTriggers va=0x140acf790 ;;    p*) fn=processTriggersInRange va=0x140acfb40 ;;
        c*) fn=processAcyclicTriggers va=0x140acf960 ;;
        e*) fn=getExtractedMotionDelta va=0x140acfff0 ;;
        s*) fn=setElapsedTime va=0x140acecd0 ;;
        k*) fn=startEcho va=0x140acee60 ;;
        y*) fn=updateSync va=0x140ace930 ;;
        j*) cls=hkbSenseHandleModifier src=Behavior/Modifier/SenseHandle/hkbSenseHandleModifier.cpp
            fn=senseInRigidBodies va=0x140afdbf0 ;;
    esac
}

# mutant <name> <file under schema/Havok> <sed expression> — the expression must change exactly that file.
mutant() {
    name=$1 file=$2 expr=$3
    target "$name"
    case "$file" in
        *.cpp) out="$work/mut/$name.cpp"
               source="$out" flags="[]" ;;
        *)     out="$work/mut/$name/$file"
               mkdir -p "$(dirname "$out")"
               source="$havok/$src" flags="[\"/I$work/mut/$name\"]" ;;
    esac
    sed "$expr" "$havok/$file" > "$out"
    if cmp -s "$out" "$havok/$file"; then echo "UNCHANGED: $name (its pattern no longer matches $file)"; exit 1; fi
    units="$units
  - { source: \"$source\", flags: $flags, functions: [ { name: \"CB::schema::$cls::$fn\", va: $va } ] }"
    names="$names $name"
}

mutant m1_wrong_field          "$clip" 's/trig\.m_time - m_cropStartAmountLocalTime/trig.m_time - m_cropEndAmountLocalTime/'
mutant m2_dropped_call         "$clip" 's/^            resetAtEnd();$/            \/\/ resetAtEnd() dropped/'
mutant m3_wrong_arg            "$clip" 's/addTrigger(t, hkbEvent(internalId, payload, HK_NULL), false, false, true);/addTrigger(t, hkbEvent(internalId, payload, HK_NULL), false, true, true);/'
mutant m4_wrong_const          "$clip" 's/new hkaDefaultAnimationControl(HK_NULL, true, 2)/new hkaDefaultAnimationControl(HK_NULL, true, 3)/'
mutant m5_inverted_compare     "$clip" 's/if (t >= 0\.0f \&\& t <= duration)/if (t > 0.0f \&\& t <= duration)/'
mutant m6_extra_store          "$clip" 's/^            m_binding = m_animationControl->m_binding;$/            m_binding = m_animationControl->m_binding; m_userData = 0;/'
mutant m7_wrong_callee         "$clip" 's/^            resetAtEnd();$/            fn_140ad11f0();/'
mutant u1_wrong_load_field     "$clip" 's/echo\.m_weight += timestep \* echo\.m_dwdt;/echo.m_weight += timestep * echo.m_offsetLocalTime;/'
mutant u2_dropped_store        "$clip" 's/^            m_time += dt;$/            \/\/ m_time += dt dropped/'
mutant u3_dropped_decrement    "$clip" 's/^                    i--;$/                    \/\/ i-- dropped/'
mutant d1_dropped_call         "$clip" 's/^        if (m_animationControl) m_animationControl->setBinding(HK_NULL);$/        \/\/ setBinding dropped/'
mutant d2_dropped_store        "$clip" 's/^        m_binding = HK_NULL;$/        \/\/ m_binding clear dropped/'
mutant d3_dropped_setsize      "$clip" 's/^        m_echos\.setSize(0);$/        \/\/ setSize dropped/'
mutant d4_swapped_assign       "$clip" 's/^            m_triggers         = m_originalTriggers;$/            m_originalTriggers = m_triggers;/'
mutant g1_swapped_quat_mul     Common/Base/Math/hkMath.h 's/m_rotation\.setMul(t1\.m_rotation, t2\.m_rotation);/m_rotation.setMul(t2.m_rotation, t1.m_rotation);/'
mutant g2_dropped_scale        "$clip" 's/if (!isAdditive) pose\[0\]\.m_scale\.mul4(hkSimdReal(scale));/\/\/ scale mul dropped/'
mutant g3_swapped_fill         "$clip" 's/isAdditive ? \&g_vectorConstantZero : \&g_vectorConstantMax/isAdditive ? \&g_vectorConstantMax : \&g_vectorConstantZero/'
mutant g4_add_to_sub           Common/Base/Math/hkMath.h 's/const hkSimdReal q2minus1(_mm_add_ps(/const hkSimdReal q2minus1(_mm_sub_ps(/'
mutant a1_doubling_to_triple   "$clip" 's/m_animationControl->m_cropStartAmountLocalTime \* 2\.0f;/m_animationControl->m_cropStartAmountLocalTime * 3.0f;/'
mutant a2_dropped_doubling     "$clip" 's/= duration - localTime + m_animationControl->m_cropStartAmountLocalTime \* 2\.0f;/= duration - localTime + m_animationControl->m_cropStartAmountLocalTime;/'
mutant a3_doubled_wrong_value  "$clip" 's/= duration - localTime + m_animationControl->m_cropStartAmountLocalTime \* 2\.0f;/= duration - localTime + localTime * 2.0f;/'
mutant r1_inverted_compare     "$clip" 's/if (m_playbackSpeed < 0\.0f) {/if (m_playbackSpeed <= 0.0f) {/'
mutant r2_dropped_store        "$clip" 's/^        m_time  *= 0\.0f;$/        \/\/ m_time reset dropped/'
mutant t1_wrong_mode_field     "$clip" 's/if (m_mode == MODE_PING_PONG) m_pingPongBackward = !m_pingPongBackward;/if (m_flags == MODE_PING_PONG) m_pingPongBackward = !m_pingPongBackward;/'
mutant t2_wrong_flag_field     "$clip" 's/processTriggersInRange(prevLocalTime, localTime, m_atEnd, queue, context);/processTriggersInRange(prevLocalTime, localTime, m_ignoreStartTime, queue, context);/'
mutant t3_swapped_range        "$clip" 's/else  *processTriggersInRange(duration, 0\.0f, true, queue, context);/else processTriggersInRange(0.0f, duration, true, queue, context);/'
mutant p1_inclusive_end        "$clip" 's/(!atEnd \&\& clipTime < to)/(!atEnd \&\& clipTime <= to)/'
mutant p2_dropped_silent_check "$clip" 's/if (graphData \&\& graphData->isEventSilent(trig\.m_event\.getId())) continue;/\/\/ silent check dropped/'
mutant c1_inverted_null_test   "$clip" 's/if (trig\.m_acyclic\.m_bool \&\& graphData \&\& /if (trig.m_acyclic.m_bool \&\& !graphData \&\& /'
mutant c2_strict_lower_bound   "$clip" 's/\&\& fromTime <= trig\.m_localTime \&\&/\&\& fromTime < trig.m_localTime \&\&/'
mutant c3_guard_constant       "$clip" 's/if (numTriggers <= 0) return;/if (numTriggers <= 1) return;/'
mutant c4_dropped_acyclic_test "$clip" 's/if (trig\.m_acyclic\.m_bool \&\& graphData \&\& /if (graphData \&\& /'
mutant e1_no_local_copy        "$clip" 's/m_mapperData->m_mapper->mapExtractedMotion(motion, out);/m_mapperData->m_mapper->mapExtractedMotion(out, out);/'
mutant e2_wrong_mirror_bit     "$clip" 's/if (m_flags \& FLAG_MIRROR) context/if (m_flags \& FLAG_MIRROR_POSE) context/'
mutant e3_scaled_wrong_field   "$clip" 's/out\.m_translation\.mul4(hkSimdReal(context/out.m_scale.mul4(hkSimdReal(context/'
mutant e4_dropped_ignore_test  "$clip" 's/if (animation \&\& !(m_flags \& FLAG_IGNORE_MOTION) \&\& /if (animation \&\& /'
mutant s1_wrong_half_cycle_bit "$clip" 's/!(m_flags \& FLAG_SYNC_HALF_CYCLE_IN_PING_PONG_MODE)/!(m_flags \& FLAG_MIRROR)/'
mutant s2_dropped_ignore_store "$clip" 's/^        m_ignoreStartTime  *= true;$/        \/\/ m_ignoreStartTime store dropped/'
mutant s3_wrong_half           "$clip" 's/t > cycle \* 0\.5f/t > cycle * 0.25f/'
mutant k1_limit_inclusive      "$clip" 's/if (numEchos < s_maxEchos) {/if (numEchos <= s_maxEchos) {/'
mutant k2_fade_sign            "$clip" 's/echo\.m_dwdt            = -1\.0f \/ echoDuration;/echo.m_dwdt            = 1.0f \/ echoDuration;/'
mutant k3_wrap_wrong_mode      "$clip" 's/if (t > duration \&\& m_mode == MODE_LOOPING)/if (t > duration \&\& m_mode == MODE_PING_PONG)/'
mutant y1_wrong_sync_field     "$clip" 's/^        syncInfo\.m_playbackSpeed = 1\.0f;$/        syncInfo.m_localTime     = 1.0f;/'
mutant y2_inverted_sync_point  "$clip" 's/if (graphData \&\& graphData->isEventSyncPoint(id)) {/if (graphData \&\& !graphData->isEventSyncPoint(id)) {/'
sense="Behavior/Modifier/SenseHandle/hkbSenseHandleModifier.cpp"
mutant j1_swapped_cases        "$sense" 's/case SENSE_IN_RIGID_BODIES_OUTSIDE_THIS_CHARACTER:/case TMP_SWAP:/; s/case SENSE_IN_OTHER_CHARACTER_RIGID_BODIES:/case SENSE_IN_RIGID_BODIES_OUTSIDE_THIS_CHARACTER:/; s/case TMP_SWAP:/case SENSE_IN_OTHER_CHARACTER_RIGID_BODIES:/'

# control <family letter>: the family's function compiled from the unmutated source.
control() {
    target "$1"
    units="$units
  - { source: \"$havok/$src\", flags: [], functions: [ { name: \"CB::schema::$cls::$fn\", va: $va } ] }"
    names="$names x_$fn"
}
for family in $expectVariant; do control "$family"; done

cat > "$work/mutants.yaml" <<EOF
runtimes:
  ae: { exe: "\${CB_SKYRIM_AE_EXE}", addresslib: "\${CB_ADDRESSLIB_AE}", pins: ae }
registry: "$repo/src/core/hooks/registry/engine.yaml"
toolchains:
  vs2015: "\${VS140COMNTOOLS}../../VC/bin/amd64/cl.exe"
flags: [/O2, /Ob1, /GS-, /fp:fast]
includes: ["$havok", "\${VS140COMNTOOLS}../../VC/include"]
units:$units
EOF

"$cbmatch" "$work/mutants.yaml" --allow-variant --diff "$seeds" --work "$work/run" > "$work/result.txt" 2>&1 || true
# One "<status> <divergences>" pair per function, in manifest order; "-" = --diff didn't run, "?" = it couldn't.
results=$(awk '
    / 14[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f] / { if (s != "") print s, d; s = $4; d = "-" }
    /^    diff: / { if ($0 ~ /not run/) d = "?"; else if ($0 ~ / DIVERGE/) { n = $0; sub(/ DIVERGE.*/, "", n); sub(/.* /, "", n); d = n } }
    END { if (s != "") print s, d }' "$work/result.txt")
failed=0 total=0
set -- $results
for name in $names; do
    status=${1:-MISSING} diverged=${2:--}
    [ $# -gt 1 ] && shift 2
    total=$((total + 1))
    short=${name%%_*}
    ok=""
    case "$name" in
        x_*) want="VARIANT, no divergence"
             [ "$status" = VARIANT ] && [ "$diverged" = 0 ] && ok=y ;;
        *)   case "$expectVariant" in
                 *" $short "*) want="VARIANT, diverging"
                               [ "$status" = VARIANT ] && [ "$diverged" != - ] && [ "$diverged" != "?" ] && [ "$diverged" != 0 ] && ok=y ;;
                 *)            want=STRUCTURAL
                               [ "$status" = STRUCTURAL ] && ok=y ;;
             esac ;;
    esac
    if [ "$ok" = y ]; then
        echo "  ok    $name ($status, $diverged diverging)"
    else
        echo "  FAIL  $name: $status, $diverged diverging; expected $want"
        failed=$((failed + 1))
    fi
done
echo "$((total - failed))/$total as expected"
[ $failed -eq 0 ]
