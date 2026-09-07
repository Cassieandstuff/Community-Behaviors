// Compile-check for the CB-API facade — ensures <CB-API.h> and every name it re-exports actually
// resolves against the engine modules. Not shipped; built by the cb-api-check target so a plain
// build fails loudly if the public contract drifts. This TU is Community Behaviors acting as a
// FIRST-PARTY CONSUMER of its own public API — the same one door a downstream tool uses.
#include <CB-API.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {
    // Odr-use each re-exported name so a missing/renamed underlying type or function is a compile
    // error here, not a silent gap a downstream tool discovers. Never executed.
    [[maybe_unused]] void cb_api_contract() {
        // cb::resolve — read a bundle + merge a load order → the resolved model
        std::string err;
        std::shared_ptr<cb::resolve::Archive> arc = cb::resolve::Archive::LoadFromFile("", err);
        (void)arc;
        cb::resolve::ResolvedGraph g = cb::resolve::LoadOrder::LoadMerged(std::vector<std::string>{});
        (void)g;
        cb::resolve::UnitKind k = cb::resolve::UnitKind::Behavior; (void)k;
        const cb::resolve::UnitSource* src = nullptr; (void)src;

        // cb::schema — the registry
        cb::schema::Registry reg; (void)reg;

        // cb::anim — decode / compile / export
        cb::anim::Def def; (void)def;
        cb::anim::Def loaded = cb::anim::YamlLoader::LoadFromString("", "<check>"); (void)loaded;
        cb::anim::CompileResult   cr = cb::anim::CompileAnimation(def);            (void)cr;
        cb::anim::DecompileResult dr = cb::anim::DecompileAnimation(std::vector<std::uint8_t>{},
                                                                   std::filesystem::path{});
        (void)dr;
        auto* toFile = &cb::anim::CompileAnimationToFile; (void)toFile;   // odr-use the file variant too
    }
}
