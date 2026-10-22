/// @file
/// @brief hkbLookAtModifier: turns the head (rotating at the head bone, limited about the neck) toward a world target.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    class hkbLookAtModifierInternalState : public hkReferencedObject
    {
    public:
        hkVector4 m_lookAtLastTargetWS;        ///< +0x10
        hkReal    m_lookAtWeight;              ///< +0x20
        hkBool    m_isTargetInsideLimitCone;   ///< +0x24
    };

    /// @note Sized 0xf0; member names per the Havok SDK's reflection (no schema YAML for this class; the internal
    ///       state's names are in the exe). The `slot N` annotations are vanilla's vtable slots. The destructor is
    ///       implicit.
    class hkbLookAtModifier : public hkbModifier
    {
    public:
        hkVector4 m_targetWS;                  ///< +0x50
        hkVector4 m_headForwardLS;             ///< +0x60
        hkVector4 m_neckForwardLS;             ///< +0x70
        hkVector4 m_neckRightLS;               ///< +0x80
        hkVector4 m_eyePositionHS;             ///< +0x90, head space
        hkReal    m_newTargetGain;             ///< +0xa0
        hkReal    m_onGain;                    ///< +0xa4
        hkReal    m_offGain;                   ///< +0xa8
        hkReal    m_limitAngleDegrees;         ///< +0xac
        hkReal    m_limitAngleLeft;            ///< +0xb0, degrees
        hkReal    m_limitAngleRight;           ///< +0xb4
        hkReal    m_limitAngleUp;              ///< +0xb8
        hkReal    m_limitAngleDown;            ///< +0xbc
        hkInt16   m_headIndex;                 ///< +0xc0
        hkInt16   m_neckIndex;                 ///< +0xc2
        hkBool    m_isOn;                      ///< +0xc4
        hkBool    m_individualLimitsOn;        ///< +0xc5
        hkBool    m_isTargetInsideLimitCone;   ///< +0xc6
        hkVector4 m_lookAtLastTargetWS;        ///< +0xd0
        hkReal    m_lookAtWeight;              ///< +0xe0

        hkbLookAtModifier();
        /// @brief The cloneNode copy: the settings, with no target and no weight.
        hkbLookAtModifier(const hkbLookAtModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        /// @note Both bones must be set (or bound).
        virtual bool                isValid(hkStringPtr& errorString) const;                          ///< slot 10
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        /// @brief Eases m_lookAtWeight toward 1 (on, target ahead of the head) or 0, chasing m_targetWS, and solves
        ///        the head toward the chased target.
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
