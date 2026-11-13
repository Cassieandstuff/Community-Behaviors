#include "DynamicClip.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>

namespace CB::engine::relay {

    namespace {

        std::uint64_t Mix(std::uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        constexpr std::array<std::string_view, 11> kScopes{ "self", "alias", "base", "race", "equipped.right", "equipped.left",
                                                            "ammo", "worn", "furniture", "combatTarget", "mount" };
        constexpr std::array<std::string_view, 7>  kTests{ "Exists", "IsForm", "IsSame", "HasKeyword", "ActorValue",
                                                          "HasMagicEffect", "HasMagicEffectWithKeyword" };

    }  // namespace

    std::uint64_t PickSeed(std::uint32_t actor, std::uint32_t activation, std::uint32_t loop)
    {
        return Mix(Mix(Mix(actor) ^ activation) ^ loop);
    }

    std::optional<std::int32_t> TopPriority(std::span<const ClipCandidate> rows)
    {
        std::optional<std::int32_t> top;
        for (const ClipCandidate& r : rows)
            if (r.passes && (!top || r.priority > *top)) top = r.priority;
        return top;
    }

    int PickClip(std::span<const ClipCandidate> rows, std::uint64_t seed)
    {
        const auto top = TopPriority(rows);
        if (!top) return -1;
        double total = 0.0;
        int    count = 0;
        for (const ClipCandidate& r : rows)
            if (r.passes && r.priority == *top) {
                ++count;
                if (r.weight > 0.0f) total += r.weight;
            }
        /// @note 53 bits of the seed as a fraction in [0, 1).
        const double u = static_cast<double>(Mix(seed) >> 11) * (1.0 / 9007199254740992.0);
        const bool   even   = total <= 0.0;
        double       target = even ? u * count : u * total;
        int          last   = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const ClipCandidate& r = rows[i];
            if (!r.passes || r.priority != *top) continue;
            const double w = even ? 1.0 : (r.weight > 0.0f ? r.weight : 0.0);
            if (w <= 0.0) continue;
            last = i;
            if (target < w) return i;
            target -= w;
        }
        return last;   ///< rounding at the very end of the range
    }

    bool ShouldRepick(std::span<const ClipCandidate> rows, int current, Moment moment)
    {
        const auto top = TopPriority(rows);
        if (current < 0 || current >= static_cast<int>(rows.size())) return moment == Moment::Loop && top.has_value();
        const ClipCandidate& c       = rows[current];
        const bool           stillTop = c.passes && top && c.priority == *top;
        if (moment == Moment::Update) return (c.flags & kInterruptible) && !stillTop;
        if (!(c.flags & kRedrawOnLoop)) return false;
        return !((c.flags & kKeepRandomOnLoop) && stillTop);
    }

    std::optional<FormKey> ParseFormKey(std::string_view text)
    {
        const std::size_t bar = text.rfind('|');
        if (bar == std::string_view::npos || bar == 0) return std::nullopt;
        std::string_view hex = text.substr(bar + 1);
        if (hex.size() > 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex.remove_prefix(2);
        if (hex.empty()) return std::nullopt;
        FormKey key;
        key.plugin       = std::string(text.substr(0, bar));
        const auto [p, e] = std::from_chars(hex.data(), hex.data() + hex.size(), key.id, 16);
        if (e != std::errc{} || p != hex.data() + hex.size() || key.id > 0xFFFFFF) return std::nullopt;
        return key;
    }

    bool ConditionTypes::Satisfies(std::string_view type, std::string_view want) const
    {
        for (int guard = 0; guard < 32; ++guard) {
            if (type == want) return true;
            const auto it = types.find(type);
            if (it == types.end()) return false;
            if (std::find(it->second.traits.begin(), it->second.traits.end(), want) != it->second.traits.end()) return true;
            if (it->second.is.empty()) return false;
            /// @note Single inheritance in practice; a second parent is walked through recursion.
            for (std::size_t i = 1; i < it->second.is.size(); ++i)
                if (Satisfies(it->second.is[i], want)) return true;
            type = it->second.is.front();
        }
        return false;
    }

    std::string ConditionTypes::Check(std::span<const std::string> kinds, std::string_view test) const
    {
        if (static_cast<int>(kinds.size()) > chainLimit) return "the scope chain is longer than " + std::to_string(chainLimit);
        std::string produced = "Actor";   ///< an empty chain is `self`
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const auto s = scopes.find(kinds[i]);
            if (s == scopes.end()) return "unknown scope '" + kinds[i] + "'";
            if (s->second.accepts.empty()) {
                if (i != 0) return "scope '" + kinds[i] + "' starts a chain and cannot follow another";
            } else if (!Satisfies(i == 0 ? "Actor" : produced, s->second.accepts)) {
                return "scope '" + kinds[i] + "' reads " + s->second.accepts + ", but gets " + (i == 0 ? "Actor" : produced);
            }
            produced = s->second.produces;
        }
        const auto t = tests.find(test);
        if (t == tests.end()) return "unknown test '" + std::string(test) + "'";
        if (!Satisfies(produced, t->second.accepts))
            return "test '" + std::string(test) + "' needs " + t->second.accepts + ", but the scope produces " + produced;
        return {};
    }

    std::optional<ConditionTypes> ParseConditionTypes(std::string_view json, std::string& err)
    {
        using Json = nlohmann::json;
        ConditionTypes out;
        try {
            const Json j       = Json::parse(json);
            const auto strings = [](const Json& a) {
                std::vector<std::string> v;
                if (a.is_array())
                    for (const auto& s : a) v.push_back(s.get<std::string>());
                return v;
            };
            for (const auto& [name, t] : j.at("types").items())
                out.types[name] = { strings(t.value("is", Json::array())), strings(t.value("traits", Json::array())) };
            for (const auto& [name, s] : j.at("scopes").items()) {
                ConditionTypes::Scope sc;
                if (s.contains("accepts") && !s["accepts"].is_null()) sc.accepts = s["accepts"].get<std::string>();
                sc.produces  = s.at("produces").get<std::string>();
                sc.arguments = strings(s.value("arguments", Json::array()));
                sc.many      = s.value("many", false);
                out.scopes[name] = std::move(sc);
            }
            for (const auto& [name, t] : j.at("tests").items())
                out.tests[name] = { t.at("accepts").get<std::string>(), strings(t.value("arguments", Json::array())), t.value("other", false) };
            out.chainLimit = j.value("chainLimit", 4);
        } catch (const std::exception& e) {
            err = e.what();
            return std::nullopt;
        }
        const auto known = [&](const std::string& name) {
            if (out.types.count(name)) return true;
            for (const auto& [_, t] : out.types)
                if (std::find(t.traits.begin(), t.traits.end(), name) != t.traits.end()) return true;
            return false;
        };
        for (const auto& [name, t] : out.types)
            for (const auto& p : t.is)
                if (!out.types.count(p)) { err = "type '" + name + "' derives from unknown '" + p + "'"; return std::nullopt; }
        for (const auto& [name, s] : out.scopes)
            if ((!s.accepts.empty() && !known(s.accepts)) || !out.types.count(s.produces)) {
                err = "scope '" + name + "' names an unknown type";
                return std::nullopt;
            }
        for (const auto& [name, t] : out.tests)
            if (!known(t.accepts)) { err = "test '" + name + "' accepts unknown '" + t.accepts + "'"; return std::nullopt; }
        return out;
    }

    std::span<const std::string_view> EvaluatedScopes() { return kScopes; }
    std::span<const std::string_view> EvaluatedTests() { return kTests; }

}  // namespace CB::engine::relay
