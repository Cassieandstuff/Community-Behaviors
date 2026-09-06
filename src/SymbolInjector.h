#pragma once

// SymbolInjector — compile-time behavior-symbol injection, the static equivalent
// of Behavior Data Injector (BDI).
//
// BDI is a runtime SKSE plugin that appends behavior events/variables to a graph
// after it loads — a workaround for the fact that, pre-Behavior-Relay, nobody
// could recompile a graph's symbol table. BR *does* recompile it, so the same job
// becomes a compile-time union: read the declarations, merge them into the graph's
// hkbBehaviorGraphData before CompileBehavior, and the symbols are simply *in* the
// bytes — no runtime append, name->index consistency across the whole project by
// construction (spec §6).
//
// Format is BDI's own, so every mod already shipping a BDI config works unchanged:
// SKSE/Plugins/BehaviorDataInjector/<name>_BDI.json is a flat JSON array of
//     { "projectPath": "actors\\Character", "type": "kInt|kBool|kFloat|kEvent",
//       "name": "MyVar", "value": 123 }
// projectPath scopes a symbol to a behavior project (segment-prefix match, so
// "Actors" broadcasts to every actor project, "actors\\Character" targets one);
// type discriminates variable (kInt/kBool/kFloat, with value) from event (kEvent).

#include <havok/model/defs/BehaviorDef.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace CB {

    class SymbolInjector {
    public:
        // Scan the given config dirs (via sct-config) and parse every declaration.
        // Idempotent — replaces any prior load.
        void Load(const std::vector<std::filesystem::path>& configDirs);

        // Append every declaration whose projectPath scopes to `serveKey`'s project
        // into `gd`, skipping names the graph already declares (the existing one
        // wins — its index is already wired into the compiled graph). Returns the
        // number of symbols actually added.
        std::size_t InjectInto(havok::model::BehaviorGraphDataDef& gd,
                               std::string_view serveKey) const;

        std::size_t DeclCount() const { return m_decls.size(); }

    private:
        struct Decl {
            std::string  project;        // normalized lowercase '/'-form, e.g. "actors/character"
            std::string  name;
            bool         isEvent = false;
            std::string  varType;        // VARIABLE_TYPE_* (variables only)
            std::int32_t value = 0;      // raw word value (variables only)
        };
        std::vector<Decl> m_decls;
    };

}  // namespace CB
