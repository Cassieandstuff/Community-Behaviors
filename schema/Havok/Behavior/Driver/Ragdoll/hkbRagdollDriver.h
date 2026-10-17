/// @file
/// @brief hkbRagdollDriver: drives a character's ragdoll toward its animated pose and feeds the simulated pose back.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkaRagdollInstance;
    class hkpRigidBody;
    struct hkbCharacter;
    struct hkbContext;
    struct hkbGeneratorOutput;

    /// @brief How the ragdoll's world-from-model is found (reflected; 8 bytes).
    struct hkbWorldFromModelModeData
    {
        /// @note Names and values per the Havok SDK: hkbPoweredRagdollControlsModifier::isValid checks the pose matching
        ///       bones under 2 (COMPUTE), its constructor defaults to 3 (NONE), and the driver's extractRagdollPoseInternal
        ///       treats 4 (RAGDOLL) apart.
        enum WorldFromModelMode
        {
            WORLD_FROM_MODEL_MODE_USE_OLD   = 0,
            WORLD_FROM_MODEL_MODE_USE_INPUT = 1,
            WORLD_FROM_MODEL_MODE_COMPUTE   = 2,
            WORLD_FROM_MODEL_MODE_NONE      = 3,
            WORLD_FROM_MODEL_MODE_RAGDOLL   = 4
        };

        hkInt16 m_poseMatchingBone0;   ///< +0x0
        hkInt16 m_poseMatchingBone1;   ///< +0x2
        hkInt16 m_poseMatchingBone2;   ///< +0x4
        hkInt8  m_mode;                ///< +0x6
    };

    /// @note 0xd0 bytes. Member names are provisional except m_attachedRigidBodyToIndexMap and m_ragdoll (read off the
    ///       constructor, the destructor, reset(), setRagdoll() and the scene modifier's calls).
    class hkbRagdollDriver : public hkReferencedObject
    {
    public:
        /// @note Copied into m_mode by the constructor and reset(); postPhysics compares the two as floats (a duration
        ///       and its elapsed part, presumably), so the names and the integer type are open.
        hkInt32                   m_defaultMode;                     ///< +0x10
        /// @note Starts as bones -1, mode 0.
        hkbWorldFromModelModeData m_worldFromModelModeData;          ///< +0x14
        /// @brief When set, setRagdoll takes the outgoing ragdoll out of its physics world.
        hkBool                    m_removeRagdollFromWorld;          ///< +0x1c
        hkBool                    m_flag1D;                          ///< +0x1d
        /// @note Reset to identity by reset().
        hkQsTransform             m_lastWorldFromModel;              ///< +0x20
        /// @note A copy of m_worldFromModelModeData at construction.
        hkbWorldFromModelModeData m_worldFromModelModeDataInUse;     ///< +0x50
        /// @note Sized to the ragdoll's bone count and zeroed by setRagdoll.
        hkArray<hkUint32>         m_boneWords;                       ///< +0x58
        hkInt32                   m_int68;                           ///< +0x68
        /// @brief Rigid bodies attached to the ragdoll (hkbAttachmentModifier adds the attachee's body).
        /// @note Name per the Havok SDK; the value stored is 1.
        hkPointerMap<hkpRigidBody*, int> m_attachedRigidBodyToIndexMap;   ///< +0x70
        hkbCharacter*             m_character;                       ///< +0x80
        hkaRagdollInstance*       m_ragdoll;                         ///< +0x88
        /// @brief An hkMemoryRouter::easyAlloc block freeRagdollPoseWS frees.
        void*                     m_ragdollPoseWS;                   ///< +0x90
        /// @brief A heap block of 0x70 bytes setRagdoll destroys with the old ragdoll.
        void*                     m_ragdollController;               ///< +0x98
        /// @brief An hkMemoryRouter::easyAlloc block reset() frees.
        void*                     m_scratch;                         ///< +0xa0
        /// @brief A heap block of m_numLastPoseLocal transforms reset() frees.
        hkQsTransform*            m_lastPoseLocal;                   ///< +0xa8
        hkInt32                   m_numLastPoseLocal;                ///< +0xb0
        hkReal                    m_timeRigidBodyControllerActive;   ///< +0xb4
        hkReal                    m_timePoweredControllerActive;     ///< +0xb8
        hkInt32                   m_counter;                         ///< +0xbc
        hkInt32                   m_mode;                            ///< +0xc0
        /// @note Raised by setRagdoll and reset.
        hkBool                    m_ragdollChanged;                  ///< +0xc4
        hkBool                    m_flagC5;                          ///< +0xc5
        /// @note Raised by the constructor; postPhysics does nothing while it is clear.
        hkBool                    m_enabled;                         ///< +0xc6
        hkBool                    m_flagsC7[4];                      ///< +0xc7
        hkBool                    m_flagCB;                          ///< +0xcb

        /// @note The Havok SDK's parameter order; the second's name is provisional.
        hkbRagdollDriver(hkbCharacter* character, bool removeRagdollFromWorld);
        /// @brief Lets the ragdoll go, then frees the pose buffers.
        ~hkbRagdollDriver();

        /// @note Out of line in vanilla (returns +0x88).
        hkaRagdollInstance* getRagdoll() const;
        /// @brief Returns the driver to its freshly-set-up state (hkbCharacter::deactivate).
        void reset();
        /// @brief Swaps in `ragdoll` (referenced), tearing down the old one's state.
        /// @note Out of line in vanilla (0x140aac7b0); reset() re-sets the current ragdoll through it.
        void setRagdoll(hkaRagdollInstance* ragdoll);
        /// @brief Frees the easyAlloc block at +0x90.
        /// @note Name provisional; out of line in vanilla (0x140aaff00).
        void freeRagdollPoseWS();
        /// @brief Drives the ragdoll toward the generated pose, before the physics step.
        /// @note Name per the Havok SDK; out of line in vanilla (0x140aacbe0), called by hkbRagdollSceneModifier.
        void driveToPose(hkReal timestep, const hkbContext& context, hkbGeneratorOutput& generatorOutput);
        /// @brief Feeds the simulated ragdoll back into the generated pose, after the physics step.
        /// @note Name per the Havok SDK; out of line in vanilla (0x140aae7b0), called by hkbRagdollSceneModifier.
        void postPhysics(const hkbContext& context, hkbGeneratorOutput& inOut);
        /// @brief Replaces the pose in `inOut` with the ragdoll's (hkbExtractRagdollPoseModifier).
        /// @note Name per the Havok SDK; out of line in vanilla (0x140aacb40).
        void extractRagdollPose(const hkbContext& context, hkbGeneratorOutput& inOut, bool computeWorldFromModel, int poseMatchingBone0,
                                int poseMatchingBone1, int poseMatchingBone2);
        /// @brief extractRagdollPose's body: maps the ragdoll pose into `inOut`, computing the world-from-model from the three
        ///        pose matching bones when asked (-1 takes m_worldFromModelModeDataInUse's).
        /// @note Name and parameter names provisional; out of line in vanilla (0x140ab0070), not reimplemented.
        ///       extractRagdollPose passes weights 0 and `blend` false; under `blend` the body compares the first weight with
        ///       m_timeRigidBodyControllerActive and the second with m_timePoweredControllerActive.
        void extractRagdollPoseInternal(const hkbContext& context, hkbGeneratorOutput& inOut, hkReal rigidBodyWeight, hkReal poweredWeight,
                                        bool blend, bool computeWorldFromModel, int poseMatchingBone0, int poseMatchingBone1,
                                        int poseMatchingBone2);
        /// @brief Registers `rigidBody` as attached, when it is one of the ragdoll's bodies.
        /// @note Name per the Havok SDK.
        void addAttachedRigidBody(hkpRigidBody* rigidBody);
        /// @note Name per the Havok SDK.
        void removeAttachedRigidBody(hkpRigidBody* rigidBody);
        /// @brief Copies `numTransforms` transforms and scales them by `scale` times one of two global unit scales
        ///        (`toWorldScale` picks which: presumably Skyrim's game-to-Havok unit conversion and its inverse).
        /// @note Name, owner and parameter names provisional; out of line in vanilla (0x140aaec40), called by the ragdoll
        ///       driver, hkbGetUpModifier and hkbPoseMatchingGenerator (which passes false and 1).
        static void copyAndScalePose(bool toWorldScale, int numTransforms, const hkQsTransform* poseIn, hkQsTransform* poseOut, hkReal scale);
    };
} }
