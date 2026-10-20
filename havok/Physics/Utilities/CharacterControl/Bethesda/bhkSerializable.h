/// @file
/// @brief bhkRefObject (a Gamebryo object owning one Havok object) and bhkSerializable (one that can live in a
///        bhkWorld and be built from a construction info).
/// @note Not Havok: declared only as their vtables need to be (slot names per CommonLibSSE where it has them).
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/NiObject.h>

namespace CB { namespace havok
{
    class hkpWorld;
    class ahkpWorld;
    class bhkWorld;

    /// @brief The interface every bhkSerializable's construction info adds to its Havok cinfo.
    /// @note Empty: in a cinfo that also derives from a (polymorphic) Havok cinfo it sits after that base, at its end.
    class hkSerializableCinfo
    {
    };

    /// @note 0x18 bytes.
    class bhkRefObject : public NiObject
    {
    public:
        /// @note Out of line in vanilla (0x140ebc8e0).
        bhkRefObject();

        virtual void SetReferencedObject(hkReferencedObject* object);   ///< slot 37
        virtual void AdjustRefCount(bool increment);                    ///< slot 38

        /// @brief The wrapped Havok object, referenced.
        hkReferencedObject* m_referencedObject;   ///< +0x10
    };

    /// @note 0x20 bytes.
    class bhkSerializable : public bhkRefObject
    {
    public:
        HK_FORCE_INLINE bhkSerializable() : m_cinfo(HK_NULL) {}
        /// @note Out of line in vanilla (0x140ebcc90).
        virtual ~bhkSerializable();

        virtual const NiRTTI* GetRTTI() const;                              ///< slot 2
        virtual void       LoadBinary(NiStream& stream);                   ///< slot 24
        virtual void       LinkObject(NiStream& stream);                   ///< slot 25
        virtual bool       RegisterStreamables(NiStream& stream);          ///< slot 26
        virtual void       SaveBinary(NiStream& stream);                   ///< slot 27
        virtual void       SetReferencedObject(hkReferencedObject* object);   ///< slot 37


        /// @brief The Havok world the wrapped object is in, null if none.
        virtual hkpWorld*  GetWorld1();                     ///< slot 39
        /// @brief The same world as Bethesda's ahkpWorld, null if none.
        virtual ahkpWorld* GetWorld2();                     ///< slot 40
        virtual void       MoveToWorld(bhkWorld* world);    ///< slot 41
        virtual void       RemoveFromCurrentWorld();        ///< slot 42
        /// @brief Frees the cached construction info (when `free`).
        /// @note Name provisional.
        virtual void       FreeCinfo(bool free);            ///< slot 43
        /// @brief The size of the class's cinfo.
        virtual hkUint32   GetCinfoSize();                  ///< slot 44 — name provisional
        virtual void       Unk_2D();                        ///< slot 45
        /// @brief Builds the wrapped Havok object from `cinfo` (the class's own construction info).
        virtual void       CreateFromCinfo(hkSerializableCinfo* cinfo) = 0;   ///< slot 46 — name provisional
        /// @brief The cached construction info, created (with `created` set) if there is none.
        virtual hkSerializableCinfo* GetCinfo(bool& created) = 0;           ///< slot 47 — name provisional
        virtual void       Unk_30();                        ///< slot 48
        /// @brief Called with the cinfo once CreateFromCinfo has built the wrapped object.
        virtual void       Unk_31(hkSerializableCinfo* cinfo);   ///< slot 49

        /// @brief Takes the global serialization lock (reentrant per thread).
        /// @note Name provisional; out of line in vanilla.
        static void LockSerialize();
        /// @note Name provisional; out of line in vanilla.
        static void UnlockSerialize();
        /// @brief Copies this object's state into `clone` for CreateClone.
        /// @note Name provisional; out of line in vanilla.
        void CopyMembers(bhkSerializable* clone, NiCloningProcess& cloning);

        /// @brief The cached construction info (see GetCinfo / FreeCinfo), null when none.
        hkSerializableCinfo* m_cinfo;   ///< +0x18
    };
} }
