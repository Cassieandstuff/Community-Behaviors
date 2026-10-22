#include "PCH.h"

#include <platform/classes/TypeRegistries.h>

#include "core/debug/Settings.h"
#include <platform/hooks/registry/Registry.h>

#include <text/Text.h>

#include <PluginLogger.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CB::platform::classes {

    namespace {

        namespace eng = hooks::engine;
        using Word    = std::uintptr_t;

        /// @brief Havok's hkTypeInfo, as the exe's static initializers fill it.
        struct TypeInfo {
            const char*   name;
            const char*   scopedName;
            void          (*finish)(void*, int);
            void          (*cleanup)(void*);
            const void*   vtable;
            std::uint64_t size;
        };

        const char* NameOf(const TypeInfo* info) { return info && info->name ? info->name : "(null)"; }
        /// @note hkClass's first member is its name.
        const char* ClassName(Word cls) { return cls && *reinterpret_cast<const char* const*>(cls) ? *reinterpret_cast<const char* const*>(cls) : "(null)"; }
        const char* Text(const char* s) { return s ? s : "(null)"; }

        constexpr std::size_t kMaxOwned      = 32;
        /// @brief hkbNode::cloneNode: how a character's graph copies a loaded node (vanilla's copy stamps vanilla's vtable).
        constexpr std::size_t kCloneNodeSlot = 12;

        /// @brief A class CB owns: its loaded objects, and its nodes' clones, carry CB's copy of the vtable.
        struct Owned {
            std::string                name;
            TypeInfo                   record{};
            void                       (*vanillaFinish)(void*, int) = nullptr;
            const Word*                vanillaVtable = nullptr;
            Word                       vanillaClone  = 0;
            /// @brief The RTTI locator vanilla keeps before slot 0, then the slots.
            std::vector<Word>          table;
            std::atomic<std::uint64_t> finished{}, cloned{};
            const Word* Vtable() const { return table.data() + 1; }
        };

        /// @brief The vanilla code each slot forwards to, and what the registries have seen.
        struct State {
            std::array<Owned, kMaxOwned> owned;
            std::size_t                  ownedCount = 0;
            const Word* typeInfoVt  = nullptr;
            const Word* vtableVt    = nullptr;
            const Word* classNameVt = nullptr;
            std::once_flag typeInfoOnce, vtableOnce, classNameOnce;
            std::atomic<std::uint64_t> typesRegistered{}, finished{}, cleanedUp{}, typeLookups{}, vtablesRegistered{},
                vtableLookups{}, classesRegistered{}, classLookups{};
            std::mutex                                          countsLock;
            std::map<std::string, std::uint64_t, std::less<>> finishedByClass;
            bool installed = false;
        };
        State& S()
        {
            static State s;
            return s;
        }

        /// @brief Runs vanilla's finish constructor, then stamps CB's vtable over vanilla's.
        template <std::size_t I>
        void FinishOwned(void* obj, int flag)
        {
            Owned& o = S().owned[I];
            o.vanillaFinish(obj, flag);
            *static_cast<const Word**>(obj) = o.Vtable();
            ++o.finished;
        }

        /// @brief Runs vanilla's cloneNode; a fresh copy (vanilla's vtable on it) gets CB's. A node with no internal state
        ///        returns itself, already CB's.
        template <std::size_t I>
        Word CloneOwned(void* self, Word rootBehavior)
        {
            Owned&     o     = S().owned[I];
            const Word clone = reinterpret_cast<Word (*)(void*, Word)>(o.vanillaClone)(self, rootBehavior);
            if (clone && *reinterpret_cast<const Word* const*>(clone) == o.vanillaVtable) {
                *reinterpret_cast<const Word**>(clone) = o.Vtable();
                ++o.cloned;
            }
            return clone;
        }

        template <std::size_t... I>
        constexpr std::array<void (*)(void*, int), sizeof...(I)> FinishThunks(std::index_sequence<I...>) { return { &FinishOwned<I>... }; }
        template <std::size_t... I>
        constexpr std::array<Word (*)(void*, Word), sizeof...(I)> CloneThunks(std::index_sequence<I...>) { return { &CloneOwned<I>... }; }
        constexpr auto kFinishThunks = FinishThunks(std::make_index_sequence<kMaxOwned>{});
        constexpr auto kCloneThunks  = CloneThunks(std::make_index_sequence<kMaxOwned>{});

        const TypeInfo* Substitute(const TypeInfo* info);
        const Word*     OwnedVtableFor(Word vanillaVtable);

        /// @brief Calls `vtable`'s slot `slot` on `self` with the argument registers passed through.
        template <class... A>
        Word Vanilla(const Word* vtable, int slot, void* self, A... args)
        {
            return reinterpret_cast<Word (*)(void*, A...)>(vtable[slot])(self, args...);
        }

        /// @brief Copies a fresh vanilla registry's fields into `self`, once, so vanilla's code runs on vanilla's layout.
        /// @note  Deferred to the first call because vanilla's maps allocate from the Havok heap, which does not exist at
        ///        SKSEPluginLoad; the first call comes from Havok's start-up. The fresh object is left unreferenced: its
        ///        storage is `self`'s from here on.
        void Adopt(std::once_flag& once, void* self, std::size_t size, const eng::Symbol& create, const char* what)
        {
            std::call_once(once, [&] {
                auto* fresh = reinterpret_cast<std::byte* (*)()>(eng::Resolve(create))();
                std::memcpy(static_cast<std::byte*>(self) + 0x10, fresh + 0x10, size - 0x10);
                LOG_INFO("type registries: {} set up from vanilla's create", what);
            });
        }

        /// @note Slot order is vanilla's vtable (0x141966350); the slots past 2 are Havok's hkTypeInfoRegistry.
        /// @invariant memSizeAndFlags 0: removeReference never deletes it (Havok's shutdown releases every singleton).
        class TypeInfoRegistry {
        public:
            virtual Word Destroy(Word flags)
            {
                LOG_INFO("type registries: type-info registry released (flags {}), kept", flags);
                return reinterpret_cast<Word>(this);
            }
            virtual Word GetClassType() { return Vanilla(S().typeInfoVt, 1, Ready()); }
            virtual Word CalcContentStatistics(Word collector, Word cls) { return Vanilla(S().typeInfoVt, 2, Ready(), collector, cls); }
            virtual Word RegisterTypeInfo(const TypeInfo* info)
            {
                ++S().typesRegistered;
                LOG_INFO("type registries: type info {} (finish {:#x}, vtable {:#x}, size {:#x})", NameOf(info),
                         info ? reinterpret_cast<Word>(info->finish) : 0, info ? reinterpret_cast<Word>(info->vtable) : 0,
                         info ? info->size : 0);
                Ready();
                return Vanilla(S().typeInfoVt, 3, this, Substitute(info));
            }
            /// @note Counted per class, not logged: a save load finishes some 300,000 objects. A class nothing registered
            ///       is logged, since vanilla then leaves the object without a vtable.
            virtual Word FinishLoadedObject(void* obj, const char* cls)
            {
                State& st = S();
                ++st.finished;
                const Word result = Vanilla(st.typeInfoVt, 4, Ready(), obj, cls);
                {
                    std::lock_guard guard(st.countsLock);
                    const std::string_view name = Text(cls);
                    if (auto it = st.finishedByClass.find(name); it != st.finishedByClass.end()) ++it->second;
                    else st.finishedByClass.emplace(name, 1);
                }
                if (!result) LOG_WARN("type registries: finishLoadedObject {} at {:#x}: no such class registered", Text(cls), reinterpret_cast<Word>(obj));
                return result;
            }
            virtual Word CleanupLoadedObject(void* obj, const char* cls)
            {
                ++S().cleanedUp;
                LOG_INFO("type registries: cleanupLoadedObject {} at {:#x}", Text(cls), reinterpret_cast<Word>(obj));
                return Vanilla(S().typeInfoVt, 5, Ready(), obj, cls);
            }
            virtual Word Merge(Word other)
            {
                LOG_INFO("type registries: type-info registry merges {:#x}", other);
                return Vanilla(S().typeInfoVt, 6, Ready(), other);
            }
            virtual Word GetTypeInfo(const char* cls)
            {
                ++S().typeLookups;
                return Vanilla(S().typeInfoVt, 7, Ready(), cls);
            }

        private:
            TypeInfoRegistry* Ready()
            {
                Adopt(S().typeInfoOnce, this, 0x30, eng::hkTypeInfoRegistry_Create, "type-info registry");
                return this;
            }

            std::uint16_t memSizeAndFlags = 0;
            std::int16_t  referenceCount  = 1;
            std::byte     fields[0x24]{};
        };
        static_assert(sizeof(TypeInfoRegistry) == 0x30);

        /// @note Slot order is vanilla's vtable (0x141966158).
        /// @invariant memSizeAndFlags 0, as TypeInfoRegistry.
        class VtableClassRegistry {
        public:
            virtual Word Destroy(Word flags)
            {
                LOG_INFO("type registries: vtable-class registry released (flags {}), kept", flags);
                return reinterpret_cast<Word>(this);
            }
            virtual Word GetClassType() { return Vanilla(S().vtableVt, 1, Ready()); }
            virtual Word CalcContentStatistics(Word collector, Word cls) { return Vanilla(S().vtableVt, 2, Ready(), collector, cls); }
            virtual Word RegisterVtable(Word vtable, Word cls)
            {
                ++S().vtablesRegistered;
                LOG_INFO("type registries: vtable {:#x} is class {}", vtable, ClassName(cls));
                const Word result = Vanilla(S().vtableVt, 3, Ready(), vtable, cls);
                if (const Word* own = OwnedVtableFor(vtable)) {
                    LOG_INFO("type registries: CB's vtable {:#x} is class {} too", reinterpret_cast<Word>(own), ClassName(cls));
                    Vanilla(S().vtableVt, 3, this, reinterpret_cast<Word>(own), cls);
                }
                return result;
            }
            virtual Word GetClassFromVirtualInstance(Word obj)
            {
                ++S().vtableLookups;
                return Vanilla(S().vtableVt, 4, Ready(), obj);
            }
            virtual Word GetClasses(Word out) { return Vanilla(S().vtableVt, 5, Ready(), out); }

        private:
            VtableClassRegistry* Ready()
            {
                Adopt(S().vtableOnce, this, 0x20, eng::hkVtableClassRegistry_Create, "vtable-class registry");
                return this;
            }

            std::uint16_t memSizeAndFlags = 0;
            std::int16_t  referenceCount  = 1;
            std::byte     fields[0x14]{};
        };
        static_assert(sizeof(VtableClassRegistry) == 0x20);

        /// @note Slot order is vanilla's hkDefaultClassNameRegistry vtable (0x141966260); registerList and both merges
        ///       register each class through slot 7, so this one's own RegisterClass logs every class they bring.
        /// @invariant memSizeAndFlags 0, as TypeInfoRegistry.
        class ClassNameRegistry {
        public:
            virtual Word Destroy(Word flags)
            {
                LOG_INFO("type registries: class-name registry released (flags {}), kept", flags);
                return reinterpret_cast<Word>(this);
            }
            virtual Word GetClassType() { return Vanilla(S().classNameVt, 1, Ready()); }
            virtual Word CalcContentStatistics(Word collector, Word cls) { return Vanilla(S().classNameVt, 2, Ready(), collector, cls); }
            virtual Word GetName() { return Vanilla(S().classNameVt, 3, Ready()); }
            virtual Word GetClassByName(const char* name)
            {
                ++S().classLookups;
                return Vanilla(S().classNameVt, 4, Ready(), name);
            }
            virtual Word GetClasses(Word out) { return Vanilla(S().classNameVt, 5, Ready(), out); }
            virtual Word SetName(const char* name)
            {
                LOG_INFO("type registries: class-name registry renamed {}", Text(name));
                return Vanilla(S().classNameVt, 6, Ready(), name);
            }
            virtual Word RegisterClass(Word cls, const char* name)
            {
                ++S().classesRegistered;
                LOG_INFO("type registries: class {} as {}", ClassName(cls), name ? name : "its own name");
                return Vanilla(S().classNameVt, 7, Ready(), cls, name);
            }
            virtual Word RegisterList(Word list)
            {
                LOG_INFO("type registries: class-name registry registers the list at {:#x}", list);
                return Vanilla(S().classNameVt, 8, Ready(), list);
            }
            virtual Word Merge(Word other)
            {
                LOG_INFO("type registries: class-name registry merges {:#x}", other);
                return Vanilla(S().classNameVt, 9, Ready(), other);
            }
            virtual Word MergeMap(Word map)
            {
                LOG_INFO("type registries: class-name registry merges the map at {:#x}", map);
                return Vanilla(S().classNameVt, 10, Ready(), map);
            }

        private:
            ClassNameRegistry* Ready()
            {
                Adopt(S().classNameOnce, this, 0x38, eng::hkDefaultClassNameRegistry_Create, "class-name registry");
                return this;
            }

            std::uint16_t memSizeAndFlags = 0;
            std::int16_t  referenceCount  = 1;
            std::byte     fields[0x2c]{};
        };
        static_assert(sizeof(ClassNameRegistry) == 0x38);

        TypeInfoRegistry    g_typeInfo;
        VtableClassRegistry g_vtable;
        ClassNameRegistry   g_className;

        void** Global(const eng::Symbol& s) { return reinterpret_cast<void**>(eng::Resolve(s)); }

        /// @return how many slots the vtable at `vt` has: its entries run while they point into the exe's code.
        /// @note The next vtable's RTTI locator (read-only data) ends it.
        std::size_t SlotCount(const Word* vt)
        {
            const auto       text  = REL::Module::get().segment(REL::Segment::textx);
            const Word       begin = text.address(), end = begin + text.size();
            std::size_t      n     = 0;
            while (n < 512 && vt[n] >= begin && vt[n] < end) ++n;
            return n;
        }

        /// @return whether the class registered as `name` derives from hkbNode (so its graph clones it through cloneNode).
        bool IsNode(const char* name)
        {
            for (Word cls = g_className.GetClassByName(name); cls; cls = *reinterpret_cast<const Word*>(cls + 8))
                if (std::string_view(ClassName(cls)) == "hkbNode") return true;
            return false;
        }

        /// @return CB's record for an owned class, built from vanilla's on first sight; otherwise `info` itself.
        /// @note  Called during Havok's start-up, after the class-name registry has every class (the static class list is
        ///        merged before any type info registers).
        const TypeInfo* Substitute(const TypeInfo* info)
        {
            State& st = S();
            if (!info || !info->name) return info;
            std::size_t i = 0;
            while (i < st.ownedCount && st.owned[i].name != info->name) ++i;
            if (i == st.ownedCount) return info;
            Owned& o = st.owned[i];
            if (o.vanillaFinish) return &o.record;
            if (!info->finish || !info->vtable) {
                LOG_WARN("type registries: {} has no finish constructor or vtable to own - vanilla's", info->name);
                return info;
            }
            const auto*       vt    = static_cast<const Word*>(info->vtable);
            const std::size_t slots = SlotCount(vt);
            o.table.assign(vt - 1, vt + slots);
            o.vanillaVtable = vt;
            o.vanillaFinish = info->finish;
            const bool node = IsNode(info->name) && slots > kCloneNodeSlot;
            if (node) {
                o.vanillaClone                   = vt[kCloneNodeSlot];
                o.table[1 + kCloneNodeSlot]      = reinterpret_cast<Word>(kCloneThunks[i]);
            }
            o.record        = *info;
            o.record.finish = kFinishThunks[i];
            o.record.vtable = o.Vtable();
            LOG_INFO("type registries: {} is CB's: {} slots copied from {:#x} to {:#x}{}", info->name, slots,
                     reinterpret_cast<Word>(vt), reinterpret_cast<Word>(o.Vtable()), node ? ", cloneNode wrapped" : "");
            return &o.record;
        }

        const Word* OwnedVtableFor(Word vanillaVtable)
        {
            const State& st = S();
            for (std::size_t i = 0; i < st.ownedCount; ++i)
                if (st.owned[i].vanillaFinish && reinterpret_cast<Word>(st.owned[i].vanillaVtable) == vanillaVtable) return st.owned[i].Vtable();
            return nullptr;
        }

        /// @brief Takes the classes CB owns from [HavokRuntime] sOwnTypes (comma list, exact names).
        void ReadOwnedTypes()
        {
            State&           st   = S();
            const std::string list = core::debug::Value("HavokRuntime", "sOwnTypes", "hkbClipGenerator");
            std::string_view  rest = list;
            while (!rest.empty()) {
                const auto       comma = rest.find(',');
                const auto       name  = CB::engine::text::Trim(rest.substr(0, comma));
                rest                   = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                if (name.empty()) continue;
                if (st.ownedCount == kMaxOwned) {
                    LOG_WARN("type registries: more than {} classes in sOwnTypes - {} and later stay vanilla's", kMaxOwned, name);
                    break;
                }
                st.owned[st.ownedCount++].name = name;
            }
        }

    }  // namespace

    void InstallTypeRegistries()
    {
        if (!core::debug::FlagOn("havok_type_registry")) {
            LOG_INFO("type registries: off ([HavokRuntime] bTypeRegistry) - vanilla's");
            return;
        }
        for (const eng::Symbol* s : { &eng::hkTypeInfoRegistry_Global, &eng::hkVtableClassRegistry_Global, &eng::hkClassNameRegistry_Global,
                                      &eng::hkTypeInfoRegistry_Create, &eng::hkVtableClassRegistry_Create, &eng::hkDefaultClassNameRegistry_Create }) {
            if (!eng::Available(*s)) {
                LOG_INFO("type registries: {} has no pin on this runtime - vanilla's", s->name);
                return;
            }
        }
        void** typeInfo  = Global(eng::hkTypeInfoRegistry_Global);
        void** vtable    = Global(eng::hkVtableClassRegistry_Global);
        void** className = Global(eng::hkClassNameRegistry_Global);
        if (*typeInfo || *vtable || *className) {
            LOG_WARN("type registries: Havok created its registries before CB loaded - vanilla's");
            return;
        }
        State& st      = S();
        ReadOwnedTypes();
        st.typeInfoVt  =reinterpret_cast<const Word*>(RE::VTABLE_hkTypeInfoRegistry[0].address());
        st.vtableVt    = reinterpret_cast<const Word*>(RE::VTABLE_hkVtableClassRegistry[0].address());
        st.classNameVt = reinterpret_cast<const Word*>(RE::VTABLE_hkDefaultClassNameRegistry[0].address());
        *typeInfo      = &g_typeInfo;
        *vtable        = &g_vtable;
        *className     = &g_className;
        st.installed   = true;
        LOG_INFO("type registries: CB's in place of Havok's (forwarding to vanilla, logging)");
    }

    void ReportTypeRegistries()
    {
        State& st = S();
        if (!st.installed) return;
        const bool ours = *Global(eng::hkTypeInfoRegistry_Global) == &g_typeInfo && *Global(eng::hkVtableClassRegistry_Global) == &g_vtable &&
                          *Global(eng::hkClassNameRegistry_Global) == &g_className;
        LOG_INFO("type registries: {}; {} type infos, {} vtables, {} classes registered; {} objects finished, {} cleaned up; "
                 "{} type-info, {} vtable, {} class-name lookups",
                 ours ? "still CB's" : "REPLACED since install", st.typesRegistered.load(), st.vtablesRegistered.load(),
                 st.classesRegistered.load(), st.finished.load(), st.cleanedUp.load(), st.typeLookups.load(), st.vtableLookups.load(),
                 st.classLookups.load());
        std::vector<std::pair<std::string, std::uint64_t>> byClass;
        {
            std::lock_guard guard(st.countsLock);
            byClass.assign(st.finishedByClass.begin(), st.finishedByClass.end());
        }
        std::stable_sort(byClass.begin(), byClass.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& [cls, count] : byClass) LOG_INFO("type registries:   {} {} finished", count, cls);
        for (std::size_t i = 0; i < st.ownedCount; ++i) {
            const Owned& o = st.owned[i];
            if (o.vanillaFinish)
                LOG_INFO("type registries: {} is CB's: {} loaded and {} cloned with CB's vtable", o.name, o.finished.load(), o.cloned.load());
            else
                LOG_WARN("type registries: {} is in sOwnTypes but never registered - nothing owned", o.name);
        }
    }

}  // namespace CB::platform::classes
