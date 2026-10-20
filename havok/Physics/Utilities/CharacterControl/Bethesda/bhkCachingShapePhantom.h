/// @file
/// @brief bhkPhantom, bhkShapePhantom and bhkCachingShapePhantom: Bethesda's wrappers of an hkpPhantom, an
///        hkpShapePhantom and an hkpCachingShapePhantom.
/// @note Declared only as far as the proxy character controller's code uses them.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkRigidBody.h>
#include <Physics/Dynamics/Phantom/hkpCachingShapePhantom.h>

namespace CB { namespace havok
{
    /// @note 0x28 bytes: no members of its own. Vtable 0x1417adc80.
    class bhkPhantom : public bhkWorldObject
    {
    public:
        HK_FORCE_INLINE bhkPhantom() { ++s_instanceCount; }

        /// @brief How many have been constructed.
        /// @note Name provisional; vanilla bumps it (not atomically) in the constructor.
        static hkUint32 s_instanceCount;
    };

    /// @note 0x30 bytes. Vtable 0x1417af068.
    class bhkShapePhantom : public bhkPhantom
    {
    public:
        HK_FORCE_INLINE bhkShapePhantom() : m_unk28(false) { ++s_instanceCount; }

        /// @brief Moves the phantom to `transform`, under its world's write lock.
        /// @note Name provisional; out of line in vanilla (0x140ec16b0).
        void SetTransform(const hkTransform& transform);
        /// @brief Moves the phantom's translation to `position`, under its world's write lock.
        /// @note Name provisional; out of line in vanilla (0x140ec1740).
        void SetPosition(const hkVector4& position);

        /// @note Cleared by the constructors; meaning unknown.
        bool    m_unk28;   ///< +0x28
        hkUint8 m_pad29[0x30 - 0x29];

        /// @brief How many have been constructed.
        /// @note Name provisional; vanilla bumps it (not atomically) in the constructor.
        static hkUint32 s_instanceCount;
    };

    /// @brief What a bhkShapePhantom's Havok phantom is built from.
    /// @note 0x70 bytes. Name and layout past the shape provisional (only Init, which builds one on the stack, is seen).
    struct bhkShapePhantomCinfo
    {
        HK_FORCE_INLINE bhkShapePhantomCinfo() : m_shape(HK_NULL), m_unk10(2)
        {
            hkRotation& rotation = m_transform.m_rotation;
            rotation.m_col0(0) = 1.0f; rotation.m_col0(1) = 0.0f; rotation.m_col0(2) = 0.0f; rotation.m_col0(3) = 0.0f;
            rotation.m_col1(0) = 0.0f; rotation.m_col1(1) = 1.0f; rotation.m_col1(2) = 0.0f; rotation.m_col1(3) = 0.0f;
            rotation.m_col2(0) = 0.0f; rotation.m_col2(1) = 0.0f; rotation.m_col2(2) = 1.0f; rotation.m_col2(3) = 0.0f;
            m_transform.m_translation.setZero4();
        }

        hkUint32       m_collisionFilterInfo;   ///< +0x00
        const hkpShape* m_shape;                ///< +0x08
        /// @note Init stores 2.
        hkUint8        m_unk10;                 ///< +0x10
        hkArray<void*> m_unk18;                 ///< +0x18
        /// @note Init stores the identity rotation (as scalar stores, not vectors) and a zero translation.
        hkTransform    m_transform;             ///< +0x30
    };

    /// @note 0x30 bytes: no members of its own. Vtable 0x1419ed858.
    class bhkCachingShapePhantom : public bhkShapePhantom
    {
    public:
        /// @brief Builds the Havok phantom from `cinfo`.
        HK_FORCE_INLINE explicit bhkCachingShapePhantom(bhkShapePhantomCinfo& cinfo)
        {
            CreateFromCinfo(reinterpret_cast<hkSerializableCinfo*>(&cinfo));
            ++s_instanceCount;
            m_unk28 = false;
        }

        virtual void CreateFromCinfo(hkSerializableCinfo* cinfo);   ///< slot 46
        virtual hkSerializableCinfo* GetCinfo(bool& created);       ///< slot 47

        /// @brief The wrapped Havok phantom (the referenced object).
        HK_FORCE_INLINE hkpCachingShapePhantom* GetPhantom() const { return static_cast<hkpCachingShapePhantom*>(m_referencedObject); }

        /// @brief How many have been constructed.
        /// @note Name provisional; vanilla bumps it (not atomically) in the constructor.
        static hkUint32 s_instanceCount;
    };
} }
