/// @file
/// @brief bhkCharProxyController: Bethesda's character controller over Havok's character proxy (a shape phantom moved
///        through the world). Skyrim drives the player with it unless bUseCharacterRB. It is also its proxy's listener.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterProxy.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCachingShapePhantom.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/Bethesda/BSSpinLock.h>
#include <Physics/Utilities/CharacterControl/Bethesda/BSTArray.h>
#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxyListener.h>

namespace CB { namespace schema
{
    /// @note 0x5b0 bytes; vtables 0x1419edfb8 (the hkpCharacterProxyListener base, +0x00) and 0x1419edff0 (the
    ///       bhkCharacterController base, +0x10).
    class bhkCharProxyController : public hkpCharacterProxyListener, public bhkCharacterController
    {
    public:
        using bhkCharacterController::operator new;
        using bhkCharacterController::operator delete;

        /// @brief Builds the controller from `cinfo` (which Init completes and passes on to the Havok proxy) and adds
        ///        it to s_controllers.
        explicit bhkCharProxyController(bhkCharProxyControllerCinfo& cinfo);
        /// @brief Takes the controller out of s_controllers and stops listening to its proxy.
        virtual ~bhkCharProxyController();

        virtual void processConstraintsCallback(const hkpCharacterProxy* proxy, const hkArray<hkRootCdPoint>& manifold,
                                                hkpSimplexSolverInput& input);
        /// @brief Counts a new character-controller contact (layer 30) of the controller's own collision group, if it
        ///        has one, stopping the controller when m_stopOnCharacterContact.
        virtual void contactPointAddedCallback(const hkpCharacterProxy* proxy, const hkRootCdPoint& point);
        /// @brief Uncounts a character-controller contact counted by contactPointAddedCallback.
        virtual void contactPointRemovedCallback(const hkpCharacterProxy* proxy, const hkRootCdPoint& point);
        /// @brief Pushes the other proxy's controller and tells bhkCharacterCollisionHandler.
        virtual void characterInteractionCallback(hkpCharacterProxy* proxy, hkpCharacterProxy* otherProxy, const hkContactPoint& contact);
        virtual void objectInteractionCallback(hkpCharacterProxy* proxy, const hkpCharacterObjectInteractionEvent& input,
                                               hkpCharacterObjectInteractionResult& output);

        virtual void          GetPositionImpl(hkVector4& pos, bool applyCenterOffset) const;
        /// @brief Teleports the proxy (no velocity), clearing its old displacement and its manifold.
        virtual void          SetPositionImpl(const hkVector4& pos, bool applyCenterOffset, bool moveByVelocity);
        virtual void          GetTransformImpl(hkTransform& transform) const;
        virtual void          SetTransformImpl(const hkTransform& transform);
        virtual void          GetLinearVelocityImpl(hkVector4& velocity) const;
        virtual void          SetLinearVelocityImpl(const hkVector4& velocity);
        virtual CFilter       GetCollisionFilterInfo() const;
        virtual void          SetCollisionFilterInfo(CFilter filter);
        virtual bool          Unk_0A();
        /// @brief Drives the proxy at m_outVelocity and integrates it over the step (one cast iteration while flag
        ///        bit 20 is set, ten otherwise), under the world's write lock.
        virtual bool          Unk_0B();
        /// @brief Tells the move-finish sinks where the character is.
        virtual void          Unk_0C();
        /// @brief Clears kCanJump and refreshes the surface info (+0x1a0) from the proxy's support query, downward.
        virtual void          CheckSupportImpl();
        virtual void          SetWorld(bhkWorld* world);
        virtual bhkWorld*     GetHavokWorld() const;
        /// @brief The shape phantom's Havok phantom.
        /// @note The slot is typed for the rigid-body controller's body; here it hands back the hkpCachingShapePhantom.
        virtual hkpRigidBody* GetRigidBody() const;
        virtual float         GetVDBAlpha() const;
        /// @brief Appends the collidables the shape phantom overlaps.
        virtual bool          GetContactCollidables(BSScrapArray<hkpCollidable*>& collidables);
        /// @brief SetTransformImpl.
        virtual void          RotateImpl(const hkTransform& transform);

        /// @brief Builds the Havok side from `cinfo`: the shape phantom (entering the cinfo's world), the proxy over it,
        ///        listening to it.
        /// @note Name provisional.
        void Init(bhkCharProxyControllerCinfo& cinfo);

        /// @brief Copies s_controllers into `controllers` (referenced), under s_controllersLock.
        /// @note Name provisional.
        static void GetControllers(BSScrapArray<NiPointer<bhkCharProxyController> >& controllers);

        /// @brief (0, 0, -1, 0): the direction CheckSupportImpl queries the support in.
        /// @note Name provisional; set by a static initializer.
        static hkVector4 s_down;
        /// @brief Every live proxy controller.
        /// @note Name provisional. Guarded by s_controllersLock.
        static BSTSmallArray<bhkCharProxyController*, 20> s_controllers;
        /// @note Name provisional.
        static BSSpinLock s_controllersLock;

        bhkCharacterProxy                  m_characterProxy;   ///< +0x340
        /// @brief The shape phantom the proxy moves.
        NiPointer<bhkCachingShapePhantom>  m_shapePhantom;     ///< +0x5a0
        /// @brief Whether m_outVelocity was above sqrt(500) Havok units a second at the last Unk_0B.
        /// @note Name provisional.
        bool                               m_highSpeed;        ///< +0x5a8
        hkUint8                            m_pad5a9[0x5b0 - 0x5a9];
    };
} }
