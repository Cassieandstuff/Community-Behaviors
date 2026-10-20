/// @file
/// @brief hkbDampingModifier: a PID controller that moves a damped value (scalar and/or vector) toward a raw one.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    class hkbDampingModifierInternalState : public hkReferencedObject
    {
    public:
        hkVector4 m_dampedVector;       ///< +0x10
        hkVector4 m_vecErrorSum;        ///< +0x20
        hkVector4 m_vecPreviousError;   ///< +0x30
        hkReal    m_dampedValue;        ///< +0x40
        hkReal    m_errorSum;           ///< +0x44
        hkReal    m_previousError;      ///< +0x48
    };

    /// @note Sized 0xc0. The `slot N` annotations are vanilla's vtable slots. The destructor is implicit: vanilla's
    ///       scalar deleting destructor (slot 0) inlines it.
    class hkbDampingModifier : public hkbModifier
    {
    public:
        hkReal    m_kP;                    ///< +0x50
        hkReal    m_kI;                    ///< +0x54
        hkReal    m_kD;                    ///< +0x58
        hkBool    m_enableScalarDamping;   ///< +0x5c
        hkBool    m_enableVectorDamping;   ///< +0x5d
        hkReal    m_rawValue;              ///< +0x60
        hkReal    m_dampedValue;           ///< +0x64
        hkVector4 m_rawVector;             ///< +0x70
        hkVector4 m_dampedVector;          ///< +0x80
        hkVector4 m_vecErrorSum;           ///< +0x90
        hkVector4 m_vecPreviousError;      ///< +0xa0
        hkReal    m_errorSum;              ///< +0xb0
        hkReal    m_previousError;         ///< +0xb4

        hkbDampingModifier();
        /// @brief The cloneNode copy: the settings and raw inputs, with the controller state cleared.
        hkbDampingModifier(const hkbDampingModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
