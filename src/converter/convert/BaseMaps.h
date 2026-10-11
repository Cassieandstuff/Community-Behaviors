#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CB::engine::codec { class HkyArchive; }

namespace CB::converter {

/// @brief Where each vanilla graph and character lives, read from the base archive (the shipped Skyrim.hky) — what the
///        conversion resolves a Nemesis patch's graph stem against, since Pandora/Nemesis leave the owning actor implicit.
struct BaseMaps {
    // third-person behavior graph stem -> its real meshes-relative serve path + actor path.
    std::unordered_map<std::string, std::string> graphServePath;  // "horsebehavior" -> "meshes/actors/horse/behaviors/horsebehavior.hkx"
    std::unordered_map<std::string, std::string> graphActorPath;  // "horsebehavior" -> "actors/horse"
    // first-person behavior graph stem -> real serve path + actor (separate namespace: _1stperson/behaviors).
    std::unordered_map<std::string, std::string> fpGraphServePath;
    std::unordered_map<std::string, std::string> fpGraphActorPath;
    // actor path -> the character unit serve path(s) under it (defaultmale + defaultfemale for
    // actors/character incl. the "characters female/" one; a single "horse" for actors/horse).
    std::unordered_map<std::string, std::vector<std::string>> actorCharacters;  // "actors/horse" -> ["meshes/actors/horse/characters/horse.hkx"]
    std::unordered_set<std::string>                           vanillaGraphStems;  // behavior stems present in base (derive-vs-own-new)
};

BaseMaps BuildBaseMaps(const CB::engine::codec::HkyArchive& baseArc);

/// @brief "meshes/actors/horse/behaviors/horsebehavior.hkx" -> "actors/horse" (handles the space-folder variants and
///        _1stperson).
std::string ActorOfServePath(const std::string& servePathLower);

}  // namespace CB::converter
