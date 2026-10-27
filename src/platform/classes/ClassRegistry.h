#pragma once

#include <cstddef>

namespace CB::engine::schema { class SchemaRegistry; }

/// @file
/// @brief CB's own Havok classes registered with the game: a class's LAYOUT comes from its schema descriptor (the one
///        the compiler serializes it by), its BEHAVIOR from the C++ class, so the two halves can never describe
///        different objects.
/// @note  How the game finishes a loaded object (RE'd, AE 1.6.1170): the behavior loader places each object in memory
///        and asks the type-info registry to finish it by its class NAME (case-sensitive). A name nobody registered is
///        skipped silently, leaving a null vtable that crashes on the first virtual call; so a class must be registered
///        before any behavior holding it loads.

namespace CB::platform::classes {

    /// @brief What the schema cannot say about a class: how to finish, destroy and recognize a C++ instance.
    struct ClassBehavior {
        const char* name;               ///< the schema descriptor's name, exactly
        std::size_t size;               ///< sizeof the C++ class; must equal the descriptor's size
        void (*finish)(void* object);   ///< placement-new of the finish constructor on loaded bytes
        void (*cleanup)(void* object);  ///< the destructor without the delete
        const void* (*vtable)();        ///< a finished instance's vtable
    };

    /// @brief Queue `behavior` for RegisterAll.
    /// @return true, so a class can declare itself from a static initializer.
    bool Declare(const ClassBehavior& behavior);

    /// @brief Build each declared class's hkClass and members from `schema` and add it to the game's type registries.
    /// @pre   Havok's type registry exists and no behavior has loaded yet: Plugin.cpp calls it at kDataLoaded.
    /// @note  A class that cannot be registered (no descriptor, a size mismatch, a field kind with no Havok member
    ///        type here) is logged and skipped; a behavior holding it must then not be served.
    void RegisterAll(const CB::engine::schema::SchemaRegistry& schema);

    /// @brief Fills the plugin's copies of the two Havok globals inline Havok code reads (hkArray's allocator, the
    ///        memory router's TLS index) from the exe, once.
    /// @pre   Havok's memory system is up: any graph loading or running implies it.
    /// @note  RegisterAll calls it at kDataLoaded, but graphs load before that (the main menu's AutoplayBehavior): a
    ///        feature running inline Havok allocation outside a CB class (its copies of havok/ functions, a graph walk)
    ///        calls it first. Without it, hkArray growth there dereferences a null allocator (2026-10-25 crash).
    void EnsureHavokGlobalsMirrored();

    /// @return the offset of `member` in vanilla class `cls`, when RegisterAll extended it with that member (a
    ///         `cb: true` field of its schema descriptor); -1 otherwise, and always before RegisterAll has run. Names
    ///         compared exactly. Lock-free: callable from any thread, per object.
    /// @note  RegisterAll also extends the vanilla classes the schema adds members to, in the exe's own hkClass, so
    ///        the engine's description (and the signature it checks packfiles against) matches what CB writes.
    int MemberOffset(const char* cls, const char* member);

    /// @brief Whether `name` was registered by RegisterAll.
    bool Registered(const char* name);

    /// @brief The hkClass built for `name` (what its getClassType returns), or null before it is registered.
    const void* ClassOf(const char* name);

}  // namespace CB::platform::classes
