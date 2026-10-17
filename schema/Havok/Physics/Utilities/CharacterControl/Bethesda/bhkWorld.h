/// @file
/// @brief bhkWorld (Bethesda's wrapper of the physics world), ahkpWorld (the hkpWorld it wraps, pointing back) and
///        bhkWorldLock (the guard that holds the world's lock around Havok calls).
/// @note Not Havok: declared only as far as the character controller's code uses them.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkSerializable.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    struct bhkPickData;

    /// @note 0xc600 bytes; opaque but for the members the character controller reads.
    class bhkWorld : public bhkSerializable
    {
    public:
        virtual void Unk_32();                           ///< slot 50
        /// @brief Casts `pickData`'s ray against the world into its output.
        /// @note Name per CommonLibSSE.
        virtual bool PickObject(bhkPickData& pickData);   ///< slot 51

        /// @brief Havok units to game units (a game unit is 1/69.99125 of a Havok unit).
        /// @note Name provisional; a constant in vanilla's .rdata.
        static const float s_worldScaleInverse;

        /// @brief The Havok world's gravity (read under the world lock), zero when the world has none.
        /// @note Name provisional; out of line in vanilla (0x140e84230).
        const hkVector4& GetGravity() const;

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

        hkUint8  m_pad20[0xc5ec - 0x20];
        /// @brief While nonzero, a character controller flagged kComputeTiltPreIntegrate skips its frame.
        /// @note Name provisional (CommonLibSSE's unkC5EC).
        hkUint32 m_unkc5ec;   ///< +0xc5ec
        hkUint8  m_padc5f0[0xc600 - 0xc5f0];
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
