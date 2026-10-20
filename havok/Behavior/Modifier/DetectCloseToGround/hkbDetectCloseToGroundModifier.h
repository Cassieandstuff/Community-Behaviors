/// @file
/// @brief hkbDetectCloseToGroundModifier: raycasts down from a bone and raises an event while it is near the ground.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    class hkbDetectCloseToGroundModifierInternalState : public hkReferencedObject
    {
    public:
        hkBool m_isCloseToGround;   ///< +0x10
    };

    /// @note Sized 0x78; member names per the Havok SDK's reflection (no schema YAML for this class yet). The `slot N`
    ///       annotations are vanilla's vtable slots. The destructor is implicit (it releases the event's payload).
    class hkbDetectCloseToGroundModifier : public hkbModifier
    {
    public:
        hkbEventProperty m_closeToGroundEvent;    ///< +0x50
        hkReal           m_closeToGroundHeight;   ///< +0x60
        hkReal           m_raycastDistanceDown;   ///< +0x64
        hkUint32         m_collisionFilterInfo;   ///< +0x68
        hkInt16          m_boneIndex;             ///< +0x6c, ragdoll bone
        hkInt16          m_animBoneIndex;         ///< +0x6e, animation bone
        hkBool           m_isCloseToGround;       ///< +0x70

        hkbDetectCloseToGroundModifier();
        /// @brief The cloneNode copy: the settings, not yet close to the ground.
        /// @warning The settings copy as one 0x20-byte block, the event's payload pointer included, without a reference
        ///          added (vanilla's behavior, reproduced).
        hkbDetectCloseToGroundModifier(const hkbDetectCloseToGroundModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        /// @brief Raises the event while close to the ground.
        virtual void                update(const hkbContext& context, hkReal timestep);               ///< slot 5
        /// @note Exactly one of the two bone indices must be set (or bound).
        virtual bool                isValid(hkStringPtr& errorString) const;                          ///< slot 10
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        /// @brief Casts the ray (m_raycastDistanceDown long, against the world's up) and updates m_isCloseToGround.
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
