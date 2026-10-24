#include <merge/LoadPlan.h>

#include <codec/resource/PathKey.h>
#include <interface/MasterTable.h>

#include <algorithm>

namespace CB::engine::merge {

    LoadPlan PlanLoadOrder(const std::unordered_map<std::string, std::vector<std::string>>& masters,
                           const codec::LoadOrder&                                         order)
    {
        LoadPlan   plan;
        const bool haveSkyrim = masters.find("skyrim") != masters.end();

        /// @note Declared masters are EXPLICIT (a missing one drops the dependent); "skyrim" is added IMPLICITLY later.
        std::unordered_map<std::string, std::vector<std::string>> explicitMasters;
        for (const auto& [stem, declared] : masters) {
            auto& ems = explicitMasters[stem];
            for (const auto& m : declared) {
                const std::string mk = codec::resource::PathKey(m);
                if (mk == stem) continue;
                if (std::find(ems.begin(), ems.end(), mk) == ems.end()) ems.push_back(mk);
            }
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& [stem, ems] : explicitMasters) {
                if (plan.dropped.count(stem)) continue;
                for (const std::string& m : ems) {
                    const bool absent  = masters.find(m) == masters.end();
                    const bool dropped = plan.dropped.count(m) != 0;
                    if (absent || dropped) {
                        plan.warnings.push_back("skipping bundle '" + stem + "': declared master '" + m + "' is " +
                                                (absent ? "not installed" : "itself skipped") +
                                                " (its overrides would bind to nothing)");
                        plan.dropped.insert(stem);
                        changed = true;
                        break;
                    }
                }
            }
        }

        std::unordered_map<std::string, int>                      indeg;
        std::unordered_map<std::string, std::vector<std::string>> children;
        std::vector<std::string>                                  survivors;
        for (const auto& [stem, declared] : masters) {
            (void)declared;
            if (plan.dropped.count(stem)) continue;
            survivors.push_back(stem);
            indeg.emplace(stem, 0);
        }
        for (const std::string& s : survivors)
            plan.masterTable[s] = codec::formid::BuildMasterTable(s, explicitMasters[s], haveSkyrim);
        for (const std::string& s : survivors) {
            std::vector<std::string> ms = explicitMasters[s];
            if (haveSkyrim && s != "skyrim" && std::find(ms.begin(), ms.end(), "skyrim") == ms.end()) ms.push_back("skyrim");
            for (const std::string& m : ms) {
                if (plan.dropped.count(m) || indeg.find(m) == indeg.end()) continue;
                children[m].push_back(s);
                ++indeg[s];
            }
        }

        /// @note Among ready bundles: the lowest loadorder.txt value first (unlisted = 0), then by stem, for determinism.
        const auto readyLess = [&](const std::string& a, const std::string& b) {
            const int la = order.Rank(a), lb = order.Rank(b);
            return la != lb ? la < lb : a < b;
        };
        std::vector<std::string> ready;
        for (const std::string& s : survivors)
            if (indeg[s] == 0) ready.push_back(s);
        std::sort(ready.begin(), ready.end(), readyLess);

        int nextRank = 0;
        while (!ready.empty()) {
            const std::string s = ready.front();
            ready.erase(ready.begin());
            plan.rank[s] = nextRank++;
            std::vector<std::string> freed;
            for (const std::string& c : children[s])
                if (--indeg[c] == 0) freed.push_back(c);
            for (const std::string& c : freed) ready.insert(std::lower_bound(ready.begin(), ready.end(), c, readyLess), c);
        }

        if (plan.rank.size() != survivors.size()) {
            std::vector<std::string> cyc;
            for (const std::string& s : survivors)
                if (!plan.rank.count(s)) cyc.push_back(s);
            std::sort(cyc.begin(), cyc.end(), readyLess);
            std::string names;
            for (const std::string& s : cyc) names += (names.empty() ? "" : ", ") + s;
            plan.warnings.push_back("master cycle among [" + names +
                                    "]; falling back to loadorder.txt order for them (masters can't resolve a cycle)");
            for (const std::string& s : cyc) plan.rank[s] = nextRank++;
        }
        return plan;
    }

}  // namespace CB::engine::merge
