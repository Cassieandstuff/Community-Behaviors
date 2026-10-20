/// @file
/// @brief hkpCharacterProxyListener: what a character proxy tells about its contacts and interactions as it integrates.
#pragma once

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>

namespace CB { namespace havok
{
    class hkpCharacterProxy;
    struct hkSimplexSolverInput;
    struct hkpCharacterObjectInteractionEvent;
    struct hkpCharacterObjectInteractionResult;

    /// @note 8 bytes; vtable 0x1419edd90 (slots in declaration order, per the Havok SDK; every one but the destructor
    ///       an empty body in vanilla). Not reference counted: it is allocated from the Havok heap by its size.
    class hkpCharacterProxyListener
    {
    public:
        HK_FORCE_INLINE void* operator new(hkUlong nbytes) { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void* operator new(hkUlong, void* p) { return p; }
        HK_FORCE_INLINE void  operator delete(void*, void*) {}

        virtual ~hkpCharacterProxyListener() {}   ///< slot 0

        /// @brief Lets the listener edit the simplex solver's planes built from the proxy's manifold.
        virtual void processConstraintsCallback(const hkpCharacterProxy* proxy, const hkArray<hkRootCdPoint>& manifold,
                                                hkSimplexSolverInput& input) {}                                          ///< slot 1
        virtual void contactPointAddedCallback(const hkpCharacterProxy* proxy, const hkRootCdPoint& point) {}             ///< slot 2
        virtual void contactPointRemovedCallback(const hkpCharacterProxy* proxy, const hkRootCdPoint& point) {}           ///< slot 3
        /// @brief The proxy touched another character proxy.
        virtual void characterInteractionCallback(hkpCharacterProxy* proxy, hkpCharacterProxy* otherProxy,
                                                  const hkContactPoint& contact) {}                                       ///< slot 4
        /// @brief The proxy pushes a body: `input` is what it touched, `output` the impulse to apply.
        virtual void objectInteractionCallback(hkpCharacterProxy* proxy, const hkpCharacterObjectInteractionEvent& input,
                                               hkpCharacterObjectInteractionResult& output) {}                            ///< slot 5
    };
} }
