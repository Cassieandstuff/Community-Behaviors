#include "PCH.h"

#include "features/Animation 64/BindingIndex.h"
#include "features/Animation 64/Loader.h"

#include <platform/game/AnimationLoad.h>
#include <platform/hooks/hookslib.h>

#include <Behavior/Generator/BSOffsetAnimation/BSOffsetAnimationGenerator.h>
#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGenerator.h>
#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <intrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

/// @file
/// @brief The clip loader's index path at 64 bits: AnimationFileManagerSingleton's slots (AE 1.6.1170: onFileReady
///        0x140bcbb90, load 0x140bcbd30, unload 0x140bcbea0, loadAnimation 0x140bcc040, unloadAnimation 0x140bcc090),
///        the resolver (0x140bcc220), the triple lookup (0x140bb3c20) and the binder (0x140bad6e0), ported from the
///        disassembly with the index 64-bit. The slots are overridden; the other three are called only by them, so
///        their ports here are the only ones that run. The offset generator's activate / deactivate / update, which
///        only hand the clip's index to slots 4 and 5, call the 64-bit versions directly.
/// @note  Where an index lands is unchanged: entry `index` (0x20 bytes) of the character's CRC table, bit `index` of
///        its loaded bitset, slot `index` of its binding set. Those all have 32-bit counts; the vanilla ceiling was the
///        16-bit value passed between these functions.
/// @note  The layouts are the exe's, read by offset as vanilla reads them (none is reflected): the character data
///        (BShkbAnimationGraph, the behavior graph's m_userData) holds the hkbCharacter at +0xc0 and its project
///        DB data at +0x200; the manager holds its spin lock at +0x14.

namespace CB::feature::widen {

    namespace {

        namespace hk    = CB::havok;
        namespace game  = CB::platform::game;
        namespace hooks = CB::platform::hooks;

        constexpr const char* kWho  = "Animation64";
        constexpr const char* kFlag = "animation64_loader";

        template <class T>
        T& At(void* base, std::ptrdiff_t offset)
        {
            return *reinterpret_cast<T*>(static_cast<std::byte*>(base) + offset);
        }

        /// @brief The EntryDB's key for one animation file. All zero names nothing (an index outside the table).
        struct CrcTriple {
            std::uint32_t path, file, ext;
            bool          Empty() const { return path == 0 && file == 0 && ext == 0; }
            bool          Same(const void* other) const { return std::memcmp(this, other, sizeof(CrcTriple)) == 0; }
        };
        static_assert(sizeof(CrcTriple) == 12);

        constexpr CrcTriple kNoTriple{};

        /// @brief The project DB data: the CRC table (+0x40 entries, +0x50 count), the loaded bitset (+0x60 words,
        ///        inline when +0x68 is 1), the event map (+0xa0), the scene-slot use counts (+0x130, count +0x140).
        void* ProjectData(void* characterData) { return At<void*>(characterData, 0x200); }

        /// @brief A CRC table entry: the triple, the loaded entry (+0x10), and a word holding the entry's bind count
        ///        under a spin bit (bit 31).
        std::byte* TableEntry(void* project, std::int64_t index)
        {
            return At<std::byte*>(project, 0x40) + static_cast<std::uint64_t>(index) * 0x20;
        }

        std::uint32_t TableCount(void* project) { return At<std::uint32_t>(project, 0x50); }

        /// @return the character data the context's behavior graph carries, or null without one.
        void* CharacterDataOf(const hk::hkbContext& context)
        {
            const hk::hkbBehaviorGraph* graph = context.m_character->m_behaviorGraph;
            return graph ? reinterpret_cast<void*>(graph->m_userData) : nullptr;
        }

        void* EntryDB() { return *reinterpret_cast<void**>(hooks::engine::Resolve(hooks::engine::AnimEntryDB_Singleton)); }

        /// @brief Sets the thread's task tag to the animation loader's (0x3a) for the scope, as every vanilla slot does.
        class LoaderTaskScope {
        public:
            LoaderTaskScope() : tls_(RE::GetStaticTLSData()), saved_(tls_->taskFlag) { tls_->taskFlag = 0x3a; }
            ~LoaderTaskScope() { tls_->taskFlag = saved_; }
            LoaderTaskScope(const LoaderTaskScope&)            = delete;
            LoaderTaskScope& operator=(const LoaderTaskScope&) = delete;

        private:
            RE::TLSData*  tls_;
            std::uint32_t saved_;
        };

