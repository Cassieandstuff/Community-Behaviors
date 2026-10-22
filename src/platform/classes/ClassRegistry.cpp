#include "PCH.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/game/Reflection.h>
#include <platform/hooks/Live.h>

#include <interface/reflection/HavokSchema.h>

#include <Behavior/Generator/hkbGenerator.h>
#include <Common/Base/Memory/hkMemory.h>
#include <Common/Base/Reflection/hkClass.h>
#include <Common/Base/Reflection/hkClassMember.h>
#include <Common/Serialize/Util/hkBuiltinTypeRegistry.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

/// @note Two Havok globals a CB class's INLINE Havok code reads: hkArray's storage allocator
///       (hkContainerHeapAllocator::s_alloc, in hkArray growth and destructors, hkbBindable's implicit one included)
///       and the memory router's TLS index (in hkReferencedObject's class new and delete, which every base vtable's
///       deleting destructor uses). The link shim leaves mutable globals unbound because a copy goes stale; these two
///       do not change once Havok is up: the allocator is 8 bytes of static .data, its vtable pointer only (a
///       stateless forwarder to the thread's heap; AE 1.6.1170 0x142019838), and the TLS index is set once when Havok
///       starts. So, as the shim does for const globals, each decorated name binds to a plugin buffer, filled from the
///       exe by MirrorHavokGlobals before any CB class is registered (kDataLoaded).
extern "C" {
    alignas(8) unsigned char cb_mirror_containerHeapAllocator[8];
    alignas(4) unsigned char cb_mirror_memoryRouterTlsIndex[4];
}
#pragma comment(linker, "/alternatename:?s_alloc@hkContainerHeapAllocator@havok@CB@@2UAllocator@123@A=cb_mirror_containerHeapAllocator")
#pragma comment(linker, "/alternatename:?hkMemoryRouter_s_memoryRouter@havok@CB@@3KA=cb_mirror_memoryRouterTlsIndex")
static_assert(sizeof(CB::havok::hkContainerHeapAllocator::Allocator) == sizeof(cb_mirror_containerHeapAllocator));
static_assert(sizeof(CB::havok::hkMemoryRouter_s_memoryRouter) == sizeof(cb_mirror_memoryRouterTlsIndex));

namespace CB::platform::classes {

    namespace {

        namespace hk = CB::havok;
        namespace sc = CB::engine::schema;

        /// @brief Havok's hkTypeInfo, laid out as the exe's static initializers fill it (BSSynchronizedClipGenerator's
        ///        at 0x1400dc310): finish runs the finish constructor, cleanup calls virtual slot 0 with flag 0.
        struct TypeInfo {
            const char*   name;
            const char*   scopedName;   ///< "!" + name
            void          (*finish)(void*);
            void          (*cleanup)(void*);
            const void*   vtable;
            std::uint64_t size;
        };
        static_assert(sizeof(TypeInfo) == 0x30);

        /// @brief One class: everything the game keeps a pointer to lives here for the life of the process.
        struct Entry {
            ClassBehavior                 behavior{};
            std::string                   name, scopedName;
            std::vector<std::string>      memberNames;
            std::vector<hk::hkClassMember> members;
            hk::hkClass                   cls{};
            TypeInfo                      info{};
            bool                          registered = false;
        };

        /// @note A deque, so an entry never moves once the game holds pointers into it.
        struct State {
            std::mutex        lock;
            std::deque<Entry> entries;
        };
        State& S()
        {
            static State s;
            return s;
        }

        /// @brief The game's hkClass for a parent or a pointer member's target class.
        /// @note Only hkbGenerator so far. It is the link shim's copy of the exe's class: its name and parent chain
        ///       read the same, which is what the registries and the parent-chain class tests use.
        const hk::hkClass* GameClass(const std::string& name)
        {
            if (name == "hkbGenerator") return &hk::hkbGeneratorClass;
            return nullptr;
        }

