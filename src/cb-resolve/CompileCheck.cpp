// Compile-check for the cb-resolve umbrella — ensures <cb-resolve/CbResolve.h> and every
// alias it exposes actually resolves against the engine modules. Not shipped; built by the
// cb-resolve-check target so a plain build fails loudly if the public contract drifts.
#include <cb-resolve/CbResolve.h>

namespace {
    // Odr-use each alias so a missing/renamed underlying type is a compile error, not silent.
    [[maybe_unused]] void cb_resolve_contract() {
        std::string err;
        std::shared_ptr<cb::resolve::Archive> arc = cb::resolve::Archive::LoadFromFile("", err);
        (void)arc;
        cb::resolve::ResolvedGraph  g   = cb::resolve::LoadOrder::LoadMerged(std::vector<std::string>{});
        (void)g;
        cb::resolve::SchemaRegistry reg;
        (void)reg;
        cb::resolve::UnitKind       k   = cb::resolve::UnitKind::Behavior;
        (void)k;
        const cb::resolve::UnitSource* src = nullptr;
        (void)src;
    }
}
