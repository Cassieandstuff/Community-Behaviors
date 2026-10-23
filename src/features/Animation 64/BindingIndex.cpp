#include "PCH.h"

#include "features/Animation 64/BindingIndex.h"
#include "features/Animation 64/Watch.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/classes/TypeRegistries.h>

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <atomic>
#include <cstddef>

namespace CB::feature::widen {

    namespace {

        namespace hk = CB::havok;

        std::int64_t* Slot(void* clip, int offset) { return reinterpret_cast<std::int64_t*>(static_cast<std::byte*>(clip) + offset); }

        /// @brief How many mismatches the writer tripwire has seen.
        std::atomic<std::uint64_t> g_unconvertedWrites{ 0 };

        /// @brief Logs the first 16, then each power of two: the tripwire must stay readable with every writer unconverted.
        void ReportUnconvertedWrite(const hk::hkbClipGenerator* clip, std::int16_t mirror, std::int64_t wide)
        {
            const std::uint64_t n = g_unconvertedWrites.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n > 16 && (n & (n - 1)) != 0) return;
            const char* name = clip->m_animationName.cString();
            LOG_WARN("animation 64: clip '{}' at {:#x}: the 16-bit index reads {}, the 64-bit slot {} - code outside CB wrote the "
                     "old field (an unconverted writer); adopted ({} so far)",
                     name ? name : "", reinterpret_cast<std::uintptr_t>(clip), mirror, wide, n);
        }

        /// @brief A loaded clip's slot starts as its 16-bit index (the packfile holds -1 there; the slot is zero); a
        ///        clone takes its original's.
        /// @note  Vanilla's copy constructor builds the clone member by member, so the padding the slot lives in is
        ///        whatever the allocator left: it is copied here, never read before.
        void KeepSlot(void* object, const void* source)
        {
            const int offset = WideBindingOffset();
            if (offset < 0) return;
            if (source) *Slot(object, offset) = *Slot(const_cast<void*>(source), offset);
            else *Slot(object, offset) = static_cast<const hk::hkbClipGenerator*>(object)->m_animationBindingIndex;
        }

        /// @brief Every activation through CB's vtable: run the writer tripwire, and offer the clip to the reader watch.
        void OnActivate(void* object)
        {
            BindingIndex(static_cast<hk::hkbClipGenerator*>(object));
            OfferToWatch(object);
        }

        [[maybe_unused]] const bool s_hooked = (platform::classes::OnOwnedObject("hkbClipGenerator", &KeepSlot),
                                                platform::classes::OnOwnedActivate("hkbClipGenerator", &OnActivate), true);

    }  // namespace

    int WideBindingOffset()
    {
        static std::atomic<int> cached{ -1 };
        int offset = cached.load(std::memory_order_relaxed);
        if (offset < 0) {
            offset = platform::classes::MemberOffset("hkbClipGenerator", "animationBindingIndex64");
            if (offset >= 0) cached.store(offset, std::memory_order_relaxed);
        }
        return offset;
    }

    std::int64_t BindingIndex(hk::hkbClipGenerator* clip)
    {
        const std::int16_t mirror = clip->m_animationBindingIndex;
        const int          offset = WideBindingOffset();
        if (offset < 0) return mirror;
        std::int64_t& wide = *Slot(clip, offset);
        if (mirror != MirrorOf(wide)) {
            ReportUnconvertedWrite(clip, mirror, wide);
            wide = mirror;
        }
        return wide;
    }

    void SetBindingIndex(hk::hkbClipGenerator* clip, std::int64_t index)
    {
        if (const int offset = WideBindingOffset(); offset >= 0) *Slot(clip, offset) = index;
        clip->m_animationBindingIndex = MirrorOf(index);
    }

}  // namespace CB::feature::widen