        RE::BSSpinLock& ManagerLock(void* manager) { return At<RE::BSSpinLock>(manager, 0x14); }

        /// @return the triple at `index` of the character's CRC table, or the empty triple outside it (0x140bb3c20).
        const CrcTriple* TripleAt(void* characterData, std::int64_t index)
        {
            void* project = ProjectData(characterData);
            if (index < 0 || index >= TableCount(project)) return &kNoTriple;
            return reinterpret_cast<const CrcTriple*>(TableEntry(project, index));
        }

        /// @brief The binder (0x140bad6e0): binds `*entry` (a loaded animation, or null to unbind) at `index` of the
        ///        character's CRC table, keeping the entry's bind count, the loaded bitset, the binding set slot and the
        ///        synchronized clip's scene slot in step.
        /// @return false when the index is outside the table, or another entry is already bound there.
        /// @note  On a first bind the entry moves out of `*entry` into the table (`*entry` becomes null); on the last
        ///        unbind the table's entry is released here.
        /// @note  The scene slot index (the synchronized clip's m_sAnimationBindingIndex) is still 16-bit; scene slots
        ///        sit after the roster, so with a roster past 32,767 that field must widen too (Animation64.h).
        bool Bind(void* characterData, std::int64_t index, hk::BSSynchronizedClipGenerator* sync, void** entry)
        {
            void* project = ProjectData(characterData);
            if (index < 0 || index >= TableCount(project) || index > std::numeric_limits<std::int32_t>::max()) return false;
            std::byte*                    slot     = TableEntry(project, index);
            volatile LONG*                word     = reinterpret_cast<volatile LONG*>(slot + 0x18);
            void*&                        bound    = *reinterpret_cast<void**>(slot + 0x10);
            void* const                   incoming = *entry;

            std::uint32_t count = static_cast<std::uint32_t>(*word);
            for (std::uint32_t spins = 0;;) {
                count &= 0x7fffffffu;
                const LONG seen = InterlockedCompareExchange(word, static_cast<LONG>(count | 0x80000000u), static_cast<LONG>(count));
                if (static_cast<std::uint32_t>(seen) == count) break;
                count = static_cast<std::uint32_t>(seen);
                if (spins < 10000) {
                    ++spins;
                    Sleep(0);
                } else {
                    Sleep(1);
                }
            }
            _mm_mfence();

            bool  ok        = true;
            void* source    = nullptr;   ///< the bound entry's payload +0x20, what the binding set installs
            void* toRelease = nullptr;
            auto  sourceOf  = [](void* e) -> void* {
                void* payload = At<void*>(e, 0x28);
                return payload ? At<void*>(payload, 0x20) : nullptr;
            };
            if (*entry) {
                if (!bound) {
                    count = 1;
                    if (void* payload = At<void*>(*entry, 0x28)) {
                        /// @note Name provisional: the payload's virtual at +0x60 finishes it before its binding is read.
                        (*reinterpret_cast<void (***)(void*)>(payload))[0x60 / 8](payload);
                        source = At<void*>(At<void*>(*entry, 0x28), 0x20);
                    }
                    if (*entry != bound) {
                        void* old = bound;
                        bound     = *entry;
                        *entry    = nullptr;
                        game::AnimEntry_Release(old);
                    }
                } else if (bound == *entry) {
                    ++count;
                    source = sourceOf(*entry);
                } else {
                    ok = false;
                }
            } else if (bound) {
                if (--count != 0) {
                    source = sourceOf(bound);
                } else {
                    toRelease = bound;
                    bound     = nullptr;
                    game::AnimEntry_Release(nullptr);
                    if (*entry != bound) {
                        void* old = bound;
                        bound     = *entry;
                        *entry    = nullptr;
                        game::AnimEntry_Release(old);
                    }
                }
            }

            project                  = ProjectData(characterData);
            const auto        bitWord = static_cast<std::uint32_t>(index >> 5);
            const std::uint32_t mask  = 1u << (static_cast<std::uint32_t>(index) & 31);
            auto              bits    = [&](void* p) -> std::uint32_t* {
                const std::uint32_t words = At<std::uint32_t>(p, 0x68);
                if (bitWord >= words) return nullptr;
                return words == 1 ? &At<std::uint32_t>(p, 0x60) : At<std::uint32_t*>(p, 0x60) + bitWord;
            };
            const std::uint32_t* loaded = bits(project);
            void*                character = static_cast<std::byte*>(characterData) + 0xc0;
            game::AnimationBindingSet_Install(character, static_cast<std::int32_t>(index), source, static_cast<std::byte*>(project) + 0xa0,
                                              !loaded || (*loaded & mask) == 0);
            if (source)
                if (std::uint32_t* b = bits(ProjectData(characterData))) *b |= mask;

            if (sync && sync->m_sAnimationBindingIndex >= 0) {
                project                 = ProjectData(characterData);
                const std::uint32_t rel = static_cast<std::uint32_t>(static_cast<std::int32_t>(sync->m_sAnimationBindingIndex) -
                                                                     static_cast<std::int32_t>(TableCount(project)));
                if (rel < At<std::uint32_t>(project, 0x140)) {
                    if (!incoming) {
                        std::uint8_t& uses = At<std::uint8_t*>(project, 0x130)[rel];
                        if (uses != 0) {
                            --uses;
                            if (At<std::uint8_t*>(ProjectData(characterData), 0x130)[rel] == 0) game::BSSync_ReleaseSceneBindingSlot(character, sync);
                        }
                    } else {
                        if (At<std::uint8_t*>(project, 0x130)[rel] == 0) {
                            /// @note Vanilla constructs the functor through its base (which only stores the base vtable)
                            ///       and destroys it the same way; the derived vtable and the map are all it carries.
                            struct {
                                std::uintptr_t vtable;
                                void*          eventMap;
                            } findEvent{ hooks::engine::Resolve(hooks::engine::BSHashMapEventFindFunctor_Vtable), static_cast<std::byte*>(project) + 0xa0 };
                            game::BSSync_FillSceneBindingSlot(character, sync, &findEvent);
                            project = ProjectData(characterData);
                        }
                        ++At<std::uint8_t*>(project, 0x130)[rel];
                    }
                }
            }

            _mm_mfence();
            *word = static_cast<LONG>(count);
            game::AnimEntry_Release(toRelease);
            return ok;
        }

