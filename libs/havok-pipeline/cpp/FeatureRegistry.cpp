#include "FeatureRegistry.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace CB::features {

    FeatureRegistry& FeatureRegistry::Instance()
    {
        static FeatureRegistry s_instance;   // Meyers singleton — safe under static-init self-registration
        return s_instance;
    }

    void FeatureRegistry::Register(std::unique_ptr<IGraphFeature> feature)
    {
        if (!feature) return;
        m_byId.insert_or_assign(std::string(feature->Id()), std::move(feature));
    }

    std::vector<std::string>
    FeatureRegistry::ResolveRunOrder(const std::vector<std::string>& enabledIds,
                                     IFeatureLog& log) const
    {
        // Nodes = the enabled ids that are actually registered, de-duped, in first-seen order (the
        // stable tie-break). `pos` keeps that order for both the ready-queue sort and cycle fallback.
        std::vector<std::string>           nodes;
        std::map<std::string, std::size_t> pos;   // id -> input rank
        for (const std::string& id : enabledIds) {
            if (pos.count(id)) continue;
            if (m_byId.find(id) == m_byId.end()) continue;   // unregistered — dropped
            pos.emplace(id, nodes.size());
            nodes.push_back(id);
        }
        const std::set<std::string_view> present(nodes.begin(), nodes.end());

        // Edges a->b == "a must run before b". indeg[b] counts predecessors; succ[a] lists successors.
        std::map<std::string, std::vector<std::string>> succ;
        std::map<std::string, int>                      indeg;
        for (const std::string& n : nodes) indeg[n] = 0;

        const auto addEdge = [&](std::string_view a, std::string_view b) {
            if (!present.count(a) || !present.count(b) || a == b) return;   // ignore out-of-set/self
            succ[std::string(a)].emplace_back(b);
            ++indeg[std::string(b)];
        };
        for (const std::string& n : nodes) {
            const IGraphFeature& f = *m_byId.find(n)->second;
            for (std::string_view before : f.RunsAfter())  addEdge(before, n);   // before -> n
            for (std::string_view after  : f.RunsBefore()) addEdge(n, after);    // n -> after
        }

        // Kahn's algorithm, popping the lowest input-rank ready node each step (deterministic).
        const auto byInputRank = [&](const std::string& x, const std::string& y) {
            return pos.at(x) < pos.at(y);
        };
        std::vector<std::string> ready;
        for (const std::string& n : nodes)
            if (indeg[n] == 0) ready.push_back(n);
        std::sort(ready.begin(), ready.end(), byInputRank);

        std::vector<std::string> order;
        order.reserve(nodes.size());
        while (!ready.empty()) {
            const std::string n = ready.front();
            ready.erase(ready.begin());
            order.push_back(n);
            for (const std::string& m : succ[n])
                if (--indeg[m] == 0) {
                    const auto at = std::lower_bound(ready.begin(), ready.end(), m, byInputRank);
                    ready.insert(at, m);
                }
        }

        if (order.size() != nodes.size()) {
            // A cycle: the nodes never reaching indeg 0. Report and append them in input order so the
            // compile still runs (loud fault, not a brick).
            std::string cyc;
            for (const std::string& n : nodes)
                if (std::find(order.begin(), order.end(), n) == order.end()) {
                    if (!cyc.empty()) cyc += ", ";
                    cyc += n;
                    order.push_back(n);
                }
            log.Warn(std::format(
                "feature run-order: dependency CYCLE among [{}] — using input order for those; "
                "fix the RunsAfter/RunsBefore declarations.", cyc));
        }
        return order;
    }

    std::vector<std::pair<std::string, FeatureResult>>
    FeatureRegistry::Run(const std::vector<std::string>& orderedIds,
                         havok::model::BehaviorData& data,
                         const FeatureContext& ctx) const
    {
        std::vector<std::pair<std::string, FeatureResult>> out;
        out.reserve(orderedIds.size());
        for (const std::string& id : orderedIds) {
            const auto it = m_byId.find(id);   // transparent lookup (std::less<>)
            if (it == m_byId.end()) continue;
            IGraphFeature& f = *it->second;
            if (!f.AppliesTo(ctx)) continue;
            out.emplace_back(id, f.Apply(data, ctx));
        }

        // Collision report: any node key written by >=2 features. `writers` preserves run order, so the
        // last entry is the winner (last-writer-wins over the topo order). Purely diagnostic — the
        // clobber already happened deterministically; this just makes it visible (CS's declarative
        // model: authors self-report `touched`, host surfaces the overlap).
        std::map<std::string, std::vector<std::string>, std::less<>> writers;
        for (const auto& [id, r] : out)
            for (const std::string& key : r.touched)
                writers[key].push_back(id);
        for (const auto& [key, ids] : writers) {
            if (ids.size() < 2) continue;
            std::string list;
            for (const std::string& id : ids) {
                if (!list.empty()) list += ", ";
                list += id;
            }
            ctx.log.Warn(std::format(
                "feature collision on node '{}': written by [{}] — '{}' wins (ran last).",
                key, list, ids.back()));
        }
        return out;
    }

}  // namespace CB::features
