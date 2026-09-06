#include "havok/anim/AnimDataDeriver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_set>

namespace havok::animdata {

namespace {

std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::string FormatG(double v) {
    // Bethesda's cache uses printf %g (6 significant digits) with the LEGACY Windows
    // 3-digit exponent ("3.72529e-009"). Modern MSVC emits 2 digits ("...e-09"), so pad
    // the exponent back to a minimum of 3 digits to match the vanilla file byte-for-byte.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    std::string s(buf);
    const auto e = s.find('e');
    if (e != std::string::npos && e + 2 < s.size()) {
        const std::size_t d = e + 2;             // skip 'e' and the sign
        std::string digits = s.substr(d);
        while (digits.size() < 3) digits.insert(digits.begin(), '0');
        s = s.substr(0, d) + digits;
    }
    return s;
}

double SnapDuration30(double d) {
    // The carried duration is a %g-rounded string; snap it back to the 30fps frame grid
    // so eff = snap30(dur)/speed reproduces the cache's full-precision trigger clamp.
    return std::round(d * 30.0) / 30.0;
}

std::vector<ClipGenerator> DeriveClipList(
    const std::vector<DeriveClipInput>&    clips,
    const std::vector<std::string>&        roster,
    const std::unordered_map<int, double>& motionDurByIndex,
    std::vector<std::string>*              unresolved)
{
    // roster: animationName (lowercased) -> first index. animIndex is that position.
    std::unordered_map<std::string, int> rosterIndex;
    rosterIndex.reserve(roster.size() * 2);
    for (int i = 0; i < static_cast<int>(roster.size()); ++i)
        rosterIndex.emplace(ToLower(roster[static_cast<std::size_t>(i)]), i);

    std::vector<ClipGenerator> out;
    out.reserve(clips.size());

    // The cache lists each clip NAME exactly once (verified: vanilla DefaultMale = 2520 clips /
    // 2520 unique names), but a graph has many clip generators sharing a name across behaviors.
    // Collapse to one per name, first occurrence in input order. (Name COLLISIONS — same name,
    // different clip — are rare; any residual they cause surfaces in the offline byte-gate.)
    std::unordered_set<std::string> seenNames;
    seenNames.reserve(clips.size() * 2);

    for (const auto& c : clips) {
        if (!seenNames.insert(c.name).second) continue;   // duplicate name — cache keeps one
        ClipGenerator g;
        g.name          = c.name;
        g.playbackSpeed = FormatG(c.playbackSpeed);
        g.cropStart     = FormatG(c.cropStart);
        g.cropEnd       = FormatG(c.cropEnd);

        int animIndex = -1;
        if (auto it = rosterIndex.find(ToLower(c.animationName)); it != rosterIndex.end())
            animIndex = it->second;
        else if (unresolved)
            unresolved->push_back(c.name + "  (" + c.animationName + ")");
        g.animIndex = std::to_string(animIndex);

        // Effective clip duration for the trigger clamp. No motion record (e.g. a clip whose
        // animation has no root motion) -> no clamp bound; end-relative falls back to localTime.
        bool   hasDur = false;
        double eff    = std::numeric_limits<double>::infinity();
        if (animIndex >= 0) {
            if (auto mit = motionDurByIndex.find(animIndex); mit != motionDurByIndex.end()) {
                const double speed = (c.playbackSpeed != 0.0) ? c.playbackSpeed : 1.0;
                // The playable clip window is the animation minus the cropped head/tail; an
                // end-relative trigger lands at that crop-adjusted end (verified against vanilla
                // 2HM_Attack*: attackStop = dur - cropStart).
                eff    = (SnapDuration30(mit->second) - c.cropStart - c.cropEnd) / speed;
                hasDur = true;
            }
        }

        // Compute + format each trigger time, then sort ascending by time (the cache order).
        std::vector<std::pair<double, std::string>> trigs;
        trigs.reserve(c.triggers.size());
        for (const auto& t : c.triggers) {
            std::string ev = t.event;
            if (t.fromAnnotation) {
                // AnimObject load/draw events are handled by the animobject system, not listed.
                if (ev.rfind("AnimObjDraw", 0) == 0 || ev.rfind("AnimObjLoad", 0) == 0) continue;
                // A SoundPlay annotation stores only the event name (its ".sound" payload is
                // consumed from the annotation at runtime); other events (SoundStop.X, foot
                // events, …) are kept verbatim.
                // NOTE (2026-08-25): this strip is CONDITIONAL in vanilla — chaurus strips
                // "SoundPlay.NPCChaurusAttack" -> bare, slaughterfish KEEPS "SoundPlay.NPC
                // SlaughterfishAttack". Discriminator not yet identified; removing the strip is
                // net-negative on the 49-project sweep. Part-B byte-exact derivation is blocked on
                // reversing this rule (see the master memory).
                if (ev.rfind("SoundPlay.", 0) == 0) ev = "SoundPlay";
            }
            if (ev.empty()) continue;   // never emit an empty-named trigger
            double tt = t.relativeToEndOfClip ? (hasDur ? eff + t.localTime : t.localTime)
                                              : t.localTime;
            // Clamp to the playable window. Forward clips: [0, eff]. But a REVERSE clip
            // (negative playbackSpeed) has eff < 0, so its window is [eff, 0] — ordering the
            // bounds keeps such triggers at their real negative time instead of collapsing to 0
            // (matches vanilla Stf_Unequip / CrossBow_Unequip). Identical to the old
            // max(0,min(tt,eff)) whenever eff >= 0, so forward clips (e.g. 2HM_Attack*) are unchanged.
            if (hasDur) tt = std::clamp(tt, std::min(0.0, eff), std::max(0.0, eff));
            trigs.emplace_back(tt, ev + ":" + FormatG(tt));
        }
        // Sort ascending by time; equal times keep source order (the cache's tie-break — e.g.
        // PitchOverrideStart before BeginWeaponDraw, both at 0).
        std::stable_sort(trigs.begin(), trigs.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [tt, line] : trigs) g.triggers.push_back(std::move(line));

        out.push_back(std::move(g));
    }
    return out;
}

}  // namespace havok::animdata
