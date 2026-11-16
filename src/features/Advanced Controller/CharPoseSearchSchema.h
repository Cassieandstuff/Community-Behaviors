#pragma once

#include <hky/layout/CBPoseSearchChannel.h>
#include <hky/layout/CBPoseSearchChannelSample.h>
#include <hky/layout/CBPoseSearchProfile.h>
#include <hky/layout/CBPoseSearchWeight.h>
#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Node/hkbBindable.h>
#include <Behavior/Behavior/Variables/hkbVariableBindingSet.h>
#include <Common/Base/Container/String/hkString.h>

#include <hky/compile/MotionFeatures.h>

#include <cstdint>
#include <string>
#include <string_view>

struct hkbContext;

/// @file
/// @brief A CBPoseSearchGenerator's channels and profiles as loaded objects (bindables, as a blender's children are), and
///        reading what a bindable's bindings name for the character running it. The layouts are
///        schema/generators/CBPoseSearch*.yaml's; the platform registers the classes from those descriptors.
/// @note  One channel or profile object serves every character's clone of the node, so a bound value is never read from
///        its members: the node reads each bound variable or character property by name for its own character.

namespace CB::feature::controller::motion {

    namespace anim = hky::anim;

    /// @brief CBPoseSearchChannelSample. POD in the loaded file's array.
    struct alignas(16) PoseSearchChannelSample {
        HK_DECLARE_POD_TYPE();

        ::hkReal    m_time;    ///< +0x00
        ::hkUint8   m_pad04[0x10 - 0x04];
        ::hkVector4 m_pose;    ///< +0x10 (x, y, facing angle, -)
        ::hkVector4 m_motion;  ///< +0x20 (velocity x, velocity y, height, -)
    };
    HKY_LAYOUT_CHECK(PoseSearchChannelSample, hky::layout::CBPoseSearchChannelSample);

    /// @brief CBPoseSearchChannel: one axis group of the search space.
    class PoseSearchChannel : public ::hkbBindable {
    public:
        ::hkStringPtr                       m_name;     ///< +0x30
        ::hkReferencedObject*               m_bones;    ///< +0x38 hkbBoneIndexArray, the loaded file's
        ::hkReferencedObject*               m_origin;   ///< +0x40 hkbBoneIndexArray, the loaded file's
        ::hkStringPtr                       m_curve;    ///< +0x48
        ::hkArray<PoseSearchChannelSample>  m_samples;  ///< +0x50
        ::hkInt8                            m_kind;     ///< +0x60 PoseSearchChannelKind
        ::hkInt8                            m_axis;     ///< +0x61
        ::hkUint8                           m_pad62[0x70 - 0x62];
        ::hkVector4                         m_query;    ///< +0x70

        explicit PoseSearchChannel(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
    };
    HKY_LAYOUT_CHECK(PoseSearchChannel, hky::layout::CBPoseSearchChannel);

    /// @brief CBPoseSearchWeight. POD in the loaded file's array.
    struct PoseSearchWeight {
        HK_DECLARE_POD_TYPE();

        ::hkStringPtr m_channel;   ///< +0x00
        ::hkInt32     m_element;   ///< +0x08
        ::hkInt8      m_part;      ///< +0x0c
        ::hkUint8     m_pad0d[0x10 - 0x0d];
        ::hkReal      m_weight;    ///< +0x10
    };
    HKY_LAYOUT_CHECK(PoseSearchWeight, hky::layout::CBPoseSearchWeight);

    /// @brief CBPoseSearchProfile: a named weight set over the channels.
    class PoseSearchProfile : public ::hkbBindable {
    public:
        ::hkStringPtr               m_name;      ///< +0x30
        ::hkArray<PoseSearchWeight> m_weights;   ///< +0x38

        explicit PoseSearchProfile(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
    };
    HKY_LAYOUT_CHECK(PoseSearchProfile, hky::layout::CBPoseSearchProfile);

    /// @brief `s` as text, "" for a null string.
    inline std::string Text(const ::hkStringPtr& s) { return s.cString() ? std::string(s.cString()) : std::string(); }

    /// @brief The variable (byte-exact) member `member` of `bindable` is bound to in `context`'s behavior; empty when none.
    std::string BoundVariable(const ::hkbContext& context, const ::hkbBindable& bindable, std::string_view member);
    /// @brief The character property member `member` of `bindable` is bound to, as `context`'s character states its
    ///        value; null when unbound or the character has no such property.
    const ::hkReferencedObject* BoundCharacterProperty(const ::hkbContext& context, const ::hkbBindable& bindable, std::string_view member);
    /// @brief Calls `fn(memberPath, variable)` for every variable binding of `bindable` in `context`'s behavior.
    template <class Fn>
    void ForEachVariableBinding(const ::hkbContext& context, const ::hkbBindable& bindable, Fn&& fn);

    /// @brief The variable name of binding `b` in `context`'s behavior; empty for a character property or a bad index.
    std::string VariableOf(const ::hkbContext& context, const ::hkbVariableBindingSet::Binding& b);

    template <class Fn>
    void ForEachVariableBinding(const ::hkbContext& context, const ::hkbBindable& bindable, Fn&& fn)
    {
        const ::hkbVariableBindingSet* set = bindable.m_variableBindingSet.val();
        if (!set) return;
        for (int i = 0; i < set->m_bindings.getSize(); ++i) {
            const auto&       b    = set->m_bindings[i];
            const char*       path = b.m_memberPath.cString();
            const std::string var  = VariableOf(context, b);
            if (path && !var.empty()) fn(std::string_view(path), var);
        }
    }

}  // namespace CB::feature::controller::motion
