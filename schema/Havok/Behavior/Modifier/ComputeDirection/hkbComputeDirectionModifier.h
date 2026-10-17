/// @file
/// @brief hkbComputeDirectionModifier: expresses a world point in the character's frame, with its ground and up angles.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    class hkbComputeDirectionModifierInternalState : public hkReferencedObject
    {
    public:
        hkVector4 m_pointOut;         ///< +0x10
        hkReal    m_groundAngleOut;   ///< +0x20
        hkReal    m_upAngleOut;       ///< +0x24
        hkBool    m_computedOutput;   ///< +0x28
    };

    /// @note Sized 0x90; member names per the Havok SDK's reflection (no schema YAML for this class yet). The `slot N`
    ///       annotations are vanilla's vtable slots. The destructor is implicit: vanilla's scalar deleting destructor
    ///       (slot 0) inlines it.
    class hkbComputeDirectionModifier : public hkbModifier
    {
    public:
        hkVector4 m_pointIn;              ///< +0x50, world space
        hkVector4 m_pointOut;             ///< +0x60, model space
        hkReal    m_groundAngleOut;       ///< +0x70, degrees
        hkReal    m_upAngleOut;           ///< +0x74, degrees
        hkReal    m_verticalOffset;       ///< +0x78
        hkBool    m_reverseGroundAngle;   ///< +0x7c
        hkBool    m_reverseUpAngle;       ///< +0x7d
        hkBool    m_projectPoint;         ///< +0x7e
        hkBool    m_normalizePoint;       ///< +0x7f
        hkBool    m_computeOnlyOnce;      ///< +0x80
        hkBool    m_computedOutput;       ///< +0x81

        hkbComputeDirectionModifier();
        /// @brief The cloneNode copy: the settings, with no output computed.
        hkbComputeDirectionModifier(const hkbComputeDirectionModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
