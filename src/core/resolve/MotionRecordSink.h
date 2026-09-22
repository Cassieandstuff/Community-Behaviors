#pragma once

// MotionRecordSink — the compile-side accumulator that makes the ANIMATION COMPILE the single
// producer of root-motion, mirroring GraphClipSink's single-writer-during-compile / single-reader-
// after contract.
//
// Native animations are `<name>.hkx` single-file units (renamed YAMLs); WriteNativeAnimations parses
// each one's AnimationDef and, when it carries an inline `motion:` block (AnimationDef.motion), emits
// the MotionRecord here — keyed by actor root + the animation's actor-root-relative path. The adsf
// finalizer (adserve::ServeAnimData / DeriveProjectPatch) then DRAINS this instead of re-reading the
// YAML, so motion resolved once at compile time flows forward like every other bound compiler output.
//
// KEY DISCIPLINE — must match the drain exactly (AnimationDataServer.cpp):
//   • outer key = actor root, the serve path truncated at "/animations" (== ServeAnimData's actorRoot,
//     unit truncated at "/behaviors"; both keep the full "meshes/actors/<nested>" prefix). LOWERCASED
//     here so a case difference between the producer's anim path and the consumer's unit path can't
//     orphan the lookup (the record VALUES stay verbatim — Havok is case-sensitive on those).
//   • inner key = the animation path relative to the actor root, ".yaml" already absent (the unit is
//     "<name>.hkx"), lowercased + '/'-sep — the exact form motionsForRoot produced and that
//     DeriveProjectPatch's normAnim(animationName) looks up.
//
// Writes come from the parallel native-anim compile (WriteNativeAnimations fans across the pool), so
// EmitMotion is mutex-guarded; reads happen only after that phase completes (no concurrent writers).

#include <interface/AnimationData.h>   // havok::animdata::MotionRecord

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace CB {

    class MotionRecordSink {
    public:
        using RootMotions = std::unordered_map<std::string, havok::animdata::MotionRecord>;  // animRelKey -> record

        // Emit one animation's inline motion. actorRoot is lowercased internally; animRelKey must already
        // be the lowercased, '/'-sep, actor-root-relative path (matching the drain).
        void EmitMotion(std::string actorRoot, std::string animRelKey, havok::animdata::MotionRecord rec) {
            for (char& c : actorRoot) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::lock_guard<std::mutex> lk(m_mtx);
            m_byRoot[std::move(actorRoot)].emplace(std::move(animRelKey), std::move(rec));
        }

        // Motion map for an actor root (drop-in for the old motionsForRoot return). actorRoot is
        // lowercased to match EmitMotion. Returns a stable empty map when the root has no inline motion.
        // Valid to read after the native-anim compile completes (no concurrent writers then).
        const RootMotions& ForRoot(std::string actorRoot) const {
            for (char& c : actorRoot) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::lock_guard<std::mutex> lk(m_mtx);
            const auto it = m_byRoot.find(actorRoot);
            if (it != m_byRoot.end()) return it->second;
            static const RootMotions kEmpty;
            return kEmpty;
        }

        std::size_t RootCount() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_byRoot.size();
        }
        std::size_t RecordCount() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            std::size_t n = 0;
            for (const auto& [k, v] : m_byRoot) n += v.size();
            return n;
        }

        void Clear() {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_byRoot.clear();
        }

    private:
        std::unordered_map<std::string, RootMotions> m_byRoot;   // lowercased actorRoot -> its motions
        mutable std::mutex                            m_mtx;
    };

}  // namespace CB
