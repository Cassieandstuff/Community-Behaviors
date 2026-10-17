/// @file
/// @brief bhkWorld (Bethesda's wrapper of the physics world), ahkpWorld (the hkpWorld it wraps, pointing back) and
///        bhkWorldLock (the guard that holds the world's lock around Havok calls).
/// @note Not Havok: declared only as far as the character controller's code uses them.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkSerializable.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    /// @note Opaque beyond its vtable.
    class bhkWorld : public bhkSerializable
    {
    public:
        /// @brief The time the physics step about to run (or running) advances, in seconds; 0 when the frame's
        ///        accumulated time is below 1 ms.
        /// @note Name provisional.
        static float s_stepDeltaTime;

        /// @brief Takes / releases the world lock for writing / reading (no-ops unless bUseWorldLock:HAVOK).
        /// @note Names provisional.
        void HavokWorldEnterWrite();
        void HavokWorldExitWrite();
        void HavokWorldEnterRead();
        void HavokWorldExitRead();
    };

    /// @note 0x440 bytes.
    class ahkpWorld : public hkpWorld
    {
    public:
        /// @brief The bhkWorld wrapping this world.
        bhkWorld* m_bhkWorld;   ///< +0x430
        hkUint8   m_pad438[0x440 - 0x438];
    };

    /// @brief Holds `world`'s lock (for writing, or for reading) for the guard's lifetime; a null world, or one no
    ///        bhkWorld wraps, takes nothing.
    /// @note Name provisional. 0x10 bytes: vanilla keeps both members on the stack for the unwinder.
    class bhkWorldLock
    {
    public:
        ahkpWorld* m_world;   ///< +0x00
        /// @brief 1 for writing, 0 for reading.
        hkInt32    m_write;   ///< +0x08

        /// @brief Locks `world`'s own hkpWorld.
        HK_FORCE_INLINE bhkWorldLock(bhkWorld* world, hkInt32 write)
        :   m_write(write)
        {
            m_world = world ? world->GetWorld2() : HK_NULL;
            enter();
        }
        HK_FORCE_INLINE bhkWorldLock(hkpWorld* world, hkInt32 write)
        :   m_world(static_cast<ahkpWorld*>(world)),
            m_write(write)
        {
            enter();
        }
        HK_FORCE_INLINE void enter()
        {
            if (m_world && m_world->m_bhkWorld) {
                if (m_write) m_world->m_bhkWorld->HavokWorldEnterWrite();
                else         m_world->m_bhkWorld->HavokWorldEnterRead();
            }
        }
        HK_FORCE_INLINE ~bhkWorldLock()
        {
            if (m_world && m_world->m_bhkWorld) {
                if (m_write) m_world->m_bhkWorld->HavokWorldExitWrite();
                else         m_world->m_bhkWorld->HavokWorldExitRead();
            }
        }
    };
} }
