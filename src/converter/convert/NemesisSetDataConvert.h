#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// NemesisSetDataConvert — convert legacy Nemesis patch-form animationsetdata (plus the
// hkbCharacterStringData animation roster) from one or more mod dirs into a clean
// split-form .hky bundle, merging across mods in load order (later wins). The offline
// set-data half of the converter (ConvertLoadOrder merges every owning code's set-data dir
// in one call). Depends only on AnimationSetData (pure std) + std::filesystem — no SKSE/PCH.
namespace CommunityBehaviors::asd {

struct NemesisConvertStats {
    std::size_t projects  = 0;   // projects that contributed >=1 emitted set delta
    std::size_t sets      = 0;   // split-form set files written
    std::size_t attacks   = 0;   // attack entries across written sets
    std::size_t crcs      = 0;   // CRC triples across written sets
    std::size_t charFiles = 0;   // animationnames/<char>.txt written
    std::size_t animNames = 0;   // roster names emitted (deduped per character)
    std::size_t fails     = 0;   // per-file parse/write failures (see log)
};

// Convert `modDirs` (each a Nemesis mod folder containing an animationsetdatasinglefile\
// subtree), lowest priority first, into split form under `bundle`:
//   <bundle>/meshes/animationsetdata/<proj>data/<set>.txt   (delta-only "V3" form)
//   <bundle>/animationnames/<char>.txt                      (roster delta, unioned per char)
// `log`, if set, receives human-readable progress lines. Never throws — per-file parse
// errors are counted in .fails and reported through `log`.
NemesisConvertStats ConvertNemesisSetData(const std::vector<std::filesystem::path>&     modDirs,
                                          const std::filesystem::path&                  bundle,
                                          const std::function<void(const std::string&)>& log = {});

}  // namespace CommunityBehaviors::asd
