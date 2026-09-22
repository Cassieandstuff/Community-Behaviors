#pragma once
// Linker — THE binding primitive. A trivial, policy-free, bidirectional map between a NAME and a
// VALUE (an index into some table, or a static enum constant — both are just "a number that goes in
// the hky binary"). One implementation, proven once; every name<->value binding in the format is built
// on it. See the linker/membrane spec.
//
// The primitive holds NO policy — no lowercasing, no path normalization, no arity. Those live in the
// Membrane wrappers (Membrane.h) that the schema names. Havok is CASE-SENSITIVE: the Linker matches
// names EXACTLY. Any case-folding is a caller decision, applied identically to both sides before the
// name reaches here (an internal key space), never inside the primitive.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CB::core::linker {

using Value = std::int64_t;   // an index or an enum constant

class Linker {
public:
    Linker() = default;

    // An INDEX space: value = position in the ordered list. First occurrence wins for encode (a name
    // that repeats binds to its FIRST slot); decode(i) returns names[i] verbatim. This is the roster /
    // event table / variable table / skeleton form.
    //
    // caseFold is the SPACE's normalization, decided ONCE here (the roster is keyed case-insensitively;
    // events/enums are exact). It folds only the internal ENCODE key on both sides — decode still returns
    // the ORIGINAL-case name, and the resolved value is a number, so nothing miscased reaches the engine
    // (safe per the CLAUDE.md case-sensitivity rule: a purely internal key folded identically both ways).
    static Linker OfOrderedNames(const std::vector<std::string>& names, bool caseFold = false);

    // An explicit name<->value set: enums (MODE_SINGLE_PLAY -> 47), or a chain table built from another
    // field's resolved binds (clip name -> its animIndex). First pair wins on a duplicate name/value.
    static Linker OfPairs(std::vector<std::pair<std::string, Value>> pairs, bool caseFold = false);

    // name -> value. nullopt when the name is not in this space (a dangling reference — the caller/
    // membrane turns that into a diagnostic rather than a silent fallback).
    std::optional<Value> encode(std::string_view name) const;

    // value -> name. nullopt when the value is out of this space.
    std::optional<std::string> decode(Value v) const;

    bool        empty() const noexcept { return fwd_.empty(); }
    std::size_t size()  const noexcept { return fwd_.size(); }

private:
    static std::string fold(std::string_view s);   // ASCII lowercase (the case-fold key), or identity

    bool                                   caseFold_ = false;
    std::unordered_map<std::string, Value> fwd_;   // fold(name) -> value  (first name wins)
    std::map<Value, std::string>           rev_;   // value -> ORIGINAL name (first value wins; ordered)
};

}  // namespace CB::core::linker
