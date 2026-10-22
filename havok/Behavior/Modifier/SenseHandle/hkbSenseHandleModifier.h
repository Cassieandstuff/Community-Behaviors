/// @file
/// @brief hkbSenseHandleModifier: finds the closest handle (rigid body, character bone or local frame) to a sensor and
///        raises the event of the distance range it falls in.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace havok
{
    struct hkbGeneratorOutput;
    class hkaRagdollInstance;
    class hkbClosestLocalFrameCollector;
    class hkpGroupFilter;
    class hkpRigidBody;

    /// @note Sized 0xe0. Member names are the reflected ones (the exe's class strings); the `slot N` annotations are
    ///       vanilla's vtable slots. The destructor is implicit: vanilla's (0x140ac6e70) is a COMDAT copy the deleting
    ///       destructor calls.
    class hkbSenseHandleModifier : public hkbModifier
    {
    public:
        /// @note 0x20 bytes.
        struct Range
        {
            hkbEventProperty m_event;          ///< +0x00
            hkReal           m_minDistance;    ///< +0x10
            hkReal           m_maxDistance;    ///< +0x14
            hkBool           m_ignoreHandle;   ///< +0x18

            /// @note Written out: the implicit one stays out of line under /Ob1.
            HK_FORCE_INLINE Range& operator=(const Range& r)
            {
                m_event        = r.m_event;
                m_minDistance  = r.m_minDistance;
                m_maxDistance  = r.m_maxDistance;
                m_ignoreHandle = r.m_ignoreHandle;
                return *this;
            }
        };

        /// @note Values per the Havok SDK, consistent with modify's dispatch (9 = local frame group, 6–8 = skeletons,
        ///       the rest = rigid bodies) and the rigid-body switch.
        /// @brief The rigid-body property key whose value is the owning hkbCharacter*.
        /// @note Name provisional; the value (0x1400) is what senseInRigidBodies looks up.
        enum { PROPERTY_CHARACTER = 0x1400 };

        enum SensingMode
        {
            SENSE_IN_NEARBY_RIGID_BODIES                 = 0,
            SENSE_IN_RIGID_BODIES_OUTSIDE_THIS_CHARACTER = 1,
            SENSE_IN_OTHER_CHARACTER_RIGID_BODIES        = 2,
            SENSE_IN_THIS_CHARACTER_RIGID_BODIES         = 3,
            SENSE_IN_GIVEN_CHARACTER_RIGID_BODIES        = 4,
            SENSE_IN_GIVEN_RIGID_BODY                    = 5,
            SENSE_IN_OTHER_CHARACTER_SKELETON            = 6,
            SENSE_IN_THIS_CHARACTER_SKELETON             = 7,
            SENSE_IN_GIVEN_CHARACTER_SKELETON            = 8,
            SENSE_IN_GIVEN_LOCAL_FRAME_GROUP             = 9,
        };

        hkbHandle           m_handle;                               ///< +0x50
        hkVector4           m_sensorLocalOffset;                    ///< +0x80
        hkArray<Range>      m_ranges;                               ///< +0x90
        hkRefPtr<hkbHandle> m_handleOut;                            ///< +0xa0
        hkRefPtr<hkbHandle> m_handleIn;                             ///< +0xa8
        hkStringPtr         m_localFrameName;                       ///< +0xb0
        hkStringPtr         m_sensorLocalFrameName;                 ///< +0xb8
        hkReal              m_minDistance;                          ///< +0xc0
        hkReal              m_maxDistance;                          ///< +0xc4
        hkReal              m_distanceOut;                          ///< +0xc8
        hkUint32            m_collisionFilterInfo;                  ///< +0xcc
        hkInt16             m_sensorRagdollBoneIndex;               ///< +0xd0
        hkInt16             m_sensorAnimationBoneIndex;             ///< +0xd2
        hkInt8              m_sensingMode;                          ///< +0xd4 — SensingMode
        hkBool              m_extrapolateSensorPosition;            ///< +0xd5
        hkBool              m_keepFirstSensedHandle;                ///< +0xd6
        hkBool              m_foundHandleOut;                       ///< +0xd7
        hkReal              m_timeSinceLastModify;                  ///< +0xd8
        int                 m_rangeIndexForEventToSendNextUpdate;   ///< +0xdc

        hkbSenseHandleModifier();
        /// @brief The cloneNode copy: the settings, with the sensed handle and outputs reset.
        hkbSenseHandleModifier(const hkbSenseHandleModifier& mod);

        virtual void     activate(const hkbContext& context);                            ///< slot 4
        virtual void     update(const hkbContext& context, hkReal timestep);             ///< slot 5
        virtual void     deactivate(const hkbContext& context);                          ///< slot 7
        virtual bool     isValid(hkStringPtr& errorString) const;                        ///< slot 10
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23

        /// @brief Where the sensor is in world space: the sensor offset from the ragdoll bone's rigid body (optionally
        ///        extrapolated by its velocity), from the animation bone in the pose, or from the character.
        /// @note Name provisional.
        void computeSensorPosWS(const hkbContext& context, hkbGeneratorOutput& inOut, hkVector4& sensorPosWS) const;
        /// @brief Modes 0–5: the closest rigid body (or local frame on one) within the distance window.
        /// @note Name provisional.
        void senseInRigidBodies(const hkVector4& sensorPosWS, hkReal minDistance, hkReal maxDistance, const hkbContext& context);
        /// @brief Modes 6–8: the closest character (or local frame on its skeleton).
        /// @note Name provisional.
        void senseInSkeletons(const hkVector4& sensorPosWS, hkReal minDistance, hkReal maxDistance, const hkbContext& context);
        /// @brief Mode 9: the closest local frame in the group of m_handleIn's frame.
        /// @note Name provisional.
        void senseInLocalFrameGroup(const hkVector4& sensorPosWS, hkReal minDistance, hkReal maxDistance, const hkbContext& context);
        /// @brief The widest distance window over the modifier's own and every range's.
        /// @note Name provisional.
        void computeMinMaxDistances(hkReal& minDistance, hkReal& maxDistance) const;
        /// @brief Publishes the sensed handle to m_handleOut and picks the range (the event update raises) the
        ///        distance falls in; a range that ignores the handle clears the output again.
        /// @note Name provisional.
        void processSensedHandle();
        /// @brief Appends the ragdoll's bodies that `filter` lets collide with `collisionFilterInfo`.
        /// @note Name provisional.
        /// @brief Offers every local frame on `character`'s skeleton to `collector`, each tested in its bone's space.
        /// @note Name provisional.
        void collectCharacterLocalFrames(const hkVector4& sensorPosWS, hkReal maxDistance, hkbCharacter* character,
                                         hkbClosestLocalFrameCollector& collector);
        static void getRigidBodiesThatCollide(const hkpGroupFilter* filter, const hkaRagdollInstance* ragdoll, hkUint32 collisionFilterInfo,
                                              hkArray<hkpRigidBody*>& rigidBodiesOut);
    };
} }
