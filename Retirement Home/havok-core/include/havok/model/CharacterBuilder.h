#pragma once
// CharacterBuilder — CharacterData -> hkbCharacterData object graph (rooted in
// hkRootLevelContainer). Retarget of HKBuild's CharacterXmlEmitter to construct
// Tier-A objects directly. Pure C++ (no ryml). The serializer decides object
// ordering, so this only builds the graph.

#include "havok/classes/Classes.h"
#include "havok/classes/gen/ClassesGen.h"   // hkbCharacterData / StringData / etc.
#include "havok/model/defs/CharacterDefs.h"

#include <memory>

namespace havok::model {

class CharacterBuilder {
public:
    explicit CharacterBuilder(const CharacterData& data) : _d(data) {}
    std::shared_ptr<hkRootLevelContainer> Build();

private:
    const CharacterData& _d;
    std::shared_ptr<hkbBoneWeightArray> buildBoneWeights(const CharBoneWeightsDef&);
    int findBone(const std::string& name) const;
};

} // namespace havok::model
