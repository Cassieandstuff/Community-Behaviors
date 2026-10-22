#pragma once

#include <PCH.h>

#include <Common/Base/Reflection/hkClass.h>

#include <cstdint>

/// @file
/// @brief Game functions of Havok's class reflection.

namespace CB::platform::game {

    /// @brief hkClass::getSignature: the CRC the engine derives from a class's reflection (its members and parent
    ///        chain), which the behavior loader compares against every packfile's stored class signatures.
    /// @param flags 1 = this class only (SIGNATURE_LOCAL); 0 = up the parent chain, as the loader asks.
    /// @note A free function over the class: havok/ declares only proven code.
    std::uint32_t hkClass_GetSignature(const CB::havok::hkClass* cls, int flags);

    /// @brief The exe's hkReferencedObject class (filled by its static initializer): the parent CB's own data classes
    ///        name, so the registries' parent-chain tests and the signature walk see the real class.
    extern const CB::havok::hkClass hkReferencedObjectClass;

}  // namespace CB::platform::game
