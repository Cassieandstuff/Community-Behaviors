#pragma once

#include "FnisListParser.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// FnisConverter — scan FNIS list files and emit a "Community Behaviors.hky" bundle
// containing YAML graph units for the relay layer and FNIS sub-behaviors.
//
// Bundle layout produced:
//
//   <bundle>/
//     meshes/actors/<actor>/behaviors/
//       0_master.hkx/                       (delta layer for the root behavior)
//         data/additive.yaml                (FNIS events + variables)
//         references/CB_FNIS_BFR.yaml       (BehaviorRef → FNIS container)
//         states/CB_FNIS_State.yaml         (state with parents: [Master_Behavior])
//         states/Master_Behavior.yaml       (wildcard additions)
//         transitions/CB_BlendIn.yaml       (transition effects)
//         transitions/CB_BlendOut.yaml
//       FNIS_anims/
//         FNIS_<actor>.hkx/                 (FNIS container graph unit, ≤30k clips)
//           behavior.yaml
//           data/graphdata.yaml
//           generators/FNIS_IdlePose.yaml
//           clips/*.yaml
//           states/*.yaml
//           transitions/*.yaml
//     animationnames/<character>.txt         (animation roster additions)
//
// Pure std + the existing FNIS parser.  No havok-core link required — the Resolver
// compiles the YAML to HKX at runtime.
namespace CommunityBehaviors::fnis {

using LogFn = std::function<void(const std::string&)>;

struct ConvertResult {
    bool                      ok = false;
    std::string               error;
    std::size_t               filesWritten = 0;
    std::size_t               animCount    = 0;
    std::size_t               eventCount   = 0;
    std::size_t               varCount     = 0;
    std::vector<std::string>  warnings;
};

// Scan one or more animation directories for FNIS list files and emit a merged
// FNIS.hky bundle at `bundlePath`.  Multiple dirs = multiple MO2 mods; their
// list files are merged into one combined ScanResult before emission.
//
// animationsDirs : each is an actor's animations/ folder (one per mod that has FNIS content)
// actor          : actor type folder name, e.g. "character"
// characterStems : hkbCharacterStringData stems to drop the roster for (animationnames/<stem>.txt).
//                  The humanoid character is BOTH projects — pass {"defaultmale","defaultfemale"};
//                  the runtime folds the roster AND derives the animationdata per stem.
ConvertResult ConvertFnis(const std::vector<std::filesystem::path>& animationsDirs,
                          const std::string&                         actor,
                          const std::vector<std::string>&            characterStems,
                          const std::filesystem::path&               bundlePath,
                          const LogFn&                               log = nullptr);

} // namespace CommunityBehaviors::fnis
