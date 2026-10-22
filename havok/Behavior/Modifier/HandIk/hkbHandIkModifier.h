/// @file
/// @brief hkbHandIkModifier: reaches a character's arms toward the targets the hand IK control tracks carry.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace havok
{
    class hkaPose;
    extern const hkClass hkbHandIkModifierClass;

    /// @note 0x78 bytes; members per the exe's reflected class (hkbHandIkModifier: hands, fadeInOutCurve,
    ///       internalHandData). The `slot N` annotations are vanilla's vtable slots. The destructor is implicit: vanilla's
    ///       out-of-line copy (0x140ac6af0) stores no vtable of its own.
    class hkbHandIkModifier : public hkbModifier
    {
    public:
        /// @brief One arm: its bones and how its hand meets the target.
        /// @note 0x60 bytes (hkbHandIkModifierHand: hkbHandIkDriverInfo::Hand's layout); `m_handOrienationOffsetLS` is
        ///       Havok's spelling. The copies are memberwise and force-inlined (vanilla inlines the array copies).
        struct Hand
        {
            hkVector4    m_elbowAxisLS;              ///< +0x00
            hkVector4    m_backHandNormalLS;         ///< +0x10
            hkVector4    m_handOffsetLS;             ///< +0x20
            hkQuaternion m_handOrienationOffsetLS;   ///< +0x30
            hkReal       m_maxElbowAngleDegrees;     ///< +0x40
            hkReal       m_minElbowAngleDegrees;     ///< +0x44
            hkInt16      m_shoulderIndex;            ///< +0x48
            hkInt16      m_shoulderSiblingIndex;     ///< +0x4a
            hkInt16      m_elbowIndex;               ///< +0x4c
            hkInt16      m_elbowSiblingIndex;        ///< +0x4e
            hkInt16      m_wristIndex;               ///< +0x50
            hkBool       m_enforceEndPosition;       ///< +0x52
            hkBool       m_enforceEndRotation;       ///< +0x53
            hkStringPtr  m_localFrameName;           ///< +0x58

            HK_FORCE_INLINE Hand(const Hand& h)
            :   m_elbowAxisLS(h.m_elbowAxisLS),
                m_backHandNormalLS(h.m_backHandNormalLS),
                m_handOffsetLS(h.m_handOffsetLS),
                m_handOrienationOffsetLS(h.m_handOrienationOffsetLS),
                m_maxElbowAngleDegrees(h.m_maxElbowAngleDegrees),
                m_minElbowAngleDegrees(h.m_minElbowAngleDegrees),
                m_shoulderIndex(h.m_shoulderIndex),
                m_shoulderSiblingIndex(h.m_shoulderSiblingIndex),
                m_elbowIndex(h.m_elbowIndex),
                m_elbowSiblingIndex(h.m_elbowSiblingIndex),
                m_wristIndex(h.m_wristIndex),
                m_enforceEndPosition(h.m_enforceEndPosition),
                m_enforceEndRotation(h.m_enforceEndRotation),
                m_localFrameName(h.m_localFrameName)
            {
            }
            HK_FORCE_INLINE void operator=(const Hand& h)
            {
                m_elbowAxisLS            = h.m_elbowAxisLS;
                m_backHandNormalLS       = h.m_backHandNormalLS;
                m_handOffsetLS           = h.m_handOffsetLS;
                m_handOrienationOffsetLS = h.m_handOrienationOffsetLS;
                m_maxElbowAngleDegrees   = h.m_maxElbowAngleDegrees;
                m_minElbowAngleDegrees   = h.m_minElbowAngleDegrees;
                m_shoulderIndex          = h.m_shoulderIndex;
                m_shoulderSiblingIndex   = h.m_shoulderSiblingIndex;
                m_elbowIndex             = h.m_elbowIndex;
                m_elbowSiblingIndex      = h.m_elbowSiblingIndex;
                m_wristIndex             = h.m_wristIndex;
                m_enforceEndPosition     = h.m_enforceEndPosition;
                m_enforceEndRotation     = h.m_enforceEndRotation;
                m_localFrameName         = h.m_localFrameName;
            }
        };

        /// @brief One hand's run-time state: its target in model space, the handle it reaches through, and the fades.
        /// @note 0x80 bytes. Names provisional (read off activate, update and modify). The empty constructor keeps
        ///       setSize from zeroing the elements: vanilla constructs only the handle.
        struct InternalHandData
        {
            hkVector4    m_targetPosition;               ///< +0x00, model space
            hkQuaternion m_targetRotation;               ///< +0x10, model space
            hkVector4    m_targetNormal;                 ///< +0x20, model space
            /// @brief The hand's local frame (Hand::m_localFrameName) on the character, found at activation.
            hkbHandle    m_handle;                       ///< +0x30
            hkReal       m_transformOnFraction;          ///< +0x60
            hkReal       m_desiredTransformOnFraction;   ///< +0x64
            hkReal       m_normalOnFraction;             ///< +0x68
            hkReal       m_desiredNormalOnFraction;      ///< +0x6c
            hkReal       m_fadeInDuration;               ///< +0x70
            hkReal       m_fadeOutDuration;              ///< +0x74
            /// @brief The control track's on-fraction last seen: a smaller one turns the hand off.
            hkReal       m_trackOnFraction;              ///< +0x78

            HK_FORCE_INLINE InternalHandData() {}
        };

        hkArray<Hand>             m_hands;              ///< +0x50
        /// @note An hkbBlendCurveUtils::BlendCurve.
        hkInt8                    m_fadeInOutCurve;     ///< +0x60
        hkArray<InternalHandData> m_internalHandData;   ///< +0x68

        hkbHandIkModifier();
        /// @brief The cloneNode copy: the hands, with fresh run-time state.
        /// @note m_fadeInOutCurve is not copied (vanilla's, reproduced).
        hkbHandIkModifier(const hkbHandIkModifier& mod);

        virtual void     calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        /// @brief Sizes the run-time state to the hands and finds each hand's local frame.
        virtual void     activate(const hkbContext& context);                              ///< slot 4
        /// @brief Fades each hand's on-fractions toward their desired values.
        virtual void     update(const hkbContext& context, hkReal timestep);               ///< slot 5
        virtual bool     isValid(hkStringPtr& errorString) const;                          ///< slot 10
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                   ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);      ///< slot 23

        /// @brief Solves arm `handIndex` (shoulder, elbow, wrist) toward the model-space target, through the hand's
        ///        local frame when it has one.
        /// @note Name and parameter names provisional; out of line in vanilla (0x140af8330). modify passes the blended
        ///       on-fraction as all three gains.
        void solveArm(int handIndex, hkReal firstJointGain, hkReal secondJointGain, hkReal endJointGain, const hkVector4& targetPositionMS,
                      const hkQuaternion& targetRotationMS, hkaPose& pose);
        /// @brief Turns wrist `handIndex` so the back of the hand faces `targetNormalMS`, by `fraction` of the angle.
        /// @note Name provisional. `worldFromModel` is unused.
        void alignHandNormal(int handIndex, hkReal fraction, const hkVector4& targetNormalMS, const hkQsTransform& worldFromModel, hkaPose& pose);
    };
    static_assert(sizeof(hkbHandIkModifier::Hand) == 0x60, "hkbHandIkModifier::Hand size");
    static_assert(sizeof(hkbHandIkModifier::InternalHandData) == 0x80, "hkbHandIkModifier::InternalHandData size");
    static_assert(sizeof(hkbHandIkModifier) == 0x78, "hkbHandIkModifier size");
} }
