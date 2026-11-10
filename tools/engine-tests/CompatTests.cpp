/// @file
/// @brief The compatibility registry's table and matching.

#include "TestSupport.h"

#include <compat/Registry.h>

#include <set>
#include <string>
#include <vector>

namespace CB::engine::tests {

namespace {

namespace reg = compat;

const reg::Finding* Find(const std::vector<reg::Finding>& found, std::string_view id) {
    for (const reg::Finding& f : found)
        if (f.entry->id == id) return &f;
    return nullptr;
}

void TableChecks() {
    std::set<std::string_view> ids, dlls;
    for (const reg::Entry& e : reg::Entries()) {
        const std::string who(e.id);
        Check(!e.id.empty() && !e.name.empty(), who + ": has an id and a name");
        Check(ids.insert(e.id).second, who + ": the id is unique");
        Check(!e.dlls.empty(), who + ": names at least one DLL");
        Check(!e.actions.empty(), who + ": does something when found");
        for (const std::string_view dll : e.dlls) {
            Check(dll.size() > 4 && dll.substr(dll.size() - 4) == ".dll", who + ": '" + std::string(dll) + "' is a .dll file name");
            Check(dll.find_first_of("/\\") == std::string_view::npos, who + ": '" + std::string(dll) + "' is a bare file name");
            Check(dlls.insert(dll).second, who + ": '" + std::string(dll) + "' belongs to one entry only");
        }
        bool notice = false;
        for (const reg::Action& a : e.actions) {
            Check(!a.text.empty(), who + ": an action has its text");
            notice = notice || a.kind == reg::Action::Kind::Notice;
        }
        Check(notice, who + ": tells the user, not only acts");
        if (e.severity == reg::Severity::Incompatible)
            Check(!reg::SeverityName(e.severity).empty(), who + ": severity has a name");
    }
}

void MatchChecks() {
    Check(reg::Match([](std::string_view) { return false; }).empty(), "nothing present, nothing found");

    const auto only = [](std::string_view want) {
        return [want](std::string_view dll) { return dll == want; };
    };

    const auto tdm = reg::Match(only("TrueDirectionalMovement.dll"));
    Check(tdm.size() == 1 && tdm[0].entry->id == "tdm", "True Directional Movement's DLL finds its entry");
    bool disablesCamera = false;
    for (const reg::Action& a : tdm.empty() ? std::span<const reg::Action>{} : tdm[0].entry->actions)
        disablesCamera = disablesCamera || (a.kind == reg::Action::Kind::DisableFeature && a.text == "camera");
    Check(disablesCamera, "True Directional Movement switches the camera off");

    const auto both = reg::Match([](std::string_view dll) { return dll == "BFCO.dll" || dll == "OpenAnimationReplacer.dll"; });
    Check(both.size() == 2 && Find(both, "bfco") && Find(both, "oar"), "two present mods find two entries");

    /// @note The probe gets the registry's own spelling, byte for byte: a case-folded name would never match here.
    Check(reg::Match(only("truedirectionalmovement.dll")).empty(), "matching never folds case: the probe decides");
}

}  // namespace

void RunCompatTests() {
    TableChecks();
    MatchChecks();
}

}  // namespace CB::engine::tests
