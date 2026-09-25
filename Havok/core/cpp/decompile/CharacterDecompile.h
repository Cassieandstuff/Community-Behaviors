#pragma once
// CharacterDecompile — schema-native character decompile (the character/project port). A compiled
// character .hkx's root is hkbCharacterData -> the character source tree (character.yaml +
// data/animations.yaml + properties/*.yaml + _order.txt + foot_ik.yaml + mirror.yaml) that
// CharacterYamlLoader reads back. The schema peer of havok-core's typed CharacterDecompiler::
// decompileCharacter — reads the generic havok-io SchemaObject graph by FIELD NAME (no typed hkb*
// classes), emitting byte-for-byte the same tree (gated vs the typed emitter over the vanilla corpus).
//
// The COMPILE peer already exists and is schema (AssembleCharacter + CharacterYamlLoader), so this
// closes the character round-trip havok-core-free. Behaviors/skeletons/animations already have their
// schema decompile; character was the last graph type on the typed path.

#include <codec/serialization/HavokIo.h>   // io::SchemaObject

#include <filesystem>
#include <string>

namespace havok::decompile {

struct CharDecompileResult { bool ok = false; std::string error; };

// Decompile a deserialized hkbCharacterData SchemaObject `cd` into the character source tree under
// `dir`. Byte-identical to the typed decompileCharacter. Returns ok=false with a message on a
// structural problem (e.g. missing stringData).
CharDecompileResult DecompileCharacterSchema(const io::SchemaObject& cd, const std::filesystem::path& dir);

} // namespace havok::decompile
