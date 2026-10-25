#pragma once

#include <PCH.h>

#include <cstdint>

/// @file
/// @brief Game functions of animation loading by binding index: the clip loader (AnimationFileManagerSingleton) and the
///        animation EntryDB below it, the per-character binding set, and LoadClips, which stamps each clip's index.
/// @note  A "CRC triple" is the 12-byte {crc32 path, crc32 file name, extension} key the EntryDB loads by. A loaded
///        animation's entry is reference-counted at entry+0xc (0x30000001 = the last reference; see AnimEntry_Release).
/// @note  AE only (ids verified hash-identical in 1.6.1170 and 1.7.104): SE's equivalents differ in shape (the EntryDB
///        lookup takes other arguments, LoadClips' maps are inlined), so none has an SE pin.

namespace CB::platform::game {

    /// @brief Queues a streaming request for the animation `triple` names, or bumps a queued one.
    /// @return 1 when it is loaded now, -1 when it is still pending, otherwise not ready (vanilla's onFileReady maps
    ///         these to the clip load states 0xC / 4 / 8).
    /// @note  Name provisional. Caller holds the manager's spin lock (+0x14).
    std::int32_t AnimationFileManager_QueueRequest(void* manager, const void* triple);

    /// @brief Promotes the manager's queued requests while nothing is streaming (its current request, +0x20).
    /// @note  Name provisional.
    void AnimationFileManager_PumpQueue(void* manager);

    /// @brief Drops one use of the manager's cached entry for `triple` (+0x50, 0x18-byte records), releasing it with the
    ///        last use.
    /// @return whether `triple` was cached.
    /// @note  Name provisional.
    bool AnimationFileManager_ReleaseCached(void* manager, const void* triple);

    /// @brief Withdraws a queued (not yet loaded) request for `triple`.
    /// @note  Name provisional. Caller holds the manager's spin lock.
    void AnimationFileManager_CancelRequest(void* manager, const void* triple);

    /// @brief Finds the loaded entry for `triple` in the animation EntryDB, adding a reference to `*entry`.
    bool AnimEntryDB_FindLoaded(void* db, const void* triple, void** entry);

    /// @brief Asks the animation EntryDB to stream `triple` in.
    /// @return true when the request could not be made (the caller then reports no state).
    /// @note  Name provisional.
    bool AnimEntryDB_Request(void* db, const void* triple);

    /// @brief `*dst = *src`, adding a reference to the new entry and releasing the old one.
    void AnimEntry_Assign(void** dst, void* const* src);

    /// @brief Releases one reference to `entry` (nothing for null); the last hands it back to the EntryDB.
    void AnimEntry_Release(void* entry);

    /// @brief Installs the binding loaded from `source` (a loaded entry's payload +0x20) into `character`'s binding set
    ///        at `index`, converting its annotations to triggers through `eventMap` when `fresh`.
    /// @note  Name provisional. The index is 32-bit here already.
    void AnimationBindingSet_Install(void* character, std::int32_t index, void* source, void* eventMap, bool fresh);

    /// @brief Releases the synchronized clip's scene binding slot when its last user unbinds.
    /// @note  Name provisional.
    void BSSync_ReleaseSceneBindingSlot(void* character, void* sync);

    /// @brief Fills the synchronized clip's scene binding slot on its first bind; `findEvent` is a
    ///        BSHashMapEventFindFunctor over the project's event map.
    void BSSync_FillSceneBindingSlot(void* character, void* sync, void* findEvent);

    /// @brief LoadClips: binds every clip generator under `rootBehavior` to the character's roster
    ///        (hkbCharacterStringData::animationNames), keyed by file name, stamping each clip's 16-bit
    ///        animationBindingIndex; an unrostered clip loads its own file and gets a control attached.
    /// @note  Lowercases each roster name's file-name tail in place.
    void LoadClips(void* stringData, void* bindingSet, void* assetLoader, void* rootBehavior, const char* path, void* annotationMap,
                   void* unknown);

}  // namespace CB::platform::game