        /// @brief `f` as a Havok member type; false with `why` set for a kind no CB class needs yet.
        bool MemberType(const sc::Field& f, hk::hkClassMember& m, std::string& why)
        {
            using M = hk::hkClassMember;
            switch (f.kind) {
            case sc::FieldKind::Scalar:
                switch (f.scalar) {
                case sc::Scalar::Int8:   m.m_type = M::TYPE_INT8; return true;
                case sc::Scalar::Byte:   m.m_type = M::TYPE_UINT8; return true;
                case sc::Scalar::Int16:  m.m_type = M::TYPE_INT16; return true;
                case sc::Scalar::UInt16: m.m_type = M::TYPE_UINT16; return true;
                case sc::Scalar::Int32:  m.m_type = M::TYPE_INT32; return true;
                case sc::Scalar::UInt32: m.m_type = M::TYPE_UINT32; return true;
                case sc::Scalar::Int64:  m.m_type = M::TYPE_INT64; return true;
                case sc::Scalar::UInt64: m.m_type = M::TYPE_UINT64; return true;
                case sc::Scalar::Float:  m.m_type = M::TYPE_REAL; return true;
                case sc::Scalar::Half:   m.m_type = M::TYPE_HALF; return true;
                case sc::Scalar::Bool:   m.m_type = M::TYPE_BOOL; return true;
                }
                break;
            case sc::FieldKind::String: m.m_type = M::TYPE_STRINGPTR; return true;
            case sc::FieldKind::EmptyPtr:   m.m_type = M::TYPE_POINTER; m.m_subtype = M::TYPE_VOID; return true;
            case sc::FieldKind::EmptyArray: m.m_type = M::TYPE_ARRAY;   m.m_subtype = M::TYPE_VOID; return true;
            case sc::FieldKind::Ptr:
            case sc::FieldKind::PtrArray:
                m.m_type    = f.kind == sc::FieldKind::Ptr ? M::TYPE_POINTER : M::TYPE_ARRAY;
                m.m_subtype = f.kind == sc::FieldKind::Ptr ? M::TYPE_STRUCT : M::TYPE_POINTER;
                m.m_class   = const_cast<hk::hkClass*>(GameClass(f.ref));
                if (!m.m_class) { why = "no game class for '" + f.ref + "'"; return false; }
                return true;
            default: break;
            }
            why = "no Havok member type for its field kind here";
            return false;
        }

        /// @brief Whether a field is a member at all: padding is not, nor an unnamed void-typed ignored slot.
        /// @note  A NAMED void slot is a SERIALIZE_IGNORED member (the way Havok reflects its own runtime pointers, as
        ///        hkbBindable's cachedBindables): the schema hashes it into the class signature, so the registered class
        ///        must carry it too, or the engine computes another signature and refuses every file holding the class.
        bool IsMember(const sc::Field& f)
        {
            switch (f.kind) {
            case sc::FieldKind::Pad:
            case sc::FieldKind::Skip:
            case sc::FieldKind::Vtable: return false;
            case sc::FieldKind::EmptyArray:
            case sc::FieldKind::EmptyPtr: return !f.name.empty();
            default: return true;
            }
        }

        /// @brief Fill `e`'s hkClass, members and type info from the schema; false with `why` set when it can't.
        bool Build(Entry& e, const sc::SchemaRegistry& schema, std::string& why)
        {
            const sc::ClassSchema* cs = schema.Find(e.name);
            if (!cs) { why = "no schema descriptor"; return false; }
            const hk::hkClass* parent = GameClass(cs->parent);
            if (!parent) { why = "no game class for its parent '" + cs->parent + "'"; return false; }
            const int size = schema.ComputeSize(e.name, &why);
            if (size < 0) return false;
            if (static_cast<std::size_t>(size) != e.behavior.size) {
                why = "the descriptor lays out " + std::to_string(size) + " bytes, the C++ class " + std::to_string(e.behavior.size);
                return false;
            }
            const std::vector<int> offsets = schema.OwnFieldOffsets(e.name, &why);
            if (offsets.size() != cs->fields.size()) return false;

            /// @note Names first, members after: a member keeps its name's c_str(), so the names never reallocate later.
            for (const auto& f : cs->fields)
                if (IsMember(f)) e.memberNames.push_back(f.name);
            std::size_t named = 0;
            for (std::size_t i = 0; i < cs->fields.size(); ++i) {
                const sc::Field& f = cs->fields[i];
                if (!IsMember(f)) continue;
                hk::hkClassMember m{};
                m.m_name   = e.memberNames[named++].c_str();
                m.m_offset = static_cast<hk::hkUint16>(offsets[i]);
                const bool empty = f.kind == sc::FieldKind::EmptyPtr || f.kind == sc::FieldKind::EmptyArray;
                m.m_flags  = static_cast<hk::hkUint16>(f.ignored || empty ? hk::hkClassMember::SERIALIZE_IGNORED : 0);
                if (!MemberType(f, m, why)) { why = "member '" + f.name + "': " + why; return false; }
                e.members.push_back(m);
            }

            e.cls.m_name               = e.name.c_str();
            e.cls.m_parent             = parent;
            e.cls.m_objectSize         = size;
            e.cls.m_declaredMembers    = e.members.data();
            e.cls.m_numDeclaredMembers = static_cast<int>(e.members.size());
            e.cls.m_describedVersion   = 1;
            e.cls.m_numImplementedInterfaces = cs->interfaces;

            e.info.name       = e.name.c_str();
            e.info.scopedName = e.scopedName.c_str();
            e.info.finish     = e.behavior.finish;
            e.info.cleanup    = e.behavior.cleanup;
            e.info.vtable     = e.behavior.vtable();
            e.info.size       = e.behavior.size;
            return true;
        }

