#include "PCH.h"
#include "SymbolInjector.h"

#include <sct-config/SctConfig.h>

#include <bit>
#include <cctype>
#include <unordered_set>

namespace CB {

    namespace {

        std::string ToLower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // BDI projectPath ("Actors\\Character") -> normalized lowercase '/'-form.
        std::string NormalizeProject(std::string p)
        {
            for (char& c : p) if (c == '\\') c = '/';
            return ToLower(std::move(p));
        }

        // The project a served graph belongs to:
        //   "meshes/actors/character/behaviors/1hm_behavior.hkx" -> "actors/character"
        std::string ProjectOfKey(std::string_view keyView)
        {
            std::string k = ToLower(std::string(keyView));
            for (char& c : k) if (c == '\\') c = '/';
            if (k.rfind("data/", 0) == 0)   k.erase(0, 5);
            if (k.rfind("meshes/", 0) == 0) k.erase(0, 7);
            if (const auto pos = k.find("/behaviors"); pos != std::string::npos) k.erase(pos);
            return k;
        }

        // `decl` scopes `graph` if it is a segment-prefix: "actors" matches
        // "actors/character"; "actors/character" matches itself; "actors/horse"
        // does not match "actors/character".
        bool ProjectMatches(const std::string& decl, const std::string& graph)
        {
            if (decl.empty()) return false;
            if (decl == graph) return true;
            return graph.size() > decl.size()
                && graph.compare(0, decl.size(), decl) == 0
                && graph[decl.size()] == '/';
        }

    }  // namespace

    void SymbolInjector::Load(const std::vector<std::filesystem::path>& configDirs)
    {
        m_decls.clear();

        sct::config::JsonBatch batch = sct::config::LoadJson({
            .roots      = configDirs,
            .extensions = { ".json" },
            .recursive  = false,
        });

        for (const auto& err : batch.errors)
            LOG_WARN("SymbolInjector: JSON error in '{}': {}",
                err.source.path.filename().string(), err.message);

        std::size_t nEvent = 0, nVar = 0, nSkip = 0;
        for (const auto& doc : batch.docs) {
            if (!doc.json.is_array()) {
                LOG_WARN("SymbolInjector: '{}' is not a JSON array — skipping.",
                    doc.source.path.filename().string());
                continue;
            }
            for (const auto& entry : doc.json) {
                if (!entry.is_object()) { ++nSkip; continue; }

                const std::string type    = entry.value("type", std::string{});
                const std::string name    = entry.value("name", std::string{});
                std::string       project = NormalizeProject(entry.value("projectPath", std::string{}));
                if (type.empty() || name.empty() || project.empty()) { ++nSkip; continue; }

                Decl d;
                d.project = std::move(project);
                d.name    = name;

                if (type == "kEvent") {
                    d.isEvent = true;
                    ++nEvent;
                } else if (type == "kInt" || type == "kBool" || type == "kFloat") {
                    // value is a raw 4-byte word: bool -> 0/1, int -> as-is, float ->
                    // the bit pattern of the parsed float (matches Engine Relay's
                    // ConfigLoader; std::stoi-style truncation would corrupt floats).
                    try {
                        if (type == "kBool")      { d.varType = "VARIABLE_TYPE_BOOL";  d.value = entry.value("value", false) ? 1 : 0; }
                        else if (type == "kInt")  { d.varType = "VARIABLE_TYPE_INT32"; d.value = entry.value("value", 0); }
                        else /* kFloat */         { d.varType = "VARIABLE_TYPE_REAL";  d.value = std::bit_cast<std::int32_t>(entry.value("value", 0.0f)); }
                    } catch (const std::exception&) {
                        LOG_WARN("SymbolInjector: '{}' — bad value for '{}' — defaulting to 0.",
                            doc.source.path.filename().string(), name);
                        d.value = 0;
                    }
                    ++nVar;
                } else {
                    LOG_WARN("SymbolInjector: '{}' — unknown type '{}' for '{}' — skipping.",
                        doc.source.path.filename().string(), type, name);
                    ++nSkip;
                    continue;
                }

                m_decls.push_back(std::move(d));
            }
        }

        LOG_INFO("SymbolInjector: {} declaration(s) ({} event, {} variable, {} skipped).",
            m_decls.size(), nEvent, nVar, nSkip);
    }

    std::size_t SymbolInjector::InjectInto(havok::model::BehaviorGraphDataDef& gd,
                                           std::string_view serveKey) const
    {
        if (m_decls.empty()) return 0;
        const std::string project = ProjectOfKey(serveKey);

        // Names already declared by the graph (vanilla/base + any lower layer).
        // Owning copies, not string_view — we push into these very vectors below.
        std::unordered_set<std::string> haveEvents, haveVars;
        for (const auto& e : gd.events)    haveEvents.insert(e.name);
        for (const auto& v : gd.variables) haveVars.insert(v.name);

        std::size_t added = 0;
        for (const auto& d : m_decls) {
            if (!ProjectMatches(d.project, project)) continue;
            if (d.isEvent) {
                if (haveEvents.insert(d.name).second) {
                    havok::model::EventInfoDef e;
                    e.name = d.name;              // flags default "0"
                    gd.events.push_back(std::move(e));
                    ++added;
                }
            } else {
                if (haveVars.insert(d.name).second) {
                    havok::model::VariableInfoDef v;
                    v.name  = d.name;
                    v.type  = d.varType;
                    v.value = d.value;            // role / roleFlags default
                    gd.variables.push_back(std::move(v));
                    ++added;
                }
                // else: the graph already declares this name — leave it (its index is
                // already wired); the base is authoritative over a mod re-declaration.
            }
        }
        return added;
    }

}  // namespace CB