        /// @brief The resolver (0x140bcc220): looks up `index`'s triple and binds its entry if it is loaded (state
        ///        0xC), or asks for it to stream in (state 4).
        /// @return false when there is no character data, or the stream request failed (no state then).
        /// @post  `*triple` holds the index's triple (empty outside the table: state 0xC, nothing to load).
        bool Resolve(void* manager, const hk::hkbContext& context, std::int64_t index, hk::BSSynchronizedClipGenerator* sync, CrcTriple* triple,
                     std::int32_t* state)
        {
            void* characterData = CharacterDataOf(context);
            if (!characterData) return false;
            void* entry = nullptr;
            *triple     = *TripleAt(characterData, index);

            std::uint32_t cached = std::numeric_limits<std::uint32_t>::max();
            for (std::uint32_t i = 0, n = At<std::uint32_t>(manager, 0x60); i < n && cached == std::numeric_limits<std::uint32_t>::max(); ++i)
                if (triple->Same(At<std::byte*>(manager, 0x50) + i * 0x18 + 8)) cached = i;

            bool ok = false;
            if (triple->Empty() || (cached == std::numeric_limits<std::uint32_t>::max() && !game::AnimEntryDB_FindLoaded(EntryDB(), triple, &entry))) {
                if (triple->Empty()) {
                    ok     = true;
                    *state = 0xc;
                } else if (!game::AnimEntryDB_Request(EntryDB(), triple)) {
                    ok     = true;
                    *state = 4;
                }
            } else {
                ok = true;
                if (cached != std::numeric_limits<std::uint32_t>::max())
                    game::AnimEntry_Assign(&entry, reinterpret_cast<void* const*>(At<std::byte*>(manager, 0x50) + cached * 0x18));
                *state = Bind(characterData, index, sync, &entry) ? 0xc : 4;
            }
            game::AnimEntry_Release(entry);
            return ok;
        }

        hk::BSSynchronizedClipGenerator* SyncOf(void* sync) { return static_cast<hk::BSSynchronizedClipGenerator*>(sync); }

