/// @file
/// @brief hkbCombineTransformsModifier: multiplies two transforms (each optionally inverted, and the result too).
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    class hkbCombineTransformsModifierInternalState : public hkReferencedObject
    {
    public:
        hkVector4    m_translationOut;   ///< +0x10
        hkQuaternion m_rotationOut;      ///< +0x20
    };

    /// @note Sized 0xc0. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbCombineTransformsModifier : public hkbModifier
    {
    public:
        hkVector4    m_translationOut;         ///< +0x50
        hkQuaternion m_rotationOut;            ///< +0x60
        hkVector4    m_leftTranslation;        ///< +0x70
        hkQuaternion m_leftRotation;           ///< +0x80
        hkVector4    m_rightTranslation;       ///< +0x90
        hkQuaternion m_rightRotation;          ///< +0xa0
        hkBool       m_invertLeftTransform;    ///< +0xb0
        hkBool       m_invertRightTransform;   ///< +0xb1
        hkBool       m_invertResult;           ///< +0xb2

        hkbCombineTransformsModifier();
        /// @brief The cloneNode copy: every member, the outputs included.
        hkbCombineTransformsModifier(const hkbCombineTransformsModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
