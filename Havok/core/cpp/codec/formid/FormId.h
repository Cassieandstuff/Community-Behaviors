#pragma once
// CB::core::formid — the node-identity FormId codec. "Bethesda's plugin FormID, for behavior-graph
// nodes." A node's identity is (masterIndex, local): the high half names a bundle RELATIVE to the
// storing bundle's master table, the low half is the node's local id.
//
// REVERSIBLE codec (reverse = decode), per the org-pass codec taxonomy. Four equivalent views of one
// identity, all bijective:
//   - struct FormId {masterIndex, local}            — the resolved fields
//   - packed uint32 = (masterIndex << 16) | local   — the compiler/merge KEY (fast, hashable)
//   - text "idx:local" (decimal)                    — the AUTHORING form in YAML (e.g. "0:184", "1:5")
//   - hex "IIIILLLL" (8 chars)                       — the canonical compact form (e.g. "000000B8")
//   - tagfile local id = low 16 bits                — the #NNNN a tagfile round-trip reads/writes
//
// LAYOUT (32-bit, 8 hex chars — ESP parity):
//   [ masterIndex : 16 ][ local : 16 ]
//   masterIndex 0 = the base game (Skyrim.hky), RESERVED for everyone incl. itself; 1..k = the bundle's
//     declared masters (header table order, append-only stable); self = the implicit k+1.
//   local = the tagfile #NNNN VERBATIM. 16 bits mirrors the engine's own `hkbNode::id` (a ushort) — an
//     id we can write is an id the engine can hold. (The MERGED graph node budget is ~32,767, the signed
//     `nextUniqueID` ceiling — a separate guard at the loader, not a codec concern.)
//
// The SELF-SPELLING choice (numeric-self "3:9" vs bare "9") lives ABOVE this codec: parse REQUIRES the
// "idx:local" form (a colon), so a reference is self-marking and never ambiguous with a data value. A
// bundle that spells self bare resolves it to an explicit index before calling parse.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace CB::core::formid {

    // A node identity relative to a bundle's master table.
    struct FormId {
        std::uint16_t masterIndex = 0;   // 0 = base game (Skyrim); 1..k = declared masters; k+1 = self
        std::uint16_t local       = 0;   // local node id = the tagfile #NNNN, verbatim
        constexpr bool operator==(const FormId& o) const {
            return masterIndex == o.masterIndex && local == o.local;
        }
    };

    inline constexpr std::uint16_t BASE_GAME_INDEX = 0;   // Skyrim.hky is index 0 for everyone

    // ── packed uint32 (the canonical compiler key) ──────────────────────────────────────────────
    inline constexpr std::uint32_t pack(FormId f) {
        return (static_cast<std::uint32_t>(f.masterIndex) << 16) | f.local;
    }
    inline constexpr FormId unpack(std::uint32_t v) {
        return FormId{ static_cast<std::uint16_t>(v >> 16), static_cast<std::uint16_t>(v & 0xFFFFu) };
    }

    // ── tagfile round-trip (reused BOTH directions by the converter) ────────────────────────────
    // The low 16 bits ARE the tagfile id — strip the namespace to get #NNNN; re-attach an index to
    // build a FormId from a tagfile id + the owning master's index.
    inline constexpr std::uint16_t tagfileId(FormId f) { return f.local; }
    inline constexpr FormId fromTag(std::uint16_t masterIndex, std::uint16_t tagLocal) {
        return FormId{ masterIndex, tagLocal };
    }

    // ── overflow-guarded construction ───────────────────────────────────────────────────────────
    // A local or index beyond 16 bits is EXACTLY the invisible mis-binding this model exists to prevent
    // (it would silently corrupt the other field on pack), so make it LOUD: nullopt on overflow. Callers
    // that mint ids (the converter, the compile-time bander) go through here.
    inline std::optional<FormId> make(std::uint32_t masterIndex, std::uint32_t local) {
        if (masterIndex > 0xFFFFu || local > 0xFFFFu) return std::nullopt;
        return FormId{ static_cast<std::uint16_t>(masterIndex), static_cast<std::uint16_t>(local) };
    }

    namespace detail {
        inline std::string_view trim(std::string_view s) {
            const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
            while (!s.empty() && ws(s.front())) s.remove_prefix(1);
            while (!s.empty() && ws(s.back()))  s.remove_suffix(1);
            return s;
        }
        inline int hexNibble(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }
        inline std::optional<std::uint16_t> hex4(std::string_view h) {
            if (h.size() != 4) return std::nullopt;
            std::uint16_t v = 0;
            for (char c : h) { const int d = hexNibble(c); if (d < 0) return std::nullopt; v = static_cast<std::uint16_t>((v << 4) | d); }
            return v;
        }
    }

    // ── text "IIIIxLLLL" (the authoring / on-disk form) ──────────────────────────────────────────
    // Fixed-width hex: 4-hex master index + 'x' + 4-hex local. No variable-width separator to parse
    // around (positions ARE the fields); the 'x' self-marks a FormId reference vs a data value (so the
    // reference-canonicalization pass is a generic scan, not a per-field enumeration); filename-safe
    // (no sanitization); and the 4-hex LOCAL visually documents the engine's node-id ceiling — hkbNode::id
    // is a signed 16-bit, so a graph tops out near 0x7FFF nodes.
    inline std::string format(FormId f) {
        static constexpr char H[] = "0123456789abcdef";
        std::string out(9, 'x');                                  // "____x____", index 4 stays 'x'
        for (int i = 0; i < 4; ++i) out[static_cast<std::size_t>(3 - i)] = H[(f.masterIndex >> (i * 4)) & 0xF];
        for (int i = 0; i < 4; ++i) out[static_cast<std::size_t>(8 - i)] = H[(f.local       >> (i * 4)) & 0xF];
        return out;
    }

    // "IIIIxLLLL" -> FormId. REQUIRES the exact fixed shape (4 hex, 'x', 4 hex); tolerates surrounding
    // whitespace. nullopt otherwise — so a bare number or a name is NOT mistaken for a FormId.
    inline std::optional<FormId> parse(std::string_view s) {
        s = detail::trim(s);
        if (s.size() != 9 || (s[4] != 'x' && s[4] != 'X')) return std::nullopt;
        const auto idx = detail::hex4(s.substr(0, 4));
        const auto loc = detail::hex4(s.substr(5, 4));
        if (!idx || !loc) return std::nullopt;
        return FormId{ *idx, *loc };
    }

    // ── hex "IIIILLLL" (8 chars, canonical compact) ─────────────────────────────────────────────
    inline std::string formatHex(FormId f) {
        static constexpr char H[] = "0123456789abcdef";
        const std::uint32_t v = pack(f);
        std::string out(8, '0');
        for (int i = 7; i >= 0; --i) { out[static_cast<std::size_t>(i)] = H[v >> ((7 - i) * 4) & 0xF]; }
        return out;
    }
    inline std::optional<FormId> parseHex(std::string_view s) {
        s = detail::trim(s);
        if (!s.empty() && (s[0] == '#')) s.remove_prefix(1);        // tolerate a leading '#'
        if (s.size() != 8) return std::nullopt;
        std::uint32_t v = 0;
        for (char c : s) {
            std::uint32_t d;
            if (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
            else return std::nullopt;
            v = (v << 4) | d;
        }
        return unpack(v);
    }

    // ── layout invariants (compile-time; the 16/16 split can never silently rot) ────────────────
    static_assert(pack(FormId{0, 184}) == 0x000000B8u, "local occupies the low 16 bits");
    static_assert(pack(FormId{1, 5})   == 0x00010005u, "masterIndex occupies the high 16 bits");
    static_assert(pack(FormId{0xFFFF, 0xFFFF}) == 0xFFFFFFFFu, "16/16 fills the 32-bit key");
    static_assert(unpack(0x00030009u) == FormId{3, 9}, "unpack is pack's inverse");
    static_assert(tagfileId(FormId{7, 6000}) == 6000, "tagfile id is the local half, verbatim");
    static_assert(fromTag(2, 999) == FormId{2, 999}, "fromTag re-attaches an index to a tagfile id");
    static_assert(pack(unpack(0xABCD1234u)) == 0xABCD1234u, "key round-trips");

}  // namespace CB::core::formid
