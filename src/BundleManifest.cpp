#include "PCH.h"

#include "BundleManifest.h"

#include <sct-config/SctConfig.h>   // sct::config::LoadJson / ReadText

#include <system_error>

namespace CB {

    namespace {

        // A string field, tolerating a JSON number (a bare "version": 1.4 becomes "1.4")
        // so a well-meaning author's un-quoted version doesn't silently vanish.
        std::string AsString(const nlohmann::json& v)
        {
            if (v.is_string()) return v.get<std::string>();
            if (v.is_number() || v.is_boolean()) return v.dump();  // no surrounding quotes
            return {};
        }

    }  // namespace

    BundleManifest BundleManifest::Load(const std::filesystem::path& bundleDir,
                                        const std::string&            bundleStem,
                                        std::vector<std::string>&     warnings)
    {
        const std::filesystem::path file = bundleDir / "manifest.json";
        std::error_code             ec;
        if (!std::filesystem::exists(file, ec))
            return Parse(std::nullopt, bundleStem, warnings);  // no manifest — defaulted + silent

        const auto text = sct::config::ReadText(file);
        if (!text) {
            std::vector<std::string> local;
            BundleManifest m = Parse(std::nullopt, bundleStem, local);  // defaulted identity
            warnings.push_back("could not read " + file.string());
            return m;
        }
        return Parse(*text, bundleStem, warnings);
    }

    BundleManifest BundleManifest::Parse(const std::optional<std::string>& jsonText,
                                         const std::string&                bundleStem,
                                         std::vector<std::string>&         warnings)
    {
        BundleManifest m;
        m.name = bundleStem;   // default identity even when no manifest ships

        if (!jsonText || jsonText->empty())
            return m;  // no manifest — defaulted + silent; the common case until authors add one

        // ignore_comments = true → JSONC (// and /* */) tolerated on our side; allow_exceptions
        // = false → a parse failure yields a discarded value instead of throwing on the hot path.
        const nlohmann::json j =
            nlohmann::json::parse(*jsonText, nullptr, /*allow_exceptions*/ false, /*ignore_comments*/ true);
        if (j.is_discarded()) {
            warnings.push_back("invalid JSON in manifest for '" + bundleStem + "'");
            return m;
        }
        if (!j.is_object()) {
            warnings.push_back("manifest for '" + bundleStem + "' is not a JSON object");
            return m;
        }

        m.present = true;
        if (auto it = j.find("name"); it != j.end()) {
            if (std::string s = AsString(*it); !s.empty()) m.name = std::move(s);
        }
        if (auto it = j.find("version");     it != j.end()) m.version     = AsString(*it);
        if (auto it = j.find("author");      it != j.end()) m.author      = AsString(*it);
        if (auto it = j.find("description"); it != j.end()) m.description = AsString(*it);
        if (auto it = j.find("light"); it != j.end() && it->is_boolean()) m.light = it->get<bool>();
        if (auto it = j.find("masters"); it != j.end() && it->is_array())
            for (const auto& e : *it)
                if (std::string s = AsString(e); !s.empty()) m.masters.push_back(std::move(s));

        // The schema-version stamp + producer chain. schema_version is the gate input; authored_by is
        // a chain (origin first) but a bare string is tolerated for a single-producer bundle.
        if (auto it = j.find("schema_version"); it != j.end()) m.schemaVersion = AsString(*it);
        if (auto it = j.find("authored_by"); it != j.end()) {
            if (it->is_array()) {
                for (const auto& e : *it)
                    if (std::string s = AsString(e); !s.empty()) m.authoredBy.push_back(std::move(s));
            } else if (std::string s = AsString(*it); !s.empty()) {
                m.authoredBy.push_back(std::move(s));
            }
        }

        return m;
    }

}  // namespace CB
