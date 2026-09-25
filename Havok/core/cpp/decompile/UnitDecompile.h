#pragma once
// UnitDecompile — the schema-native unit decompile dispatcher (the havok-core DecompileToDir peer).
// Deserialize a compiled unit .hkx, detect its root dialect, and decompile it to `outDir` entirely
// through the schema stack — NO typed havok-core:
//   project   -> ReadProject (compile) + EmitProjectYaml (codec/format)
//   animation -> DecompileAnimation (decompile)
//   character -> DecompileCharacterSchema (decompile — character-schema-parity gated 46/46)
//   behavior  -> DecompileBehaviorSchema (decompile, encounter-order ids)
// Shared by the converter + tree-diff (both previously called the typed DecompileToDir). Requires
// havok::schema::SharedRegistry() for the character/behavior graph walk.

#include <cstdint>
#include <string>
#include <vector>

namespace havok::decompile {

struct UnitDecompileResult { bool ok = false; std::string error; std::string kind; };

UnitDecompileResult DecompileUnit(const std::vector<std::uint8_t>& bytes, const std::string& outDir);

} // namespace havok::decompile
