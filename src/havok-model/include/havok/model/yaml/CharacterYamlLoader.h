#pragma once
// CharacterYamlLoader — load a character source directory into CharacterData.
// The dir carries: character.yaml, animations.txt, properties/ (+ _order.txt),
// foot_ik.yaml, mirror.yaml; skeleton bone names come from the nearest
// "character assets/skeleton.yaml" walking upward. Port of HKBuild's
// CharacterReader + SkeletonReader. ryml-gated (VS/vcpkg build only).

#include "havok/model/defs/CharacterDefs.h"
#include "havok/model/yaml/UnitSource.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace havok::model {

struct CharacterYamlLoader {
    // Throws std::runtime_error on missing required files / parse failure.
    static CharacterData Load(const std::filesystem::path& dir);

    // Record-level merge, mirroring YamlBehaviorLoader::LoadMerged: load `dirs` in
    // priority order (base first) into one CharacterData. The base (first dir) provides
    // the full character (rig / properties / footIk / mirror); each later layer UNIONS
    // its animationNames additions (dedup, case-insensitive, base order preserved) so the
    // served character carries the complete roster the merged set-data references — no
    // runtime animationNames injection needed. Only the base must be a full character
    // unit; a mod delta may ship animations.txt alone.
    static CharacterData LoadMerged(const std::vector<std::string>& dirs);

    // IUnitSource overloads — the SAME loaders over abstract unit sources instead of
    // on-disk directories (the disk overloads above wrap each path in a DiskUnitSource
    // and forward here). Lets BR read a character straight from a packed .hky in memory
    // (ZipUnitSource) with no filesystem extraction. The skeleton (`character assets/
    // skeleton.yaml`, needed only for `named` bone-weight maps) is resolved THROUGH the
    // source — DiskUnitSource walks the vanilla ancestor convention, an in-memory source
    // packages it unit-relative — exactly as YamlBehaviorLoader does. `sources` are in
    // priority order (base first); only the base must be a full character unit.
    static CharacterData Load(const IUnitSource& unit);
    static CharacterData LoadMerged(const std::vector<std::shared_ptr<const IUnitSource>>& sources);
};

} // namespace havok::model
