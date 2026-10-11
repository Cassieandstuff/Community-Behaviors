#pragma once
// Membrane — the wrapper vocabulary the SCHEMA names, over the one Linker primitive. Governing rule:
// "the schema names a membrane; the membrane calls the linker."
//
//   IndexMembrane  runtime table (roster|events|vars|charprops) scalar
//   BoneMembrane   the actor's SKELETON (authoritative)         animation track bone refs
//
// The membranes hold NO tables of their own — they borrow a `const Linker*` from the caller.
// Ownership + lifetime of the Linkers is the caller's; membranes are cheap value types.

#include <interface/linker/Linker.h>

#include <optional>
#include <string>
#include <string_view>

namespace CB::engine::linker {

// ── IndexMembrane: a name <-> value bind over a non-bone index table ─────────────────────────────
struct IndexMembrane {
    const Linker* table = nullptr;
    std::optional<Value>       encode(std::string_view name) const { return table ? table->encode(name) : std::nullopt; }
    std::optional<std::string> decode(Value v)               const { return table ? table->decode(v)   : std::nullopt; }
};

// ── BoneMembrane: skeleton-authoritative bone binding ────────────────────────────────────────────
struct BoneMembrane {
    const Linker* skeleton = nullptr;   // bone name -> binary bone slot

    // An animation track's authored bone reference -> skeleton bone index, with the decompile
    // conventions: a "track<N>" placeholder means identity (track N animates bone N); no skeleton
    // bound or an unresolved name falls back to `ordinal` (identity). So a clip re-resolves against
    // the served skeleton by NAME (added bones shift indices) instead of freezing a raw index, and a
    // null/empty skeleton round-trips to the identity binding vanilla ships. Byte-identical to the
    // former CB::engine::cross::trackBoneRef.
    int resolveTrackRef(std::string_view ref, int ordinal) const {
        if (ref.size() > 5 && ref.substr(0, 5) == "track") {
            int n = 0; bool allDigits = true;
            for (char c : ref.substr(5)) { if (c < '0' || c > '9') { allDigits = false; break; } n = n * 10 + (c - '0'); }
            if (allDigits) return n;
        }
        if (!skeleton || skeleton->empty()) return ordinal;
        const auto idx = skeleton->encode(ref);
        return idx ? static_cast<int>(*idx) : ordinal;
    }
};

}  // namespace CB::engine::linker
