#include "PCH.h"

#include "features/Animation 64/BindingIndex.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/game/AnimationLoad.h>
#include <platform/hooks/hookslib.h>

#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// @file
/// @brief LoadClips' index stamp at 64 bits ([Animation64] bLoader): after LoadClips binds a graph's clips to the
///        character's roster, each clip it stamped gets its full roster position in the 64-bit slot (LoadClips truncates
///        the position to the 16-bit field).
/// @note  The position is found as LoadClips finds it, so the two agree by construction: the roster keyed by each name's
///        file-name tail (from the last '\', the whole name without one), the later of two equal keys winning; a clip
///        LoadClips stamped is an exact hkbClipGenerator with a name and no animation control (an unrostered clip loads
///        its own file and gets a control instead). Keys compare as BSFixedStrings, the engine's case-insensitive string
///        pool, as LoadClips' maps compare them: path identity folds inside the engine's codec, never in CB.
/// @note  The clips are reached the way LoadClips' iterator reaches them: every node getChildren reports under
///        FLAG_GENERATORS_ONLY, referenced behaviors included, each node once.

namespace CB::feature::widen {

    namespace {

        namespace hk    = CB::havok;
        namespace game  = CB::platform::game;
        namespace hooks = CB::platform::hooks;

        using StampClips = hooks::Override<&game::LoadClips>;

        /// @return the key LoadClips files `name` under: its tail from the last backslash, or the whole name.
        RE::BSFixedString KeyOf(const char* name)
        {
            const char* tail = std::strrchr(name, '\\');
            return RE::BSFixedString(tail ? tail : name);
        }

        void OnStampClips(StampClips::Next next, void* stringData, void* bindingSet, void* assetLoader, void* rootBehavior, const char* path,
                          void* annotationMap, void* unknown)
        {
            next(stringData, bindingSet, assetLoader, rootBehavior, path, annotationMap, unknown);
            if (!stringData || !rootBehavior) return;
            /// @note Graphs load before kDataLoaded (the main menu's), and the walk grows hkArrays inline.
            platform::classes::EnsureHavokGlobalsMirrored();

            /// @note The keys stay held for the walk: LoadClips has released its own by now, so a key's pool entry can be
            ///       this string's alone, freed with it, and its address reused by the next key (2026-10-25: most clips
            ///       took one late roster position). Held, an equal name interns to the same entry.
            const auto&                                   names = static_cast<const hk::hkbCharacterStringData*>(stringData)->m_animationNames;
            std::vector<RE::BSFixedString>                keys;
            std::unordered_map<const char*, std::int64_t> positions;
            keys.reserve(static_cast<std::size_t>(names.getSize()));
            positions.reserve(static_cast<std::size_t>(names.getSize()));
            for (int i = 0; i < names.getSize(); ++i) {
                if (const char* name = names[i].cString()) {
                    keys.push_back(KeyOf(name));
                    positions[keys.back().data()] = i;
                }
            }

            std::uint64_t               stamped = 0, disagreed = 0;
            std::vector<hk::hkbNode*>   pending{ static_cast<hk::hkbNode*>(rootBehavior) };
            std::unordered_set<hk::hkbNode*> seen{ pending.front() };
            while (!pending.empty()) {
                hk::hkbNode* node = pending.back();
                pending.pop_back();

                if (node->getClassType() == &hk::hkbClipGenerator::staticClass()) {
                    auto* clip = static_cast<hk::hkbClipGenerator*>(node);
                    if (const char* name = clip->m_animationName.cString(); name && !clip->m_animationControl) {
                        const auto         at       = positions.find(KeyOf(name).data());
                        const std::int64_t position = at != positions.end() ? at->second : -1;
                        if (static_cast<std::uint16_t>(position) != static_cast<std::uint16_t>(clip->m_animationBindingIndex)) ++disagreed;
                        SetBindingIndex(clip, position);
                        ++stamped;
                    }
                }

                hk::hkArray<hk::hkbNode::ChildInfo> children;
                children.reserve(node->getMaxNumChildren(hk::hkbNode::FLAG_GENERATORS_ONLY));
                hk::hkbNode::ChildrenInfo info(children);
                node->getChildren(hk::hkbNode::FLAG_GENERATORS_ONLY, info);
                for (int i = 0; i < children.getSize(); ++i)
                    if (hk::hkbNode* child = children[i].m_node; child && seen.insert(child).second) pending.push_back(child);
            }
            /// @note A disagreement means this key differs from LoadClips' own: the 16-bit stamp is the low half of the
            ///       position it found, so the two match whenever they name the same roster slot.
            if (disagreed)
                LOG_WARN("animation 64: LoadClips '{}': {} of {} stamped clip(s) got another roster position from CB than from LoadClips", path ? path : "",
                         disagreed, stamped);
        }

        [[maybe_unused]] const bool s_subscribed = StampClips::Subscribe(&OnStampClips, hooks::Priority::Last, "Animation64", "animation64_loader");

    }  // namespace

}  // namespace CB::feature::widen
