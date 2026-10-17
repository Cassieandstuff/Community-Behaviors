/// @file
/// @brief bhkCharacterProxy: Bethesda's wrapper of an hkpCharacterProxy.
/// @note Not matched here: declared only as far as the proxy character controller's code uses it.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterPointCollector.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterProxyCinfo.h>
#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxy.h>

namespace CB { namespace schema
{
    /// @note 0x260 bytes (vanilla's deleting destructor frees 0x260). Vtable 0x1419edde8.
    class bhkCharacterProxy : public bhkSerializable
    {
    public:
        HK_FORCE_INLINE bhkCharacterProxy() : m_ignoredCollisionStartCollector(HK_NULL) { ++s_instanceCount; }
        /// @note Out of line in vanilla (0x140f18850).
        virtual ~bhkCharacterProxy();

        /// @note Inline, as vanilla's: its one copy sits with the controller unit that emits the vtable.
        virtual const NiRTTI* GetRTTI() const { return &ms_RTTI; }      ///< slot 2
        virtual NiObject*     CreateClone(NiCloningProcess& cloning);   ///< slot 23
        virtual void          LoadBinary(NiStream& stream);             ///< slot 24
        virtual void          LinkObject(NiStream& stream);             ///< slot 25
        virtual bool          RegisterStreamables(NiStream& stream);    ///< slot 26
        virtual void          SaveBinary(NiStream& stream);             ///< slot 27
        virtual void          AdjustRefCount(bool increment);           ///< slot 38
        virtual hkpWorld*     GetWorld1();                              ///< slot 39
        virtual ahkpWorld*    GetWorld2();                              ///< slot 40
        virtual void          FreeCinfo(bool free);                     ///< slot 43
        /// @note Inline, as GetRTTI.
        virtual hkUint32      GetCinfoSize() { return sizeof(bhkCharacterProxyCinfo); }   ///< slot 44
        virtual void          CreateFromCinfo(hkSerializableCinfo* cinfo); ///< slot 46
        virtual hkSerializableCinfo* GetCinfo(bool& created);           ///< slot 47

        /// @brief Stops the proxy listening to the bodies and phantoms it touches (forgetting them), under the world's
        ///        write lock, and resets m_ignoredCollisionStartCollector.
        /// @note Name provisional; out of line in vanilla (0x140f18df0).
        void ReleaseContacts();

        /// @brief The class's Gamebryo RTTI record.
        static const NiRTTI ms_RTTI;
        /// @brief How many are alive.
        /// @note Name provisional; vanilla bumps it (not atomically) in the constructor and drops it in the destructor.
        static hkUint32 s_instanceCount;

        /// @brief The wrapped proxy (the referenced object), null before one is built.
        HK_FORCE_INLINE hkpCharacterProxy* GetCharacterProxy() const
        {
            return static_cast<hkpCharacterProxy*>(m_referencedObject);
        }
        /// @brief The bhkWorld the wrapped proxy's world belongs to, null if none.
        HK_FORCE_INLINE bhkWorld* GetBhkWorld()
        {
            hkpWorld* world = GetWorld1();
            return world ? static_cast<ahkpWorld*>(world)->m_bhkWorld : HK_NULL;
        }

        /// @brief The wrapped proxy's position (zero before one is built), read under the world's read lock.
        HK_FORCE_INLINE const hkVector4& GetPosition()
        {
            const hkVector4*   position = &hkVector4::getZero();
            hkpCharacterProxy* proxy    = GetCharacterProxy();
            if (proxy) {
                bhkWorldLock lock(GetBhkWorld(), 0);
                position = &proxy->getPosition();
            }
            return *position;
        }
        /// @brief Teleports the wrapped proxy to `position` (forgetting its last displacement and its manifold), under
        ///        the world's write lock.
        HK_FORCE_INLINE void SetPosition(const hkVector4& position)
        {
            hkpCharacterProxy* proxy = GetCharacterProxy();
            if (proxy) {
                bhkWorldLock lock(GetBhkWorld(), 1);
                proxy->setPosition(position);
                proxy->m_oldDisplacement.setZero4();
                proxy->getManifold().clear();
            }
        }
        /// @brief Integrates the wrapped proxy over `stepInfo` under the world's gravity and write lock, with a cast
        ///        collector over m_ignoredCollisionStartCollector; then flushes that collector's events.
        /// @return Whether there was a proxy to integrate.
        HK_FORCE_INLINE bool Integrate(const hkStepInfo& stepInfo)
        {
            hkpCharacterProxy* proxy = GetCharacterProxy();
            if (!proxy) return false;
            bhkWorldLock lock(GetBhkWorld(), 1);
            bhkCharacterPointCollector castCollector(&m_ignoredCollisionStartCollector);
            proxy->integrateWithCollectors(stepInfo, GetWorld1()->m_gravity, castCollector, m_ignoredCollisionStartCollector);
            m_ignoredCollisionStartCollector.FlushEvents();
            return true;
        }
        /// @brief The wrapped proxy's support query in `direction` (nothing before one is built), under the world's
        ///        read lock.
        HK_FORCE_INLINE void CheckSupport(const hkVector4& direction, hkpSurfaceInfo& ground)
        {
            hkpCharacterProxy* proxy = GetCharacterProxy();
            if (proxy) {
                bhkWorldLock lock(GetBhkWorld(), 0);
                bhkCharacterPointCollector collector(&m_ignoredCollisionStartCollector);
                proxy->checkSupportWithCollector(direction, ground, collector);
            }
        }

        /// @brief Adds `listener` to the wrapped proxy (nothing before one is built), under the world's write lock.
        HK_FORCE_INLINE void AddListener(hkpCharacterProxyListener* listener)
        {
            hkpCharacterProxy* proxy = GetCharacterProxy();
            if (proxy) {
                bhkWorldLock lock(GetBhkWorld(), 1);
                proxy->addCharacterProxyListener(listener);
            }
        }
        /// @brief Removes `listener` from the wrapped proxy (nothing before one is built), under the world's write lock.
        HK_FORCE_INLINE void RemoveListener(hkpCharacterProxyListener* listener)
        {
            hkpCharacterProxy* proxy = GetCharacterProxy();
            if (proxy) {
                bhkWorldLock lock(GetBhkWorld(), 1);
                proxy->removeCharacterProxyListener(listener);
            }
        }

        /// @brief The start-point collector the proxy integrates and checks support with (it tracks the bodies the
        ///        proxy starts out overlapping).
        bhkCharacterPointCollector m_ignoredCollisionStartCollector;   ///< +0x20
    };
} }
