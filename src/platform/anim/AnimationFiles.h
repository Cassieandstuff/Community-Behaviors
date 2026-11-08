#pragma once

#include <PCH.h>

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Generator/Clip/hkbClipGenerator.h>

#include <string>
#include <string_view>

class hkaAnimationBinding;

/// @file
/// @brief Animation files by the engine's own file key, outside any character's roster: find a loaded one or ask for it
///        to stream in, read its binding, and play it on an active clip in place of the binding the clip activated with.
/// @note  Why no roster: a node holding its own file paths (CBAnimModifier) needs no binding-set slot, so it adds no
///        roster entry, needs no first-wins patch on LoadClips, keeps male and female rosters aligned, and meets no
///        16-bit binding limit.
/// @note  The protocol mirrors the clip loader's index path (Animation 64 Loader.cpp, from the exe): FindLoaded
///        takes a reference; a miss asks the EntryDB to stream the file in and holds nothing (vanilla's load state 4).
///        A later poll finds it loaded.

namespace CB::platform::anim {

    /// @return the EntryDB key of the animation at `path`, as the roster's CRC table builds it (0x140bc26d0): the path
    ///         from its `meshes\` folder (added in front when it has none), through BSResource::ID::GenerateFromPath.
    /// @note   Case: the `meshes\` search folds case (the exe's own, 0x140d094f0) and GenerateFromPath folds inside
    ///         the engine. Nothing else touches the path.
    /// @pre    `path` uses backslashes (the roster path is normalized before hashing; this is not).
    RE::BSResource::ID IdOf(std::string_view path);

    /// @brief One reference to a loaded animation file, released with this object. Move-only.
    class LoadedAnimation {
    public:
        LoadedAnimation() = default;
        ~LoadedAnimation();
        LoadedAnimation(LoadedAnimation&& other) noexcept;
        LoadedAnimation& operator=(LoadedAnimation&& other) noexcept;
        LoadedAnimation(const LoadedAnimation&)            = delete;
        LoadedAnimation& operator=(const LoadedAnimation&) = delete;

        explicit operator bool() const { return binding_ != nullptr; }
        /// @return the file's first animation binding (its hkaAnimationContainer's m_bindings[0]).
        ::hkaAnimationBinding* Binding() const { return binding_; }

        /// @brief Releases the reference now.
        void Reset();

    private:
        friend LoadedAnimation Acquire(const RE::BSResource::ID& id);

        void*                  entry_   = nullptr;   ///< the EntryDB entry; refcount at +0xc
        ::hkaAnimationBinding* binding_ = nullptr;
    };

    /// @brief Finds the file `id` names, taking a reference, or asks the EntryDB to stream it in.
    /// @return the loaded file, or an empty one while it streams in (poll again later) or when it has no binding.
    /// @note   Safe on Havok graph threads: the clip loader makes the same calls there.
    LoadedAnimation Acquire(const RE::BSResource::ID& id);

    /// @brief Plays `binding` on `clip`, which activated this pass with its own: the control's binding, the
    ///        annotation triggers (unless the clip is flagged not to convert them), the retargeting data and the
    ///        crop / start-time setup are redone as the clip's own activate does them (hkbClipGenerator.cpp:54-134).
    /// @return false (nothing changed) for a clip with no control, with index -1 (an unrostered clip: its
    ///         annotations merged into its loaded triggers, which no deactivate restores), or a null binding.
    /// @pre    `clip` is active, and `binding` stays alive until the clip deactivates.
    /// @post   The clip's own deactivate undoes everything: triggers back to m_originalTriggers, control and binding
    ///         cleared, mapper data released, its own file unloaded.
    bool Rebind(::hkbClipGenerator& clip, const ::hkbContext& context, ::hkaAnimationBinding* binding);

}  // namespace CB::platform::anim