        /// @brief Fill the two mirrors (see the file's note) from the exe's variables.
        void MirrorHavokGlobals()
        {
            std::memcpy(cb_mirror_containerHeapAllocator, &hooks::Live<&hk::hkContainerHeapAllocator::s_alloc>(),
                        sizeof(cb_mirror_containerHeapAllocator));
            std::memcpy(cb_mirror_memoryRouterTlsIndex, &hooks::Live<&hk::hkMemoryRouter_s_memoryRouter>(),
                        sizeof(cb_mirror_memoryRouterTlsIndex));
            LOG_INFO("classes: Havok's container allocator and memory router index mirrored for CB's classes");
        }

    }  // namespace

    bool Declare(const ClassBehavior& behavior)
    {
        std::scoped_lock lock(S().lock);
        Entry& e     = S().entries.emplace_back();
        e.behavior   = behavior;
        e.name       = behavior.name;
        e.scopedName = "!" + e.name;
        return true;
    }

    void RegisterAll(const sc::SchemaRegistry& schema)
    {
        std::scoped_lock lock(S().lock);
        hk::hkBuiltinTypeRegistry* registry = hooks::Live<&hk::hkBuiltinTypeRegistry::s_instance>();
        if (!registry) {
            LOG_ERROR("classes: Havok's type registry does not exist yet - {} CB class(es) not registered", S().entries.size());
            return;
        }
        MirrorHavokGlobals();
        for (Entry& e : S().entries) {
            if (e.registered) continue;
            std::string why;
            if (!Build(e, schema, why)) {
                LOG_ERROR("classes: {} not registered: {}", e.name, why);
                continue;
            }
            /// @note addType is the registry's virtual slot 6 (+0x30): 0x140b143d0 in the vtable at 0x141966200 (AE
            ///       1.6.1170, verified 2026-10-20). It files the class by name, the type info, and, since the parent
            ///       chain implements an interface, the vtable -> class map the graph's clone fixups read.
            using AddType = void (*)(hk::hkBuiltinTypeRegistry*, TypeInfo*, const hk::hkClass&);
            const auto addType = (*reinterpret_cast<AddType* const*>(registry))[6];
            addType(registry, &e.info, e.cls);
            e.registered = true;
            /// @note The behavior loader hashes this hkClass and compares the result with the signature the schema
            ///       wrote into every packfile; on a mismatch the file is refused and the project's remaining
            ///       behaviors with it. Registered either way: a refused file is an A-pose, an unregistered class a crash.
            const std::uint32_t engineSig = game::hkClass_GetSignature(&e.cls, 0);
            const std::uint32_t schemaSig = schema.Find(e.name)->signature;
            if (engineSig != schemaSig)
                LOG_ERROR("classes: {} signature mismatch - the engine computes {:#010x}, the schema writes {:#010x}: "
                          "every behavior holding it is refused at load", e.name, engineSig, schemaSig);
            LOG_INFO("classes: registered {} ({} bytes, {} member(s), signature {:#010x})", e.name, e.cls.m_objectSize,
                     e.members.size(), engineSig);
        }
    }

    bool Registered(const char* name)
    {
        std::scoped_lock lock(S().lock);
        for (const Entry& e : S().entries)
            if (e.name == name) return e.registered;
        return false;
    }

    const void* ClassOf(const char* name)
    {
        std::scoped_lock lock(S().lock);
        for (const Entry& e : S().entries)
            if (e.name == name) return e.registered ? &e.cls : nullptr;
        return nullptr;
    }

}  // namespace CB::platform::classes
