/// @file
/// @brief hkbFootIkModifier: plants the character's feet on the ground found by ray casts (one
///        hkaFootPlacementIkSolver per leg), and can tilt the world-from-model with the ground.
#pragma once

#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Modifier/FootIk/hkbFootIkGains.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    class hkaFootPlacementIkSolver;
    class hkaPose;
    struct hkaSkeleton;

    /// @note Sized 0x100. The `slot N` annotations are vanilla's vtable slots. The destructor is out of line
    ///       (0x140af0970) and called by the scalar deleting destructor (slot 0).
    class hkbFootIkModifier : public hkbModifier
    {
    public:
        /// @brief How the world-from-model is aligned with the ground when hkbFootIkGains::m_alignWorldFromModelGain is
        ///        non-zero.
        enum AlignMode
        {
            ALIGN_MODE_FORWARD_RIGHT = 0,
            /// @brief Only the forward slope: the ground points are projected onto the plane across the up axis first.
            ALIGN_MODE_FORWARD       = 1
        };

        /// @brief One leg's bones and limits.
        /// @note 0xa0 bytes (Havok's hkbFootIkModifierLeg). The copy constructor and assignment are implicit and stay
        ///       out of line in vanilla (0x140af5200, 0x140af5320; hkArray's copy calls them).
        struct Leg
        {
            hkQsTransform    m_originalAnkleTransformMS;        ///< +0x00
            hkQuaternion     m_prevAnkleRotLS;                  ///< +0x30
            hkVector4        m_kneeAxisLS;                      ///< +0x40
            hkVector4        m_footEndLS;                       ///< +0x50
            /// @brief Raised while the leg is planted but its ray hit nothing.
            hkbEventProperty m_ungroundedEvent;                 ///< +0x60
            hkReal           m_footPlantedAnkleHeightMS;        ///< +0x70
            hkReal           m_footRaisedAnkleHeightMS;         ///< +0x74
            hkReal           m_maxAnkleHeightMS;                ///< +0x78
            hkReal           m_minAnkleHeightMS;                ///< +0x7c
            hkReal           m_maxKneeAngleDegrees;             ///< +0x80
            hkReal           m_minKneeAngleDegrees;             ///< +0x84
            hkReal           m_verticalError;                   ///< +0x88
            hkReal           m_maxAnkleAngleDegrees;            ///< +0x8c
            hkInt16          m_hipIndex;                        ///< +0x90
            hkInt16          m_kneeIndex;                       ///< +0x92
            hkInt16          m_ankleIndex;                      ///< +0x94
            hkBool           m_hitSomething;                    ///< +0x96
            hkBool           m_isPlantedMS;                     ///< +0x97
            hkBool           m_isOriginalAnkleTransformMSSet;   ///< +0x98
        };

        /// @brief One leg's solver and the ground point it found.
        /// @note 0x20 bytes (Havok's hkbFootIkModifierInternalLegData).
        struct InternalLegData
        {
            hkVector4                 m_groundPosition;   ///< +0x00
            /// @note Owned: deleted by deactivate and the destructor.
            hkaFootPlacementIkSolver* m_footIkSolver;     ///< +0x10

            HK_FORCE_INLINE InternalLegData() : m_groundPosition(hkVector4::getZero()), m_footIkSolver(HK_NULL) {}
        };

        hkbFootIkGains           m_gains;                     ///< +0x50
        hkArray<Leg>             m_legs;                      ///< +0x80
        hkReal                   m_raycastDistanceUp;         ///< +0x90
        hkReal                   m_raycastDistanceDown;       ///< +0x94
        hkReal                   m_originalGroundHeightMS;    ///< +0x98
        hkReal                   m_errorOut;                  ///< +0x9c
        hkVector4                m_errorOutTranslation;       ///< +0xa0
        hkQuaternion             m_alignWithGroundRotation;   ///< +0xb0
        hkReal                   m_verticalOffset;            ///< +0xc0
        hkUint32                 m_collisionFilterInfo;       ///< +0xc4
        hkReal                   m_forwardAlignFraction;      ///< +0xc8
        hkReal                   m_sidewaysAlignFraction;     ///< +0xcc
        hkReal                   m_sidewaysSampleWidth;       ///< +0xd0
        hkBool                   m_useTrackData;              ///< +0xd4
        hkBool                   m_lockFeetWhenPlanted;       ///< +0xd5
        hkBool                   m_useCharacterUpVector;      ///< +0xd6
        hkInt8                   m_alignMode;                 ///< +0xd7, AlignMode
        hkArray<InternalLegData> m_internalLegData;           ///< +0xd8
        hkReal                   m_prevIsFootIkEnabled;       ///< +0xe8
        hkBool                   m_isSetUp;                   ///< +0xec
        hkBool                   m_isGroundPositionValid;     ///< +0xed
        hkReal                   m_timeStep;                  ///< +0xf0

        hkbFootIkModifier();
        /// @brief The cloneNode copy: the settings and legs, with the runtime state fresh.
        hkbFootIkModifier(const hkbFootIkModifier& mod);
        /// @brief Deletes every leg's solver.
        ~hkbFootIkModifier();

        virtual void     activate(const hkbContext& context);                              ///< slot 4
        /// @brief Records the timestep and raises each leg's ungrounded event while it is planted but its ray hit
        ///        nothing.
        virtual void     update(const hkbContext& context, hkReal timestep);               ///< slot 5
        /// @brief Deletes the solvers (they are rebuilt on the next modify).
        virtual void     deactivate(const hkbContext& context);                            ///< slot 7
        virtual bool     isValid(hkStringPtr& errorString) const;                          ///< slot 10
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);    ///< slot 23

        /// @brief Builds each leg's solver for `skeleton` (lengths scaled by the game's foot IK scale).
        /// @note Name provisional.
        void setup(const hkbContext& context, const hkaSkeleton* skeleton);
        /// @brief Turns each leg's hip toward the rotation taking the world-from-model's up onto `upWS`, by `gain`.
        /// @note Name provisional.
        void orientHips(const hkbContext& context, hkbGeneratorOutput& inOut, hkaPose& pose, const hkVector4& upWS, hkReal gain);
        /// @brief Turns the world-from-model's up toward the ground's normal, by `gain` (through m_alignWithGroundRotation).
        /// @note Name provisional.
        void alignWorldFromModelUpWithGroundNormal(const hkbContext& context, const hkVector4& upWS, hkbGeneratorOutput& inOut, hkReal gain);
        /// @brief The ground's normal under the feet (from the legs' ground points, the forward and sideways fractions).
        /// @note Name provisional.
        void computeGroundNormal(const hkbContext& context, const hkQsTransform& worldFromModel, const hkVector4& upWS, hkVector4& groundNormalOut);
        /// @brief Fits a plane through `numPoints` points; its normal on `upWS`'s side.
        /// @return False when the points do not span a plane.
        /// @note Name provisional.
        bool fitPlane(const hkVector4* points, int numPoints, const hkVector4& upWS, hkVector4& normalOut) const;
        /// @brief Fits a line through `numPoints` points seen in the plane of `axis` and `upWS` (least squares); its unit
        ///        direction, pointing along `axis`.
        /// @return False when the points do not determine a line.
        /// @note Name provisional.
        bool fitLine(const hkVector4* points, int numPoints, const hkVector4& axis, const hkVector4& upWS, hkVector4& directionOut) const;
        /// @brief The mean of `numPoints` points.
        /// @note Name provisional.
        void computeAverage(const hkVector4* points, int numPoints, hkVector4& averageOut) const;
    };
    static_assert(sizeof(hkbFootIkModifier) == 0x100, "hkbFootIkModifier size");
} }