        using Queue           = hooks::Override<&hk::IAnimationClipLoader::queue>;
        using Load            = hooks::Override<&hk::IAnimationClipLoader::load>;
        using Unload          = hooks::Override<&hk::IAnimationClipLoader::unload>;
        using LoadAnimation16 = hooks::Override<&hk::IAnimationClipLoader::loadAnimation>;
        using UnloadAnim16    = hooks::Override<&hk::IAnimationClipLoader::unloadAnimation>;
        using OffsetActivate   = hooks::Override<&hk::BSOffsetAnimationGenerator::activate>;
        using OffsetDeactivate = hooks::Override<&hk::BSOffsetAnimationGenerator::deactivate>;
        using OffsetUpdate     = hooks::Override<&hk::BSOffsetAnimationGenerator::update>;

        /// @brief Slot 1 (onFileReady, 0x140bcbb90): binds the clip's animation if it is loaded, else queues it.
        /// @return 1 bound, 0 queued, -1 pending or nothing to load; the clip's load state goes to m_userData.
        std::int32_t OnQueue(Queue::Next, hk::IAnimationClipLoader* manager, const hk::hkbContext& context, hk::hkbClipGenerator* clip, void* sync)
        {
            LoaderTaskScope    task;
            std::int32_t       result = -1;
            CrcTriple          triple{};
            std::int32_t       state = 4;
            const std::int64_t index = BindingIndex(clip);
            if (Resolve(manager, context, index, SyncOf(sync), &triple, &state)) {
                clip->m_userData = static_cast<hk::hkUlong>(static_cast<std::int64_t>(state));
                result           = 1;
            } else if (triple.Empty()) {
                clip->m_userData = 4;
            } else {
                RE::BSSpinLock& lock = ManagerLock(manager);
                lock.Lock();
                const std::int32_t queued = game::AnimationFileManager_QueueRequest(manager, &triple);
                std::int64_t       loadState;
                if (queued == 1) {
                    if (void* characterData = CharacterDataOf(context)) Bind(characterData, index, SyncOf(sync), &At<void*>(manager, 0x20));
                    loadState = state;
                    result    = 1;
                } else {
                    loadState = queued == -1 ? 4 : 8;
                    result    = queued == -1 ? -1 : 0;
                }
                clip->m_userData = static_cast<hk::hkUlong>(loadState);
                lock.Unlock();
            }
            return result;
        }

        /// @brief Slot 2 (0x140bcbd30): pumps the stream queue and binds the clip's animation once it is loaded.
        bool OnLoad(Load::Next, hk::IAnimationClipLoader* manager, const hk::hkbContext& context, hk::hkbClipGenerator* clip, void* sync)
        {
            RE::BSSpinLock& lock = ManagerLock(manager);
            lock.Lock();
            bool result = false;
            {
                LoaderTaskScope task;
                game::AnimationFileManager_PumpQueue(manager);
                CrcTriple    triple{};
                std::int32_t state = 4;
                if (Resolve(manager, context, BindingIndex(clip), SyncOf(sync), &triple, &state)) {
                    result           = true;
                    clip->m_userData = static_cast<hk::hkUlong>(static_cast<std::int64_t>(state));
                    if (At<void*>(manager, 0x20) && triple.ext == At<std::uint32_t>(manager, 0x30) && std::memcmp(&triple, &At<std::byte>(manager, 0x28), 8) == 0)
                        game::AnimationFileManager_PumpQueue(manager);
                    game::AnimationFileManager_ReleaseCached(manager, &triple);
                }
            }
            lock.Unlock();
            return result;
        }

        /// @brief Slot 3 (0x140bcbea0): unbinds the clip's animation (state 0xC) or withdraws its request (state 8).
        /// @note  Vanilla returns nothing; the interface's int is unused by every caller.
        std::int32_t OnUnload(Unload::Next, hk::IAnimationClipLoader* manager, const hk::hkbContext& context, hk::hkbClipGenerator* clip, void* sync)
        {
            LoaderTaskScope task;
            if (void* characterData = CharacterDataOf(context)) {
                void*              entry  = nullptr;
                const std::int64_t index  = BindingIndex(clip);
                const CrcTriple*   triple = TripleAt(characterData, index);
                if (!triple->Empty()) {
                    if (clip->m_userData == 8) {
                        RE::BSSpinLock& lock = ManagerLock(manager);
                        lock.Lock();
                        game::AnimationFileManager_CancelRequest(manager, triple);
                        lock.Unlock();
                    } else if (clip->m_userData == 0xc) {
                        Bind(characterData, index, SyncOf(sync), &entry);
                    }
                }
                game::AnimEntry_Release(entry);
            }
            clip->m_userData = 0;
            return 0;
        }

