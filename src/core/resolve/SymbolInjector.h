#pragma once

// SymbolInjector — compile-time behavior-symbol injection, the static equivalent
// of Behavior Data Injector (BDI).
//
// BDI is a runtime SKSE plugin that appends behavior events/variables to a graph
// after it loads — a workaround for the fact that, pre-Behavior-Relay, nobody
// could recompile a graph's symbol table. BR *does* recompile it, so the same job
// becomes a compile-time union: read the declarations, merge them into the graph's
// hkbBehaviorGraphData before it serializes (the cb.bdi-symbols feature), and the symbols are simply *in* the
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

#include <feature-sdk/IGraphFeature.h>   // CB::feature::ISymbolDecls / SymbolDecl

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace CB {

    /// @brief Loads the BDI declarations and serves the ones scoped to a graph to the `cb.bdi-symbols` feature.
    class SymbolInjector final : public CB::feature::ISymbolDecls {
    public:
        // Scan the given config dirs (via the CB::config scanner) and parse every declaration.
        // Idempotent — replaces any prior load.
        void Load(const std::vector<std::filesystem::path>& configDirs);

        /// @brief The declarations whose projectPath scopes to `graphKey`'s project, in load order.
        std::vector<CB::feature::SymbolDecl> For(std::string_view graphKey) const override;

        std::size_t DeclCount() const { return m_decls.size(); }

    private:
        struct Decl {
            std::string             project;   ///< normalized lowercase '/'-form, e.g. "actors/character"
            CB::feature::SymbolDecl symbol;
        };
        std::vector<Decl> m_decls;
    };

}  // namespace CB
