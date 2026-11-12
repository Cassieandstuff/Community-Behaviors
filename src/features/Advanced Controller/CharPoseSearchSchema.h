#pragma once

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Node/hkbBindable.h>
#include <Common/Base/Container/String/hkString.h>

#include <hky/compile/MotionFeatures.h>

#include <string>
#include <vector>

struct hkbContext;

/// @file
/// @brief CBPoseSearchSchema and its feature classes as loaded objects: what a database matches on (locomotion/PoseSearch).
///        The layouts are schema/generators/CBPoseSearch*.yaml's; the platform registers the classes from those descriptors.
/// @note  A feature's bindings are never copied into it: one feature object serves every character's clone of the node, so
///        the node reads each bound variable by name for its own character (BoundInput) instead.

namespace CB::feature::controller::motion {

    namespace anim = hky::anim;

    /// @brief CBPoseSearchFeature, the abstract base: a bindable with a weight.
    class PoseSearchFeature : public ::hkbBindable {
    public:
        ::hkReal m_weight;   ///< +0x30
        explicit PoseSearchFeature(::hkFinishLoadedObjectFlag flag) : ::hkbBindable(flag) {}
    };
    static_assert(sizeof(PoseSearchFeature) == 56, "PoseSearchFeature size (schema: 56)");

    /// @brief CBPoseSearchTrajectorySample. POD in the loaded file's array.
    struct alignas(16) PoseSearchTrajectorySample {
        HK_DECLARE_POD_TYPE();

        ::hkReal    m_time;       ///< +0x00
        ::hkReal    m_position;   ///< +0x04
        ::hkReal    m_facing;     ///< +0x08
        ::hkReal    m_velocity;   ///< +0x0c
        ::hkReal    m_height;     ///< +0x10
        ::hkUint8   m_pad14[0x20 - 0x14];
        ::hkVector4 m_pose;       ///< +0x20 (x, y, facing angle, -)
        ::hkVector4 m_motion;     ///< +0x30 (velocity x, velocity y, height, -)
    };
    static_assert(sizeof(PoseSearchTrajectorySample) == 64, "PoseSearchTrajectorySample size (schema: 64)");

    class PoseSearchTrajectory : public PoseSearchFeature {
    public:
        ::hkArray<PoseSearchTrajectorySample> m_samples;   ///< +0x38
        explicit PoseSearchTrajectory(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
    };
    static_assert(sizeof(PoseSearchTrajectory) == 72, "PoseSearchTrajectory size (schema: 72)");

    /// @brief CBPoseSearchPosition and CBPoseSearchVelocity share a layout.
    class PoseSearchBoneFeature : public PoseSearchFeature {
    public:
        ::hkStringPtr m_bone;     ///< +0x38
        ::hkStringPtr m_origin;   ///< +0x40
        ::hkUint8     m_pad48[0x50 - 0x48];
        ::hkVector4   m_query;    ///< +0x50
        explicit PoseSearchBoneFeature(::hkFinishLoadedObjectFlag flag);
    };
    static_assert(sizeof(PoseSearchBoneFeature) == 96, "PoseSearchBoneFeature size (schema: 96)");
    class PoseSearchPosition : public PoseSearchBoneFeature {
    public:
        using PoseSearchBoneFeature::PoseSearchBoneFeature;
        const ::hkClass* getClassType() const override;
    };
    class PoseSearchVelocity : public PoseSearchBoneFeature {
    public:
        using PoseSearchBoneFeature::PoseSearchBoneFeature;
        const ::hkClass* getClassType() const override;
    };

    class PoseSearchHeading : public PoseSearchFeature {
    public:
        ::hkStringPtr m_bone;    ///< +0x38
        ::hkInt32     m_axis;    ///< +0x40
        ::hkUint8     m_pad44[0x50 - 0x44];
        ::hkVector4   m_query;   ///< +0x50
        explicit PoseSearchHeading(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
    };
    static_assert(sizeof(PoseSearchHeading) == 96, "PoseSearchHeading size (schema: 96)");

    class PoseSearchCurve : public PoseSearchFeature {
    public:
        ::hkStringPtr m_curve;   ///< +0x38
        ::hkReal      m_query;   ///< +0x40
        explicit PoseSearchCurve(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
    };
    static_assert(sizeof(PoseSearchCurve) == 72, "PoseSearchCurve size (schema: 72)");

    /// @brief CBPoseSearchSchema: shared by the databases pointing at it.
    class PoseSearchSchema : public ::hkReferencedObject {
    public:
        ::hkStringPtr                  m_name;       ///< +0x10
        ::hkStringPtr                  m_rootBone;   ///< +0x18
        ::hkArray<PoseSearchFeature*>  m_features;   ///< +0x20, the loaded file's (never referenced or freed here)
        explicit PoseSearchSchema(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
    };
    static_assert(sizeof(PoseSearchSchema) == 48, "PoseSearchSchema size (schema: 48)");

    /// @brief One query value a feature takes from a graph variable: where it lands in the schema's row and how it reads.
    struct BoundInput {
        enum class How : std::uint8_t {
            Float,      ///< one float
            Vector,     ///< x, y, z into as many dims as the feature has (up to 3)
            PoseXY,     ///< a trajectory sample's pose: x, y, then the facing angle as (-sin, cos)
            MotionXYZ,  ///< a trajectory sample's motion: velocity x, y, then height
        };
        int         dim = 0;     ///< the first dim it fills
        int         width = 1;   ///< Vector: how many dims
        How         how = How::Float;
        std::string variable;    ///< the bound variable's name, byte-exact
    };

    /// @brief `schema` as data, and each of its features' bound query values resolved to variable names through the
    ///        behavior graph `context` runs (its string data's variable names).
    anim::SchemaSpec SpecOfSchema(const PoseSearchSchema& schema, const ::hkbContext& context, std::vector<BoundInput>& bound);

}  // namespace CB::feature::controller::motion
