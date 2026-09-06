#pragma once
// CharacterCompiler — CharacterData -> validated character .hkx, mirroring
// BehaviorCompiler.

#include "havok/core/PackFileTypes.h"       // HKXHeader
#include "havok/model/defs/CharacterDefs.h" // CharacterData
#include "havok/sct/BehaviorCompiler.h"     // CompileResult

#include <filesystem>

namespace havok::sct {

CompileResult CompileCharacter(const model::CharacterData& data,
                               const HKXHeader& header = HKXHeader::SkyrimSE());

CompileResult CompileCharacterToFile(const model::CharacterData&  data,
                                     const std::filesystem::path& outPath,
                                     bool                         validate = true,
                                     const HKXHeader&             header = HKXHeader::SkyrimSE());

} // namespace havok::sct
