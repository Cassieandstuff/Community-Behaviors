#pragma once

#include "FnisListParser.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

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

/// @brief Scan actors' animations/ folders (one per MO2 mod with FNIS content) for FNIS list files, for a merged
///        FNIS.hky bundle at `bundlePath`.
/// @note  Emission is not yet on the FormID .hky format (docs/bugs CB-7): a load order WITH FNIS animations fails
///        (`ok` false, `error` says why); one without succeeds with nothing written.
/// @param characterStems the hkbCharacterStringData stems whose roster the animations join — the humanoid character
///        is BOTH projects, {"defaultmale","defaultfemale"}.
ConvertResult ConvertFnis(const std::vector<std::filesystem::path>& animationsDirs,
                          const std::string&                         actor,
                          const std::vector<std::string>&            characterStems,
                          const std::filesystem::path&               bundlePath,
                          const LogFn&                               log = nullptr);

} // namespace CommunityBehaviors::fnis
