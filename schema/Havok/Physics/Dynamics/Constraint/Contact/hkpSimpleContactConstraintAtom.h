#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x20, version 1).
    struct hkpSimpleContactConstraintDataInfo
    {
    public:
        hkUint16 m_flags;   ///< +0x0
        hkUint16 m_index;   ///< +0x2
        hkReal m_internalData0;   ///< +0x4
        hkHalf m_rollingFrictionMultiplier;   ///< +0x8
        hkHalf m_internalData1;   ///< +0xa
        hkUint32 m_data[5];   ///< +0xc
    };

    /// @brief A contact constraint's atom: its header, then the contact points (0x20 each, the reserved count), then
    ///        their property records (striding bytes each).
    /// @note Per Havok's reflection (size 0x30).
    struct __declspec(align(16)) hkpSimpleContactConstraintAtom : public hkpConstraintAtom
    {
    public:
        hkUint16 m_sizeOfAllAtoms;   ///< +0x2
        hkUint16 m_numContactPoints;   ///< +0x4
        hkUint16 m_numReservedContactPoints;   ///< +0x6
        hkUint8 m_numUserDatasForBodyA;   ///< +0x8
        hkUint8 m_numUserDatasForBodyB;   ///< +0x9
        hkUint8 m_contactPointPropertiesStriding;   ///< +0xa
        hkUint16 m_maxNumContactPoints;   ///< +0xc
        hkpSimpleContactConstraintDataInfo m_info;   ///< +0x10

        HK_FORCE_INLINE hkContactPoint* getContactPoints() const { return reinterpret_cast<hkContactPoint*>(const_cast<hkpSimpleContactConstraintAtom*>(this) + 1); }
        /// @brief Contact `index`'s property record (after all the reserved contact points).
        HK_FORCE_INLINE hkUint8* getContactPointPropertiesStream(int index) const
        {
            return reinterpret_cast<hkUint8*>(getContactPoints() + m_numReservedContactPoints) + index * m_contactPointPropertiesStriding;
        }
    };
} }
