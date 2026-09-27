#include <compile/AnimDataDeriver.h>

#include <interface/linker/Membrane.h>   // the roster IndexMembrane (one clip<->roster join)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace CB::core::animdata {

namespace linker = CB::core::linker;

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
    // The SAME roster IndexMembrane ResolveClipIndices uses (case-folded key). animIndex is the roster
    // position. One join, one normalization — the offline and runtime clip binds can't diverge.
    const linker::Linker        rosterBind = linker::Linker::OfOrderedNames(roster, /*caseFold*/true);
    const linker::IndexMembrane rosterMembrane{&rosterBind};

    std::vector<ClipGenerator> out;
    out.reserve(clips.size());

    // Dedup by FULL record CONTENT, not by name. A behaviour graph reuses a clip-generator name across many
    // states/behaviours; when those repeats are IDENTICAL (same animation/crop/speed/triggers) the cache
    // keeps ONE record (vanilla is all-unique-name, so this is a no-op there). But when the same name carries
    // DIFFERENT content — a genuinely distinct binding, as SkyParkour's ClimbHigh does — each distinct record
    // is kept, the repeats suffixed "_1", "_2", … in encounter order (the vanilla/Nemesis convention; vanilla
    // carries 46 such "_N"). The OLD dedup keyed on NAME alone, collapsing distinct same-name bindings into
    // one — dropping the mod records Pandora emits and shifting the high-band animIndex mapping (mis-bind).
    std::unordered_map<std::string, int> nameRecords;   // base name -> distinct records emitted so far (for the suffix)
    std::unordered_set<std::string>      seenContent;    // full record signature -> already emitted (drop exact dupes)

    for (const auto& c : clips) {
        ClipGenerator g;
        g.name          = c.name;   // the "_N" suffix (if needed) is assigned after the content is known, below
        g.playbackSpeed = FormatG(c.playbackSpeed);
        g.cropStart     = FormatG(c.cropStart);
        g.cropEnd       = FormatG(c.cropEnd);

        int animIndex = -1;
        if (const auto v = rosterMembrane.encode(c.animationName))
            animIndex = static_cast<int>(*v);
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

        // Signature = the record's full content under its BASE name (so the "_N" suffix itself isn't part of
        // the key). An exact duplicate binding is dropped; a new DISTINCT record whose base name was already
        // used gets the next "_N" suffix — matching Pandora (identical repeats collapse, distinct repeats kept).
        std::string sig = c.name;
        sig += '\x1f'; sig += g.animIndex;
        sig += '\x1f'; sig += g.playbackSpeed;
        sig += '\x1f'; sig += g.cropStart;
        sig += '\x1f'; sig += g.cropEnd;
        for (const auto& tl : g.triggers) { sig += '\x1f'; sig += tl; }
        if (!seenContent.insert(sig).second) continue;                    // identical binding -> one record
        if (const int n = nameRecords[c.name]++; n > 0) g.name = c.name + "_" + std::to_string(n);

        out.push_back(std::move(g));
    }
    return out;
}

}  // namespace CB::core::animdata
