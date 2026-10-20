/// @file
/// @brief bhkCharacterControllerCinfo: the construction info of a Bethesda character controller (either kind).
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/MemoryManager.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkShape.h>

namespace CB { namespace havok
{
    class bhkWorld;

    /// @note 0xb0 bytes; vtable 0x1419e4300 (slots 1 to 5 pure).
    class bhkCharacterControllerCinfo
    {
    public:
        CB_BETHESDA_HEAP_ALIGNED_NEW_DELETE

        /// @note Name provisional for both flags: `createStateManager` (the shared hkpCharacterStateManager is
        ///       created on first use and referenced), `unkA5` (kept at +0xa5). Out of line in vanilla (0x140e9c960).
        bhkCharacterControllerCinfo(bool createStateManager, bool unkA5);
        /// @note Out of line in vanilla (0x140e9ccf0).
        virtual ~bhkCharacterControllerCinfo();

        /// @brief Sets the Havok cinfo's starting position.
        virtual void  SetPosition(const hkVector4& position) = 0;                  ///< slot 1
        /// @note The proxy controller's cinfo reads and writes its proxy cinfo's +0x60 / +0x64 through slots 2 to 4;
        ///       the rigid-body controller's returns 0 and ignores the write.
        virtual float Unk_02() const = 0;                                         ///< slot 2
        virtual float Unk_03() const = 0;                                         ///< slot 3
        virtual void  Unk_04(float value) = 0;                                    ///< slot 4
        virtual float Unk_05() const = 0;                                         ///< slot 5

        hkUint8   m_pad08[0x18 - 0x08];
        hkUint32  m_collisionFilterInfo;   ///< +0x18
        hkUint8   m_pad1c[0x20 - 0x1c];
        /// @brief The world to put the controller in once built, null for none.
        bhkWorld* m_world;                 ///< +0x20
        hkUint8   m_pad28[0x38 - 0x28];
        /// @brief Counted references of unknown type (the destructor releases them).
        NiPointer<NiRefObject>         m_unk38;          ///< +0x38
        NiPointer<NiRefObject>         m_unk40;          ///< +0x40
        /// @brief The character state manager (an hkpCharacterStateManager) the controller runs its states with.
        hkRefPtr<hkReferencedObject>   m_stateManager;   ///< +0x48
        hkUint8                        m_pad50[0x90 - 0x50];
        /// @brief The controller's shapes.
        NiPointer<bhkShape>            m_shapes[2];      ///< +0x90
        hkUint8                        m_pada0[0xb0 - 0xa0];
    };
} }
