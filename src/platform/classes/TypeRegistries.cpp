#include "PCH.h"

#include <platform/classes/TypeRegistries.h>

#include "core/debug/Settings.h"
#include <platform/hooks/registry/Registry.h>

#include <PluginLogger.h>

#include <algorithm>
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

        /// @brief The vanilla code each slot forwards to, and what the registries have seen.
        struct State {
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
                return Vanilla(S().typeInfoVt, 3, Ready(), info);
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
                return Vanilla(S().vtableVt, 3, Ready(), vtable, cls);
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
        st.typeInfoVt  = reinterpret_cast<const Word*>(RE::VTABLE_hkTypeInfoRegistry[0].address());
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
    }

}  // namespace CB::platform::classes
