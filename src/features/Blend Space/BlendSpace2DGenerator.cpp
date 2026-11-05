#include "PCH.h"

#include "BlendSpace2DGenerator.h"

#include <locomotion/BlendSpace.h>
#include <platform/classes/ClassRegistry.h>

#include <Common/Base/Reflection/hkClass.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <new>

/// @note Registered whatever the flags: an unregistered class name loads with a null vtable.

namespace CB::feature::blendspace {

    namespace {

        namespace loco = CB::engine::locomotion;

        constexpr const char* kClassName = "CBBlendSpace2DGenerator";

        /// @brief The most samples a blend space weighs: the weights live on the stack, and more than this on one plane
        ///        would be a table, not a blend. isValid refuses more; past it a child keeps weight 0.
        constexpr int kMaxSamples = 64;

        void Finish(void* object)
        {
            if (object) new (object) BlendSpace2DGenerator(::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<BlendSpace2DGenerator*>(object)->~BlendSpace2DGenerator(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(BlendSpace2DGenerator) unsigned char scratch[sizeof(BlendSpace2DGenerator)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }

        [[maybe_unused]] const bool s_declared = platform::classes::Declare(
            { kClassName, sizeof(BlendSpace2DGenerator), &Finish, &Cleanup, &Vtable });

        /// @return where hkbBlenderGeneratorChild holds `member` (schema `cb: true`, reached by offset because the SDK's
        ///         class stays as exported); -1 until the class is extended
        int CachedOffset(std::atomic<int>& cached, const char* member)
        {
            int offset = cached.load(std::memory_order_relaxed);
            if (offset < 0) {
                offset = platform::classes::MemberOffset("hkbBlenderGeneratorChild", member);
                if (offset >= 0) cached.store(offset, std::memory_order_relaxed);
            }
            return offset;
        }
        int XOffset()
        {
            static std::atomic<int> cached{ -1 };
            return CachedOffset(cached, "x");
        }
        int YOffset()
        {
            static std::atomic<int> cached{ -1 };
            return CachedOffset(cached, "y");
        }

        ::hkReal& Coordinate(::hkbBlenderGeneratorChild* child, int offset)
        {
            return *reinterpret_cast<::hkReal*>(reinterpret_cast<char*>(child) + offset);
        }

    }  // namespace

    BlendSpace2DGenerator::BlendSpace2DGenerator(::hkFinishLoadedObjectFlag flag) :
        hkbBlenderGenerator(flag)
    {}

    BlendSpace2DGenerator::BlendSpace2DGenerator(const BlendSpace2DGenerator& other) :
        hkbBlenderGenerator(other),
        m_blendX(other.m_blendX),
        m_blendY(other.m_blendY)
    {
        const int x = XOffset();
        const int y = YOffset();
        if (x < 0 || y < 0) return;
        for (int i = 0; i < m_children.getSize(); ++i) {
            Coordinate(m_children[i], x) = Coordinate(other.m_children[i], x);
            Coordinate(m_children[i], y) = Coordinate(other.m_children[i], y);
        }
    }

    const ::hkClass* BlendSpace2DGenerator::getClassType() const
    {
        static const auto* const cls = static_cast<const ::hkClass*>(platform::classes::ClassOf(kClassName));
        return cls;
    }

    void BlendSpace2DGenerator::computeWeights()
    {
        const int x = XOffset();
        const int y = YOffset();
        if (x < 0 || y < 0) return;

        const int                                 count = std::min(m_children.getSize(), kMaxSamples);
        std::array<loco::BlendSample, kMaxSamples> samples;
        std::array<float, kMaxSamples>             weights;
        for (int i = 0; i < count; ++i) samples[i] = { Coordinate(m_children[i], x), Coordinate(m_children[i], y) };
        loco::GradientBandWeights(std::span(samples.data(), count), m_blendX, m_blendY, std::span(weights.data(), count));

        for (int i = 0; i < m_children.getSize(); ++i) m_children[i]->m_weight = i < count ? weights[i] : 0.0f;
    }

    void BlendSpace2DGenerator::activate(const ::hkbContext& context)
    {
        m_flags = static_cast<::hkInt16>(m_flags & ~FLAG_PARAMETRIC_BLEND);
        computeWeights();
        hkbBlenderGenerator::activate(context);
    }

    void BlendSpace2DGenerator::update(const ::hkbContext& context, ::hkReal timestep)
    {
        computeWeights();
        hkbBlenderGenerator::update(context, timestep);
    }

    bool BlendSpace2DGenerator::isValid(::hkStringPtr& errorString) const
    {
        if (m_children.getSize() > kMaxSamples) {
            errorString = "A 2D blend space weighs 64 children at most.";
            return false;
        }
        return hkbBlenderGenerator::isValid(errorString);
    }

    ::hkbNode* BlendSpace2DGenerator::cloneNode(::hkbBehaviorGraph& rootBehavior) const
    {
        return new BlendSpace2DGenerator(*this);
    }

}  // namespace CB::feature::blendspace
