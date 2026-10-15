/// @file
/// @brief hkbTransformVectorModifier: transforms an input vector by a rotation and translation (or their inverse).
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    class hkbTransformVectorModifierInternalState : public hkReferencedObject
    {
    public:
        hkVector4 m_vectorOut;   ///< +0x10
    };

    /// @note Sized 0xa0. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbTransformVectorModifier : public hkbModifier
    {
    public:
        hkQuaternion m_rotation;            ///< +0x50
        hkVector4    m_translation;         ///< +0x60
        hkVector4    m_vectorIn;            ///< +0x70
        hkVector4    m_vectorOut;           ///< +0x80
        hkBool       m_rotateOnly;          ///< +0x90
        hkBool       m_inverse;             ///< +0x91
        hkBool       m_computeOnActivate;   ///< +0x92
        hkBool       m_computeOnModify;     ///< +0x93

        hkbTransformVectorModifier();
        /// @brief The cloneNode copy: the settings, with the output cleared.
        hkbTransformVectorModifier(const hkbTransformVectorModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23

    };

    /// @note The heavy halves of several modifiers sit together outside their classes' TUs (0x140b0c450–0x140b0fb0f); each
    ///       is a `<Class>Utils` here, as BSSynchronizedClipGeneratorUtils is (an exe path) and BSLookAtModifierUtils.
    namespace hkbTransformVectorModifierUtils
    {
        /// @brief m_vectorOut = m_vectorIn transformed by (m_translation, m_rotation), or by its inverse; rotation only
        ///        under m_rotateOnly.
        /// @note Name provisional (0x140b0f2e0); pinned, not reimplemented yet.
        void computeVectorOut(hkbTransformVectorModifier& modifier);
    }
} }
