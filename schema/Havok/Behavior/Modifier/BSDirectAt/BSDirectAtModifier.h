/// @file
/// @brief BSDirectAtModifier: Bethesda's aim-at — turns a bone chain by a heading and a pitch toward a target.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @brief The game-side source of a BSDirectAtModifier's target (the exe's BSTSingletonSDM<BSDirectAtModifierInterface>).
    /// @note Only the slot update calls is declared; its name is provisional.
    class BSDirectAtModifierInterface
    {
    public:
        virtual ~BSDirectAtModifierInterface();   ///< slot 0
        /// @brief Fills the modifier's target for this frame: whether there is one, and where it is (world space).
        virtual void getTarget(const hkbContext& context, hkBool& hasTarget, hkVector4& targetLocation, hkUint32 userInfo) = 0;   ///< slot 1

        /// @note Null until the game installs it.
        static BSDirectAtModifierInterface* s_instance;
    };

    /// @note Member names are the class's reflected names (the exe's hkClass strings); m_pUnknown and
    ///       m_boneChainIndices are reflected unnamed, their names are provisional. The `slot N` annotations are vanilla's
    ///       vtable slots. The destructor is implicit: vanilla's (0x140b9a040) stamps no vtable.
    class BSDirectAtModifier : public hkbModifier
    {
    public:
        hkBool            m_directAtTarget;           ///< +0x50
        hkInt16           m_sourceBoneIndex;          ///< +0x52, -1 = the character's own position
        hkInt16           m_startBoneIndex;           ///< +0x54
        hkInt16           m_endBoneIndex;             ///< +0x56
        hkReal            m_limitHeadingDegrees;      ///< +0x58
        hkReal            m_limitPitchDegrees;        ///< +0x5c
        hkReal            m_offsetHeadingDegrees;     ///< +0x60
        hkReal            m_offsetPitchDegrees;       ///< +0x64
        hkReal            m_onGain;                   ///< +0x68
        hkReal            m_offGain;                  ///< +0x6c
        hkVector4         m_targetLocation;           ///< +0x70, world space
        hkUint32          m_userInfo;                 ///< +0x80, handed to BSDirectAtModifierInterface::getTarget
        hkBool            m_directAtCamera;           ///< +0x84
        hkReal            m_directAtCameraX;          ///< +0x88
        hkReal            m_directAtCameraY;          ///< +0x8c
        hkReal            m_directAtCameraZ;          ///< +0x90
        hkBool            m_active;                   ///< +0x94
        /// @brief The heading / pitch the chain is currently turned by (radians), eased toward the target each frame.
        hkReal            m_currentHeadingOffset;     ///< +0x98
        hkReal            m_currentPitchOffset;       ///< +0x9c
        hkReal            m_timeStep;                 ///< +0xa0, the last update's
        /// @note Nulled by the constructors and deactivate; nothing in the class or its Utils reads it.
        void*             m_pUnknown;                 ///< +0xa8
        /// @brief Whether BSDirectAtModifierInterface supplied a target this frame (else m_targetLocation is aimed at).
        hkBool            m_hasTarget;                ///< +0xb0
        hkVector4         m_directAtTargetLocation;   ///< +0xc0, the interface's target (world space)
        /// @brief The chain from m_startBoneIndex down to m_endBoneIndex, start first (rebuilt every update).
        hkArray<hkInt16>  m_boneChainIndices;         ///< +0xd0

        BSDirectAtModifier();
        /// @note Called by the class's packfile finish function (in the type-registration block).
        BSDirectAtModifier(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: everything but m_pUnknown and the bone chain.
        BSDirectAtModifier(const BSDirectAtModifier& mod);

        virtual void     activate(const hkbContext& context);                          ///< slot 4
        /// @brief Asks BSDirectAtModifierInterface for the target and rebuilds the bone chain.
        virtual void     update(const hkbContext& context, hkReal timestep);           ///< slot 5
        virtual void     deactivate(const hkbContext& context);                        ///< slot 7
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut); ///< slot 23
    };
    static_assert(sizeof(BSDirectAtModifier) == 0xe0, "BSDirectAtModifier size");
} }
