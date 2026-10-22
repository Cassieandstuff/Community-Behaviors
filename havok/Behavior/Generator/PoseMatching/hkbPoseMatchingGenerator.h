/// @file
/// @brief hkbPoseMatchingGenerator: a blender that blends in the child whose pose best matches the ragdoll's, then plays
///        it.
#pragma once

#include <Behavior/Generator/Blender/hkbBlenderGenerator.h>

namespace CB { namespace havok
{
    class hkaPoseMatchingUtility;

    struct hkbPoseMatchingGeneratorInternalState : hkReferencedObject
    {
        int    m_currentMatch;                 ///< +0x10
        int    m_bestMatch;                    ///< +0x14
        hkReal m_timeSinceBetterMatch;         ///< +0x18
        hkReal m_error;                        ///< +0x1c
        hkBool m_resetCurrentMatchLocalTime;   ///< +0x20
    };

    /// @note Layout from the schema (schema/gen/hkbPoseMatchingGenerator.yaml, 0xf0 bytes). The `slot N` annotations are
    ///       vanilla's vtable slots (the rest are hkbBlenderGenerator's).
    class hkbPoseMatchingGenerator : public hkbBlenderGenerator
    {
    public:
        enum Mode
        {
            MODE_MATCH = 0,
            MODE_PLAY  = 1,
        };

        hkQuaternion            m_worldFromModelRotation;       ///< +0xa0
        /// @brief How fast (per second) the best match's weight rises and the others' fall.
        hkReal                  m_blendSpeed;                   ///< +0xb0
        /// @brief The pelvis body must move at least this fast to switch to a better match.
        hkReal                  m_minSpeedToSwitch;             ///< +0xb4
        /// @brief How long a better match must stay best before switching, at zero match error ...
        hkReal                  m_minSwitchTimeNoError;         ///< +0xb8
        /// @brief ... and at full error (linear between).
        hkReal                  m_minSwitchTimeFullError;       ///< +0xbc
        hkInt32                 m_startPlayingEventId;          ///< +0xc0
        hkInt32                 m_startMatchingEventId;         ///< +0xc4
        hkInt16                 m_rootBoneIndex;                ///< +0xc8
        hkInt16                 m_otherBoneIndex;               ///< +0xca
        hkInt16                 m_anotherBoneIndex;             ///< +0xcc
        hkInt16                 m_pelvisIndex;                  ///< +0xce
        hkInt8                  m_mode;                         ///< +0xd0 — Mode
        /// @brief The child being blended in (-1 before the first match).
        int                     m_currentMatch;                 ///< +0xd4
        int                     m_bestMatch;                    ///< +0xd8
        hkReal                  m_timeSinceBetterMatch;         ///< +0xdc
        hkReal                  m_error;                        ///< +0xe0
        /// @brief Restart the current match's clock on the next updateSync (set by the start-matching event).
        hkBool                  m_resetCurrentMatchLocalTime;   ///< +0xe4
        /// @brief Each child's pose as a candidate, built by activate (serialize-ignored).
        hkaPoseMatchingUtility* m_poseMatchingUtility;          ///< +0xe8

        hkbPoseMatchingGenerator();
        /// @brief The cloneNode copy: the blender's copy and the settings, nothing matched yet.
        hkbPoseMatchingGenerator(const hkbPoseMatchingGenerator& gen);
        /// @brief Frees the pose matching utility.
        virtual ~hkbPoseMatchingGenerator();   ///< slot 0

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                                    ///< slot 1
        /// @brief Restarts the matching; on the first activation with a ragdoll and valid matching bones, builds the
        ///        candidate set from each child's first pose (generated in a scratch graph, mapped onto the ragdoll
        ///        skeleton). Every child starts weightless before the first match.
        virtual void        activate(const hkbContext& context);                                     ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);                      ///< slot 5
        /// @brief The start-playing event switches to MODE_PLAY; the start-matching event back to MODE_MATCH (restarting
        ///        the current match's clock).
        virtual void        handleEvent(const hkbContext& context, const hkbEvent e);                ///< slot 6
        virtual void        deactivate(const hkbContext& context);                                   ///< slot 7
        /// @brief The blender's checks, then all four bones set (or bound), the three matching bones distinct.
        virtual bool        isValid(hkStringPtr& errorString) const;                                 ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        virtual hkReferencedObject* createInternalState();                                           ///< slot 13
        /// @pre `internalState` is an hkbPoseMatchingGeneratorInternalState.
        virtual void        getInternalState(hkReferencedObject& internalState) const;               ///< slot 14
        /// @pre `internalState` is an hkbPoseMatchingGeneratorInternalState.
        virtual void        setInternalState(const hkReferencedObject& internalState);               ///< slot 16
        /// @note Only the start-playing event: vanilla leaves the start-matching event out.
        virtual void        getActiveEvents(hkPointerMap<int, bool>& activeEvents) const;            ///< slot 18
        /// @brief While matching, the generator and its active children report no timeline (the current match's clock
        ///        restarts when asked); while playing, the blender's.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25

        /// @brief While matching, finds the candidate closest to the ragdoll's pose and switches to it once it has stayed
        ///        best long enough (the switch time grows with the match error) and the pelvis body moves fast enough;
        ///        then moves the children's weights toward the current match by m_blendSpeed.
        /// @note Name provisional.
        void matchPose(const hkbContext& context, hkReal timestep);
    };
    static_assert(sizeof(hkbPoseMatchingGenerator) == 0xf0, "hkbPoseMatchingGenerator size");
} }
