#include <compat/Registry.h>

namespace CB::engine::compat {

    namespace {

        using A = Action;
        using K = Action::Kind;

        constexpr std::string_view kTdmDlls[]       = { "TrueDirectionalMovement.dll" };
        constexpr std::string_view kSmoothCamDlls[] = { "SmoothCam.dll" };
        constexpr std::string_view kOarDlls[]       = { "OpenAnimationReplacer.dll" };
        constexpr std::string_view kAmrDlls[]       = { "AnimationMotionRevolution.dll" };
        constexpr std::string_view kTkDodgeDlls[]   = { "TK_Dodge_RE.dll" };
        constexpr std::string_view kDmcoDlls[]      = { "DodgeFramework.dll" };
        constexpr std::string_view kBfcoDlls[]      = { "BFCO.dll" };
        constexpr std::string_view kMcoDlls[]       = { "MCO.dll" };

        constexpr A kTdmActions[] = {
            { K::DisableFeature, "camera" },
            { K::DisableFeature, "char_movement_model" },
            { K::DisableFeature, "horse_movement_model" },
            { K::Notice, "It drives the same third-person camera and turns the body. CB's camera and its stick-relative "
                         "movement stay off." },
        };
        constexpr A kSmoothCamActions[] = {
            { K::DisableFeature, "camera" },
            { K::Notice, "It drives the same third-person camera. CB's camera stays off." },
        };
        constexpr A kOarActions[] = {
            { K::DisableFeature, "animation64_readers" },
            { K::DisableFeature, "animation64_loader" },
            { K::DisableFeature, "animation64_watch" },
            { K::Notice, "CB's animation hooks collide with its. Animation 64 stays off." },
        };
        constexpr A kAmrActions[] = {
            { K::Notice, "CB already does what it does. Disable it." },
        };
        constexpr A kTkDodgeActions[] = {
            { K::Notice, "CB is getting its own dodge system, which replaces this one." },
        };
        constexpr A kDmcoActions[] = {
            { K::Notice, "CB is getting its own dodge system, which replaces this one. It also needs Open Animation Replacer." },
        };
        constexpr A kBfcoActions[] = {
            { K::Notice, "Combat frameworks are not supported. It also needs Open Animation Replacer." },
        };
        constexpr A kMcoActions[] = {
            { K::Notice, "Combat frameworks are not supported." },
        };

        constexpr Entry kEntries[] = {
            { "tdm",       "True Directional Movement",    kTdmDlls,       Severity::Incompatible, kTdmActions },
            { "smoothcam", "SmoothCam",                    kSmoothCamDlls, Severity::Incompatible, kSmoothCamActions },
            { "oar",       "Open Animation Replacer",      kOarDlls,       Severity::Incompatible, kOarActions },
            { "amr",       "Animation Motion Revolution",  kAmrDlls,       Severity::Warn,         kAmrActions },
            { "tkdodge",   "TK Dodge RE",                  kTkDodgeDlls,   Severity::Incompatible, kTkDodgeActions },
            { "dmco",      "Dodge MCO-DXP",                kDmcoDlls,      Severity::Incompatible, kDmcoActions },
            { "bfco",      "BFCO",                         kBfcoDlls,      Severity::Incompatible, kBfcoActions },
            { "mco",       "MCO",                          kMcoDlls,       Severity::Incompatible, kMcoActions },
        };

    }  // namespace

    std::span<const Entry> Entries()
    {
        return kEntries;
    }

    std::vector<Finding> Match(const Probe& present)
    {
        std::vector<Finding> found;
        for (const Entry& entry : kEntries) {
            Finding finding{ &entry, {} };
            for (const std::string_view dll : entry.dlls)
                if (present(dll)) finding.dlls.push_back(dll);
            if (!finding.dlls.empty()) found.push_back(std::move(finding));
        }
        return found;
    }

    std::string_view SeverityName(Severity severity)
    {
        switch (severity) {
        case Severity::Note: return "note";
        case Severity::Warn: return "warn";
        case Severity::Incompatible: return "incompatible";
        }
        return "?";
    }

}  // namespace CB::engine::compat
