#pragma once

// GraphClipSink — the host's concrete CB::features::IAnimDataSink for the warm-up compile.
//
// The animation-relay.adsf-derive contributor feature pushes one DeriveClipInput per hkbClipGenerator
// as each graph compiles (Resolver::Resolve). This accumulates them keyed by graph serve-key. The
// animdata finalizer (adserve::ServeAnimData) reads Contributions() AFTER CompileAll — a single-writer
// / single-reader handoff across the warm-up phase boundary. Reads happen only once compile is done;
// writes are also serialized by the Resolver mutex (Resolve holds it for its whole body), but the sink
// guards independently so it stays correct if that ever changes.

#include "IGraphFeature.h"                 // CB::features::IAnimDataSink (havok-pipeline)

#include <havok/anim/AnimDataDeriver.h>    // havok::animdata::DeriveClipInput

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace CB {

    class GraphClipSink final : public CB::features::IAnimDataSink {
    public:
        // graph serve-key (e.g. "meshes/actors/character/behaviors/0_master.hkx") -> its clip inputs.
        using Contribs = std::map<std::string, std::vector<havok::animdata::DeriveClipInput>>;

        void EmitClip(std::string_view graphKey, const havok::animdata::DeriveClipInput& in) override {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_byGraph[std::string(graphKey)].push_back(in);
        }

        // Valid to read after CompileAll (no concurrent writers then).
        const Contribs& Contributions() const { return m_byGraph; }

        std::size_t GraphCount() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_byGraph.size();
        }
        std::size_t ClipCount() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            std::size_t n = 0;
            for (const auto& [k, v] : m_byGraph) n += v.size();
            return n;
        }

        void Clear() {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_byGraph.clear();
        }

    private:
        Contribs           m_byGraph;
        mutable std::mutex m_mtx;
    };

}  // namespace CB
