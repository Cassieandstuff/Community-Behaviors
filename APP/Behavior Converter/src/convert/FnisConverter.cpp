#include "FnisConverter.h"

#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace CommunityBehaviors::fnis {
namespace {

void Say(const LogFn& log, const std::string& s) { if (log) log(s); }

}  // namespace

ConvertResult ConvertFnis(const std::vector<fs::path>&                     animationsDirs,
                          const std::string&                               actor,
                          [[maybe_unused]] const std::vector<std::string>& characterStems,
                          [[maybe_unused]] const fs::path&                 bundlePath,
                          const LogFn&                                     log)
{
    ConvertResult result;

    ScanResult scan;
    scan.actor = actor;
    for (const auto& dir : animationsDirs) {
        Say(log, "FNIS converter: scanning " + dir.string());
        std::vector<std::string> parseWarnings;
        ScanResult partial = ScanForLists(dir, actor, &parseWarnings);
        for (auto& w : parseWarnings) result.warnings.push_back(std::move(w));
        for (auto& lf : partial.lists) {
            Say(log, "  " + lf.modName + ": " + std::to_string(lf.anims.size()) + " anims");
            scan.lists.push_back(std::move(lf));
        }
    }
    if (scan.lists.empty()) {
        result.ok = true;
        Say(log, "  No FNIS list files found.");
        return result;
    }

    const std::vector<AnimGroup> groups = GroupAnimations(scan);
    if (groups.empty()) {
        result.ok = true;
        Say(log, "  No animation groups after grouping.");
        return result;
    }
    result.error = std::to_string(scan.lists.size()) + " FNIS list file(s), " + std::to_string(groups.size()) +
                   " animation group(s) found, but FNIS conversion is not yet on the FormID .hky format — "
                   "skipped (docs/bugs CB-7).";
    return result;
}

}  // namespace CommunityBehaviors::fnis
