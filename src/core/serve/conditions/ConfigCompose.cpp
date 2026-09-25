// ConfigCompose.cpp — parse condition configs (YAML) + compose them into ConditionInstances.
// Inc 2a: the runtime half (register gate feeders). Set-emission into the served asdsf is Inc 2b
// (AnimationSetDataServer); discovery/wiring is Inc 2c. Until a set gated on our gate name exists in
// the asdsf, registered instances feed a gate nobody reads (harmless).
#include "ConfigModel.h"
#include "Conditions.h"
#include "RymlInclude.h"
#include <PluginLogger.h>

#include <cstdio>
#include <format>
#include <unordered_map>

namespace CB::conditions {

    std::uint32_t ConfigId(std::string_view id) {
        std::uint32_t h = 2166136261u;                       // FNV-1a/32
        for (char c : id) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
        return h;
    }

    std::string GateNameForConfig(std::string_view id) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "CBCond_%08X", ConfigId(id));
        return std::string(buf);
    }

    bool ParseConditionConfig(std::string_view yaml, ConditionConfig& out, std::string& err) {
        std::string storage(yaml);   // parse_in_place needs a mutable buffer
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        auto root = tree.rootref();
        if (!root.readable() || !root.is_map()) { err = "config root is not a map"; return false; }

        const auto str = [&](const char* k, std::string& dst) {
            const auto key = c4::to_csubstr(k);
            if (root.has_child(key) && root[key].has_val()) c4::from_chars(root[key].val(), &dst);
        };
        str("id", out.id);
        str("when", out.when);
        str("target", out.target);

        if (const auto ek = c4::to_csubstr("enabled"); root.has_child(ek) && root[ek].has_val()) {
            std::string b; c4::from_chars(root[ek].val(), &b);
            out.enabled = !(b == "false" || b == "0" || b == "no");
        }
        if (const auto rk = c4::to_csubstr("replace"); root.has_child(rk)) {
            for (auto rn : root[rk]) {
                ReplaceRule r;
                if (rn.has_child(c4::to_csubstr("from")) && rn[c4::to_csubstr("from")].has_val())
                    c4::from_chars(rn[c4::to_csubstr("from")].val(), &r.from);
                if (rn.has_child(c4::to_csubstr("to")) && rn[c4::to_csubstr("to")].has_val())
                    c4::from_chars(rn[c4::to_csubstr("to")].val(), &r.to);
                if (!r.from.empty()) out.replace.push_back(std::move(r));
            }
        }
        if (out.id.empty())   { err = "config missing 'id'";   return false; }
        if (out.when.empty()) { err = "config missing 'when'"; return false; }
        return true;
    }

    ComposeStats ComposeConfigs(std::span<const ConditionConfig> ordered, std::string& log) {
        ClearConditionInstances();
        // Per-config toggle store: unordered_map nodes give stable bool addresses for instance.enabled.
        // (User-toggle persistence across sessions is a later §8 item; default here from config.enabled.)
        static std::unordered_map<std::uint32_t, bool> s_toggles;
        s_toggles.clear();

        // Harvest + liveness by id: later config (higher load-order priority) overrides earlier.
        std::unordered_map<std::string, const ConditionConfig*> live;
        for (const ConditionConfig& c : ordered) live[c.id] = &c;

        ComposeStats st;
        st.total = static_cast<int>(live.size());
        for (auto& [id, cfg] : live) {
            std::string cerr;
            CompiledExpr expr = Compile(cfg->when, cerr);
            if (!expr.Valid()) {
                ++st.failed;
                log += std::format("  [skip] '{}' — when: {}\n", id, cerr);
                continue;
            }
            const std::uint32_t cid = ConfigId(id);
            const bool* enabledPtr = &(s_toggles[cid] = cfg->enabled);

            ConditionInstance inst;
            inst.gateName = RE::BSFixedString(GateNameForConfig(id).c_str());
            inst.expr     = std::move(expr);
            inst.enabled  = enabledPtr;
            inst.configId = cid;
            RegisterConditionInstance(std::move(inst));
            ++st.registered;
        }
        LOG_INFO("[cond] composed {} config(s): {} registered, {} failed.", st.total, st.registered, st.failed);
        return st;
    }

}  // namespace CB::conditions