        bool OnLoadAnimation16(LoadAnimation16::Next, hk::IAnimationClipLoader* manager, const hk::hkbContext& context, hk::hkInt16 index)
        {
            return LoadAnimation(manager, context, index);
        }

        void OnUnloadAnimation16(UnloadAnim16::Next, hk::IAnimationClipLoader* manager, const hk::hkbContext& context, hk::hkInt16 index)
        {
            UnloadAnimation(manager, context, index);
        }

        hk::IAnimationClipLoader* ClipLoader() { return hooks::Live<&hk::g_animationClipLoader>(); }

        /// @return the offset clip's binding index, -1 without one (vanilla passes hkUint16(-1)).
        std::int64_t OffsetIndex(hk::BSOffsetAnimationGenerator* gen)
        {
            hk::hkbClipGenerator* clip = gen->getOffsetClipGenerator();
            return clip ? BindingIndex(clip) : -1;
        }

        /// @brief BSOffsetAnimationGenerator::activate as havok/ has it, the offset clip's index handed over at 64 bits.
        void OnOffsetActivate(OffsetActivate::Next, hk::BSOffsetAnimationGenerator* gen, const hk::hkbContext& context)
        {
            if (hk::IAnimationClipLoader* loader = ClipLoader(); loader && !LoadAnimation(loader, context, OffsetIndex(gen))) {
                gen->m_userData = 8;
                return;
            }
            gen->sampleOffset(context);
            gen->m_userData = 0xc;
        }

        void OnOffsetDeactivate(OffsetDeactivate::Next, hk::BSOffsetAnimationGenerator* gen, const hk::hkbContext& context)
        {
            if (gen->m_userData == 0xc)
                if (hk::IAnimationClipLoader* loader = ClipLoader()) UnloadAnimation(loader, context, OffsetIndex(gen));
            gen->m_BoneIndexA.clear();
            gen->m_BoneOffsetA.clear();
            gen->m_bOffsetValid = false;
        }

        /// @note Drops vanilla's HK_TIMER pair, as the clip readers do (Readers.cpp).
        void OnOffsetUpdate(OffsetUpdate::Next, hk::BSOffsetAnimationGenerator* gen, const hk::hkbContext& context, hk::hkReal)
        {
            bool loaded = true;
            if (gen->m_userData == 8) {
                if (hk::IAnimationClipLoader* loader = ClipLoader()) loaded = LoadAnimation(loader, context, OffsetIndex(gen));
                if (loaded) gen->m_userData = 0xc;
            }
            if (loaded) gen->sampleOffset(context);
        }

        /// @note Last: replacements, innermost, so observers still see every call.
        [[maybe_unused]] const bool s_subscribed =
            (Queue::Subscribe(&OnQueue, hooks::Priority::Last, kWho, kFlag), Load::Subscribe(&OnLoad, hooks::Priority::Last, kWho, kFlag),
             Unload::Subscribe(&OnUnload, hooks::Priority::Last, kWho, kFlag),
             LoadAnimation16::Subscribe(&OnLoadAnimation16, hooks::Priority::Last, kWho, kFlag),
             UnloadAnim16::Subscribe(&OnUnloadAnimation16, hooks::Priority::Last, kWho, kFlag),
             OffsetActivate::Subscribe(&OnOffsetActivate, hooks::Priority::Last, kWho, kFlag),
             OffsetDeactivate::Subscribe(&OnOffsetDeactivate, hooks::Priority::Last, kWho, kFlag),
             OffsetUpdate::Subscribe(&OnOffsetUpdate, hooks::Priority::Last, kWho, kFlag), true);

    }  // namespace

    /// @note Slot 4 (0x140bcc040): the resolver without a clip or sync.
    bool LoadAnimation(hk::IAnimationClipLoader* loader, const hk::hkbContext& context, std::int64_t index)
    {
        CrcTriple    triple{};
        std::int32_t state = 0;
        return Resolve(loader, context, index, nullptr, &triple, &state);
    }

    /// @note Slot 5 (0x140bcc090).
    void UnloadAnimation(hk::IAnimationClipLoader*, const hk::hkbContext& context, std::int64_t index)
    {
        LoaderTaskScope task;
        if (void* characterData = CharacterDataOf(context)) {
            void* entry = nullptr;
            Bind(characterData, index, nullptr, &entry);
            game::AnimEntry_Release(entry);
        }
    }

}  // namespace CB::feature::widen
