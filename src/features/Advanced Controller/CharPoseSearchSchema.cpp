#include "PCH.h"

#include "CharPoseSearchSchema.h"

#include <platform/classes/ClassRegistry.h>

#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraphData.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraphStringData.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Variables/hkbVariableBindingSet.h>
#include <Common/Base/Reflection/hkClass.h>

#include <charconv>
#include <cstring>
#include <new>
#include <string_view>

namespace CB::feature::controller::motion {

    namespace {


        /// @brief Declare `T` (finish constructor, destructor, vtable) for the platform to register as `name`.
        template <class T>
        bool DeclareClass(const char* name)
        {
            struct Ops {
                static void Finish(void* object)
                {
                    if (object) new (object) T(::hkFinishLoadedObjectFlag{});
                }
                static void        Cleanup(void* object) { static_cast<T*>(object)->~T(); }
                static const void* Vtable()
                {
                    alignas(T) unsigned char scratch[sizeof(T)] = {};
                    Finish(scratch);
                    return *reinterpret_cast<const void* const*>(scratch);
                }
            };
            return platform::classes::Declare({ name, sizeof(T), &Ops::Finish, &Ops::Cleanup, &Ops::Vtable });
        }

        constexpr const char* kSchema     = "CBPoseSearchSchema";
        constexpr const char* kTrajectory = "CBPoseSearchTrajectory";
        constexpr const char* kPosition   = "CBPoseSearchPosition";
        constexpr const char* kVelocity   = "CBPoseSearchVelocity";
        constexpr const char* kHeading    = "CBPoseSearchHeading";
        constexpr const char* kCurve      = "CBPoseSearchCurve";

        [[maybe_unused]] const bool s_declared = DeclareClass<PoseSearchSchema>(kSchema) && DeclareClass<PoseSearchTrajectory>(kTrajectory) &&
                                                 DeclareClass<PoseSearchPosition>(kPosition) && DeclareClass<PoseSearchVelocity>(kVelocity) &&
                                                 DeclareClass<PoseSearchHeading>(kHeading) && DeclareClass<PoseSearchCurve>(kCurve);

        const ::hkClass* ClassNamed(const char* name) { return static_cast<const ::hkClass*>(platform::classes::ClassOf(name)); }

        std::string Text(const ::hkStringPtr& s) { return s.cString() ? std::string(s.cString()) : std::string(); }

        /// @brief "samples:<i>/<rest>" -> i and rest; false for any other path.
        bool SamplePath(std::string_view path, int& index, std::string_view& rest)
        {
            constexpr std::string_view prefix = "samples:";
            if (!path.starts_with(prefix)) return false;
            path.remove_prefix(prefix.size());
            const auto slash = path.find('/');
            if (slash == std::string_view::npos) return false;
            const auto r = std::from_chars(path.data(), path.data() + slash, index);
            if (r.ec != std::errc{}) return false;
            rest = path.substr(slash + 1);
            return true;
        }

    }  // namespace

    PoseSearchTrajectory::PoseSearchTrajectory(::hkFinishLoadedObjectFlag flag) : PoseSearchFeature(flag), m_samples(flag) {}
    PoseSearchBoneFeature::PoseSearchBoneFeature(::hkFinishLoadedObjectFlag flag) : PoseSearchFeature(flag), m_bone(flag), m_origin(flag) {}
    PoseSearchHeading::PoseSearchHeading(::hkFinishLoadedObjectFlag flag) : PoseSearchFeature(flag), m_bone(flag) {}
    PoseSearchCurve::PoseSearchCurve(::hkFinishLoadedObjectFlag flag) : PoseSearchFeature(flag), m_curve(flag) {}
    PoseSearchSchema::PoseSearchSchema(::hkFinishLoadedObjectFlag flag) : ::hkReferencedObject(flag), m_name(flag), m_rootBone(flag), m_features(flag) {}

    const ::hkClass* PoseSearchTrajectory::getClassType() const { static const auto* c = ClassNamed(kTrajectory); return c; }
    const ::hkClass* PoseSearchPosition::getClassType() const { static const auto* c = ClassNamed(kPosition); return c; }
    const ::hkClass* PoseSearchVelocity::getClassType() const { static const auto* c = ClassNamed(kVelocity); return c; }
    const ::hkClass* PoseSearchHeading::getClassType() const { static const auto* c = ClassNamed(kHeading); return c; }
    const ::hkClass* PoseSearchCurve::getClassType() const { static const auto* c = ClassNamed(kCurve); return c; }
    const ::hkClass* PoseSearchSchema::getClassType() const { static const auto* c = ClassNamed(kSchema); return c; }

