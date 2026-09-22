#include "havok/sct/PatchConverter.h"

// Typed ConvertPatch RETIRED (firesale) — the 965-line typed decompile/convert impl is parked in
// Retirement Home/_archive/. Schema replaced it: ConvertModDelta (convert) + DecompileBehaviorSchema
// (base decompile). This failure-stub only keeps the retired CLI dev/gate verbs (patchconvert /
// patchdelta / vanbase / basefidelity) compiling until they're deleted — no live path calls it.
namespace havok::sct {

PatchConvertResult ConvertPatch(const std::string& /*vanillaBin*/, const std::string& /*vanillaXml*/,
                                const std::vector<std::string>& /*patchDirs*/, const std::string& /*outBin*/,
                                const std::string& /*nativeDeltaDir*/, const std::string& /*vanBaseDir*/) {
    PatchConvertResult r;
    r.error = "typed ConvertPatch retired (firesale) — use the schema path (ConvertModDelta / DecompileBehaviorSchema)";
    return r;
}

}  // namespace havok::sct
