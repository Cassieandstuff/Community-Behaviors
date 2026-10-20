/// @file
/// @brief hkpSimpleConstraintContactMgr: the default dynamics contact manager (one contact constraint per agent).
#pragma once

#include <Physics/Dynamics/Collide/hkpDynamicsContactMgr.h>
#include <Physics/Dynamics/Constraint/hkpConstraintInstance.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/Contact/hkpSimpleContactConstraintAtom.h>

namespace CB { namespace havok
{
    /// @note 0xf0 bytes per Havok's reflection (which lists no members and skips the unreflected bases). The contact
    ///       constraint data (hkpSimpleContactConstraintData) sits at +0x28; only its id table (+0x40) and atom (+0x68)
    ///       are declared, then the constraint instance it runs in.
    class hkpSimpleConstraintContactMgr : public hkpDynamicsContactMgr
    {
    public:
        enum { FREE_CONTACT_POINT_SLOT = 0xff };

        hkUint8                         m_pad18[0x40 - 0x18];
        /// @brief The id manager's table (hkpDynamicsCpIdMgr::m_values): per contact point id, the index of its contact
        ///        point in the atom, or FREE_CONTACT_POINT_SLOT.
        hkArray<hkUint8>                m_contactPointIdSlots;      ///< +0x40
        hkUint8                         m_pad50[0x68 - 0x50];
        hkpSimpleContactConstraintAtom* m_contactConstraintAtom;    ///< +0x68
        hkUint8                         m_pad70[0x78 - 0x70];
        hkpConstraintInstance           m_constraint;               ///< +0x78
        hkUint8                         m_padE8[0xf0 - 0xe8];

        /// @brief How many contact point ids getAllContactPointIds can return at most.
        HK_FORCE_INLINE int getMaxNumContactPointIds() const { return m_contactPointIdSlots.getSize(); }
        /// @brief The id whose slot holds contact point `index`, -1 if none (searched from the end).
        HK_FORCE_INLINE int findContactPointId(int index) const
        {
            int id = m_contactPointIdSlots.getSize() - 1;
            for (; id >= 0; id--) {
                if (m_contactPointIdSlots[id] == index) break;
            }
            return id;
        }
        /// @brief `id`'s property record, null for a free id.
        HK_FORCE_INLINE hkpContactPointProperties* getContactPointPropertiesInline(hkContactPointId id) const
        {
            const int slot = m_contactPointIdSlots[id];
            if (slot == FREE_CONTACT_POINT_SLOT) return HK_NULL;
            return reinterpret_cast<hkpContactPointProperties*>(m_contactConstraintAtom->getContactPointPropertiesStream(static_cast<hkInt16>(slot)));
        }
        HK_FORCE_INLINE hkContactPoint& getContactPointInline(hkContactPointId id) const
        {
            return m_contactConstraintAtom->getContactPoints()[m_contactPointIdSlots[id]];
        }
    };
} }
