/// @file
/// @brief hkbTwistModifier: twists a bone chain about an axis, spreading the angle along it.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note The `slot N` annotations are vanilla's vtable slots (25 of them; the rest are hkbModifier's). The destructor is
    ///       implicit: vanilla's stamps no vtable, which a user-declared one (even empty) does. The deleting destructor
    ///       (slot 0) still calls it out of line.
    class hkbTwistModifier : public hkbModifier
    {
    public:
        enum SetAngleMethod { LINEAR = 0, RAMPED = 1 };
        enum RotationAxisCoordinates { ROTATION_AXIS_IN_MODEL_COORDINATES = 0, ROTATION_AXIS_IN_LOCAL_COORDINATES = 1 };

        hkVector4        m_axisOfRotation;            ///< +0x50
        hkReal           m_twistAngle;                ///< +0x60, degrees, spread along the chain
        hkInt16          m_startBoneIndex;            ///< +0x64
        hkInt16          m_endBoneIndex;              ///< +0x66
        hkInt8           m_setAngleMethod;            ///< +0x68, SetAngleMethod
        hkInt8           m_rotationAxisCoordinates;   ///< +0x69, RotationAxisCoordinates
        hkBool           m_isAdditive;                ///< +0x6a
        hkArray<hkInt16> m_boneChainIndices;          ///< +0x70, start to end, built by activate
        hkArray<hkInt16> m_parentBoneIndices;         ///< +0x80, the start bone's ancestors, nearest first

        hkbTwistModifier();
        /// @brief The cloneNode copy: the settings, with empty bone lists.
        hkbTwistModifier(const hkbTwistModifier& mod);

        virtual void     activate(const hkbContext& context);                              ///< slot 4
        virtual void     deactivate(const hkbContext& context);                            ///< slot 7
        virtual bool     isValid(hkStringPtr& errorString) const;                          ///< slot 10
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);     ///< slot 23
    };
} }
