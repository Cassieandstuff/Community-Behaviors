#include "BaseMaps.h"

#include <codec/resource/PathKey.h>
#include <codec/serialization/HkyArchive.h>

#include <algorithm>
#include <filesystem>

namespace CB::converter {
namespace {

using CB::engine::codec::resource::PathKey;

// "meshes/actors/horse/behaviors/horsebehavior.hkx" -> "actors/horse"
// Handles the space-folder variants ("behaviors wolf", "characters female") and _1stperson.
std::string actorOf(const std::string& prefixKey) {
    const auto ap = prefixKey.find("actors/");
    if (ap == std::string::npos) return {};
    const std::string rest = prefixKey.substr(ap + 7);  // after "actors/"
    std::size_t cut = std::string::npos;
    for (const char* seg : { "/behaviors", "/characters", "/_1stperson" }) {
        const auto b = rest.find(seg);
        if (b != std::string::npos && (cut == std::string::npos || b < cut)) cut = b;
    }
    const std::string actorSeg = (cut == std::string::npos) ? rest.substr(0, rest.find('/'))
                                                            : rest.substr(0, cut);
    return "actors/" + actorSeg;
}

std::string stemOf(const std::string& prefixKey) {
    const auto slash = prefixKey.rfind('/');
    std::string fn = (slash == std::string::npos) ? prefixKey : prefixKey.substr(slash + 1);
    const auto dot = fn.rfind(".hkx");
    return (dot == std::string::npos) ? fn : fn.substr(0, dot);
}


}  // namespace

std::string ActorOfServePath(const std::string& servePath) { return actorOf(PathKey(servePath)); }

BaseMaps BuildBaseMaps(const CB::engine::codec::HkyArchive& baseArc) {
    using UK = CB::engine::codec::HkyArchive::UnitKind;
    BaseMaps m;
    for (const auto& u : baseArc.units()) {
        /// @note Map KEYS (stem, actor) are path identity; the VALUES keep the unit's own serve path (unitOut writes it).
        const std::string& prefix = u.prefix;
        const std::string  key    = PathKey(prefix);
        if (u.kind == UK::Behavior) {
            const std::string stem  = stemOf(key);
            const std::string actor = actorOf(key);
            const bool isFp = key.find("/_1stperson/behaviors/") != std::string::npos;
            auto& sp = isFp ? m.fpGraphServePath : m.graphServePath;
            auto& ap = isFp ? m.fpGraphActorPath : m.graphActorPath;
            // Templated/Nemesis graphs are the character-family graphs (templates/ mirrors
            // actors/character). A stem can collide across actors — horsebehavior exists at BOTH
            // actors/character (the rider's mounted behavior, 118 vars incl. MC_*) and actors/horse
            // (the horse creature, 76 vars). Prefer actors/character so a Nemesis <g> patch resolves to
            // the rider graph it actually targets; the creature/precompiled path resolves by its real
            // prefix (leg 1d), NOT this stem map.
            if (sp.find(stem) == sp.end() || actor == "actors/character") {
                sp[stem] = prefix;
                ap[stem] = actor;
            }
            m.vanillaGraphStems.insert(stem);
        } else if (u.kind == UK::Character) {
            m.actorCharacters[actorOf(key)].push_back(prefix);
        }
    }
    for (auto& [actor, chars] : m.actorCharacters) {
        std::sort(chars.begin(), chars.end());
        chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
    }
    return m;
}

}  // namespace CB::converter
