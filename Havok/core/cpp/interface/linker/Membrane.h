#pragma once
// Membrane — the wrapper vocabulary the SCHEMA names, over the one Linker primitive. Governing rule:
// "the schema names a membrane; the membrane calls the linker." A schema field carries a tag like
// `IndexMembrane: roster` or `BoneMembrane: positional`; at bind time the compiler resolves the tag's
// table from the BindingContext and applies the membrane in both directions (encode=compile,
// decode=decompile) over the SAME field descriptor — one descriptor read two ways (CB-3 drift-proofing).
//
// Four membranes cover the whole hky format (see the linker/membrane spec):
//   EnumMembrane   static table (schema metadata)               mode, eventMode
//   IndexMembrane  runtime table (roster|events|vars|charprops) scalar or index-list
//   BoneMembrane   the actor's SKELETON (authoritative)         positional | index-list; bonelist is a
//                  regen-only vanilla projection, never authored
//   ChainMembrane  inherit another field's resolved bind        motion.animIndex <- clip
//
// The membranes hold NO tables of their own — they borrow a `const Linker*` from the BindingContext.
// Ownership + lifetime of the Linkers is the context's; membranes are cheap value types.

#include <interface/linker/Linker.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CB::core::linker {

// ── the binding context: which tables THIS unit binds against ────────────────────────────────────
// Assembled by compile/serve for a unit (a behaviour gets roster+events+variables+characterProps+
// skeleton; a character/project a subset). The implicit, scattered "which table" of the old code is
// this one explicit object — the heart of the refactor. Enum tables and chain (resolved-field) tables
// are keyed by name so a schema tag ("EnumMembrane: PlaybackMode", "ChainMembrane: clip") selects one.
struct BindingContext {
    Linker roster;          // animationNames, in order
    Linker events;          // hkbBehaviorGraphStringData.eventNames
    Linker variables;       // variable table
    Linker characterProps;  // character-property names
    Linker skeleton;        // bone name -> binary bone slot (skeleton-native, or bonelist-projected for vanilla)

    std::unordered_map<std::string, Linker> enums;           // enum name -> its name<->value table
    std::unordered_map<std::string, Linker> resolvedFields;  // field name -> its resolved (name -> value) binds (ChainMembrane source)

    // Resolve a named index space (the `IndexMembrane: <space>` tag) to its Linker, or nullptr.
    const Linker* space(std::string_view name) const;
};

// ── the scalar bind: the common shape (Enum, single Index, Chain) ────────────────────────────────
struct ScalarBind {
    const Linker* table = nullptr;
    std::optional<Value>       encode(std::string_view name) const { return table ? table->encode(name) : std::nullopt; }
    std::optional<std::string> decode(Value v)               const { return table ? table->decode(v)   : std::nullopt; }
    bool bound() const noexcept { return table != nullptr; }
};

// EnumMembrane / ChainMembrane are the scalar bind over different tables (a static enum set / another
// field's resolved binds). Distinct names for the schema vocabulary; one implementation for no drift.
using EnumMembrane  = ScalarBind;
using ChainMembrane = ScalarBind;

// ── IndexMembrane: scalar OR a name-list <-> value-list (a non-bone index array) ─────────────────
struct IndexMembrane {
    const Linker* table = nullptr;
    std::optional<Value>       encode(std::string_view name) const { return table ? table->encode(name) : std::nullopt; }
    std::optional<std::string> decode(Value v)               const { return table ? table->decode(v)   : std::nullopt; }

    std::vector<std::optional<Value>>       encodeList(const std::vector<std::string>& names)  const;
    std::vector<std::optional<std::string>> decodeList(const std::vector<Value>&       values) const;
    bool bound() const noexcept { return table != nullptr; }
};

// ── BoneMembrane: skeleton-authoritative bone binding ────────────────────────────────────────────
// The one genuinely special wrapper. Two array shapes:
//   positional  — flat[i] is bone i's value (bone WEIGHTS). decode -> one (name,value) per bone slot;
//                 encode places each named value at slot(name), filling absent bones. Position is
//                 DERIVED from the name, so it can't drift when a skeleton's bone count changes.
//   index-list  — a list of bone indices (hkbBoneIndexArray) <-> a list of bone names.
struct BoneMembrane {
    const Linker* skeleton = nullptr;   // bone name -> binary bone slot

    // An animation track's authored bone reference -> skeleton bone index, with the decompile
    // conventions: a "track<N>" placeholder means identity (track N animates bone N); no skeleton
    // bound or an unresolved name falls back to `ordinal` (identity). So a clip re-resolves against
    // the served skeleton by NAME (added bones shift indices) instead of freezing a raw index, and a
    // null/empty skeleton round-trips to the identity binding vanilla ships. Byte-identical to the
    // former havok::cross::trackBoneRef.
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

    // positional (bone weights)
    std::vector<std::pair<std::string, Value>> decodePositional(const std::vector<Value>& flat) const;
    std::vector<Value>                         encodePositional(
        const std::vector<std::pair<std::string, Value>>& named, std::size_t count, Value fill = 0) const;

    // index-list (hkbBoneIndexArray, footIk bones, …)
    std::vector<std::optional<std::string>> decodeIndexList(const std::vector<Value>&       indices) const;
    std::vector<std::optional<Value>>       encodeIndexList(const std::vector<std::string>& names)   const;

    bool bound() const noexcept { return skeleton != nullptr; }
};

}  // namespace CB::core::linker