    anim::SchemaSpec SpecOfSchema(const PoseSearchSchema& schema, const ::hkbContext& context, std::vector<BoundInput>& bound)
    {
        anim::SchemaSpec out;
        out.name     = Text(schema.m_name);
        out.rootBone = Text(schema.m_rootBone);
        const ::hkbBehaviorGraph* graph = context.m_behavior;
        const auto*               names = graph && graph->m_data ? graph->m_data->m_stringData.val() : nullptr;
        const auto variableName = [&](const ::hkbVariableBindingSet::Binding& b) -> std::string {
            if (b.m_bindingType != ::hkbVariableBindingSet::Binding::BINDING_TYPE_VARIABLE || !names) return {};
            if (b.m_variableIndex < 0 || b.m_variableIndex >= names->m_variableNames.getSize()) return {};
            return Text(names->m_variableNames[b.m_variableIndex]);
        };

        int dim = 0;
        for (int k = 0; k < schema.m_features.getSize(); ++k) {
            const PoseSearchFeature* f   = schema.m_features[k];
            const ::hkClass*         cls = f ? f->getClassType() : nullptr;
            if (!cls) continue;
            const std::string_view type = cls->getName();
            anim::FeatureSpec      spec;
            spec.weight = f->m_weight;
            if (type == kTrajectory) {
                const auto& t = static_cast<const PoseSearchTrajectory&>(*f);
                spec.kind     = anim::FeatureKind::Trajectory;
                for (int i = 0; i < t.m_samples.getSize(); ++i) {
                    const auto& s = t.m_samples[i];
                    spec.samples.push_back({ s.m_time, s.m_position, s.m_facing, s.m_velocity, s.m_height });
                }
            } else if (type == kPosition || type == kVelocity) {
                const auto& b = static_cast<const PoseSearchBoneFeature&>(*f);
                spec.kind     = type == kPosition ? anim::FeatureKind::Position : anim::FeatureKind::Velocity;
                spec.bone     = Text(b.m_bone);
                spec.origin   = Text(b.m_origin);
            } else if (type == kHeading) {
                const auto& h = static_cast<const PoseSearchHeading&>(*f);
                spec.kind     = anim::FeatureKind::Heading;
                spec.bone     = Text(h.m_bone);
                spec.axis     = h.m_axis;
            } else if (type == kCurve) {
                spec.kind  = anim::FeatureKind::Curve;
                spec.curve = Text(static_cast<const PoseSearchCurve&>(*f).m_curve);
            } else {
                continue;
            }

            /// @note The bound query values, by member path: "query" for a pose feature or a curve, "samples:<i>/pose" and
            ///       "samples:<i>/motion" for a trajectory sample. A path this reads nothing of stays the graph's.
            if (const ::hkbVariableBindingSet* set = f->m_variableBindingSet.val())
                for (int b = 0; b < set->m_bindings.getSize(); ++b) {
                    const auto&       binding  = set->m_bindings[b];
                    const std::string variable = variableName(binding);
                    const char*       path     = binding.m_memberPath.cString();
                    if (variable.empty() || !path) continue;
                    int              sample = 0;
                    std::string_view part;
                    if (spec.kind == anim::FeatureKind::Trajectory && SamplePath(path, sample, part) && sample >= 0 &&
                        sample < static_cast<int>(spec.samples.size())) {
                        const int base = dim + sample * anim::features::kPerSample;
                        if (part == "pose") bound.push_back({ base + anim::features::kSamplePos, 4, BoundInput::How::PoseXY, variable });
                        if (part == "motion") bound.push_back({ base + anim::features::kSampleVelocity, 3, BoundInput::How::MotionXYZ, variable });
                    } else if (spec.kind != anim::FeatureKind::Trajectory && std::strcmp(path, "query") == 0) {
                        const int width = anim::FeatureDims(spec);
                        bound.push_back({ dim, width, width == 1 ? BoundInput::How::Float : BoundInput::How::Vector, variable });
                    }
                }
            dim += anim::FeatureDims(spec);
            out.features.push_back(std::move(spec));
        }
        return out;
    }

}  // namespace CB::feature::controller::motion
