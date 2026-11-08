#include "PCH.h"

#include "platform/anim/AnimationFiles.h"

#include <platform/game/AnimationLoad.h>
#include <platform/game/Character.h>
#include <platform/hooks/hookslib.h>

#include <Common/Base/hkBase.h>
#include <Animation/Animation/Animation/hkaAnimation.h>
#include <Animation/Animation/Animation/hkaAnimationBinding.h>
#include <Animation/Animation/Animation/hkaAnnotationTrack.h>
#include <Animation/Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Animation/Playback/Control/Default/hkaDefaultAnimationControl.h>
#include <Animation/Animation/Rig/hkaSkeleton.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
#include <Behavior/Behavior/Character/hkbCharacterSetup.h>
#include <Behavior/Behavior/Event/hkbEventPayload.h>
#include <Behavior/Behavior/Generator/Clip/hkbClipTriggerArray.h>
#include <Behavior/Behavior/Utils/hkbSymbolIdMap.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace CB::platform::anim {

    namespace {

        namespace game = CB::platform::game;

        template <class T>
        T& At(void* base, std::ptrdiff_t offset)
        {
            return *reinterpret_cast<T*>(static_cast<std::byte*>(base) + offset);
        }

        void* EntryDB() { return *reinterpret_cast<void**>(hooks::engine::Resolve(hooks::engine::AnimEntryDB_Singleton)); }

        /// @brief Sets the thread's task tag to the animation loader's (0x3a) for the scope, as every vanilla loader
        ///        slot does around these calls (Animation 64 Loader.cpp).
        class LoaderTaskScope {
        public:
            LoaderTaskScope() : tls_(RE::GetStaticTLSData()), saved_(tls_->taskFlag) { tls_->taskFlag = 0x3a; }
            ~LoaderTaskScope() { tls_->taskFlag = saved_; }
            LoaderTaskScope(const LoaderTaskScope&)            = delete;
            LoaderTaskScope& operator=(const LoaderTaskScope&) = delete;

        private:
            RE::TLSData*  tls_;
            std::uint32_t saved_;
        };

        constexpr std::string_view kMeshes = "meshes\\";

        bool Separator(char c) { return c == '\\' || c == '/'; }

        /// @return whether `text` starts with `prefix`, ASCII case folded.
        bool StartsFolded(std::string_view text, std::string_view prefix)
        {
            if (text.size() < prefix.size()) return false;
            for (std::size_t i = 0; i < prefix.size(); ++i) {
                const auto a = static_cast<unsigned char>(text[i]), b = static_cast<unsigned char>(prefix[i]);
                if (std::tolower(a) != std::tolower(b)) return false;
            }
            return true;
        }

        /// @brief The exe's `meshes\` placement (0x140d094f0, called with "meshes\" by 0x140ba84d0): the path itself
        ///        when it starts with `meshes\`; from the first `meshes` folder after a separator when one follows; else
        ///        `meshes\` + path.
        /// @note  A path whose first letter is the `m` of a mismatched word (e.g. `male\x.hkx`) is prefixed without the
        ///        search, as the exe does.
        std::string MeshesRelative(std::string_view path)
        {
            if (!path.empty() && std::tolower(static_cast<unsigned char>(path[0])) == 'm') {
                if (StartsFolded(path, kMeshes)) return std::string(path);
            } else {
                const std::string_view folder = kMeshes.substr(0, kMeshes.size() - 1);
                for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                    if (!Separator(path[i])) continue;
                    const std::string_view rest = path.substr(i + 1);
                    if (StartsFolded(rest, folder) && rest.size() > folder.size() && Separator(rest[folder.size()])) return std::string(rest);
                }
            }
            std::string out(kMeshes);
            out.append(path);
            return out;
        }

        /// @brief An hkRootLevelContainer named variant: {name, className, variant}, 0x18 bytes.
        struct NamedVariant {
            ::hkStringPtr name;
            ::hkStringPtr className;
            void*         variant;
        };
        static_assert(sizeof(NamedVariant) == 0x18);

        /// @return the first binding of the loaded entry's animation container, null without one.
        /// @note   As the binder takes it (0x140bad6e0, then AnimationBindingSet_Install 0x140bc34f0): the payload's
        ///         (entry+0x28) virtual +0x60 first (an empty `ret` in both BShkbHkxDB::DBData vtables at 1.6.1170, kept
        ///         for parity), then payload+0x20 is the file's hkRootLevelContainer, searched for the variant whose
        ///         class is "hkaAnimationContainer" (0x140b14ba0), whose m_bindings (+0x30, size +0x38) holds it.
        /// @note   The exe prefers the variant's runtime class name; packfiles always write className, which is read here.
        ::hkaAnimationBinding* BindingOf(void* entry)
        {
            void* payload = At<void*>(entry, 0x28);
            if (!payload) return nullptr;
            (*reinterpret_cast<void (***)(void*)>(payload))[0x60 / 8](payload);
            void* root = At<void*>(payload, 0x20);
            if (!root) return nullptr;
            const auto* variants = At<const NamedVariant*>(root, 0x0);
            const int   count    = At<int>(root, 0x8);
            for (int i = 0; i < count; ++i) {
                const char* cls = variants[i].className.cString();
                if (!cls || std::strcmp(cls, "hkaAnimationContainer") != 0 || !variants[i].variant) continue;
                void* container = variants[i].variant;
                if (At<int>(container, 0x38) <= 0) return nullptr;
                return At<::hkaAnimationBinding**>(container, 0x30)[0];
            }
            return nullptr;
        }

        /// @brief One annotation as the binding set stores it: its time and the project event it fires.
        struct Converted {
            float                     time;
            int                       externalId;
            ::hkbStringEventPayload*  payload;   ///< owned reference, or null
        };

        /// @return the project event id `name` names in `events`, -1 for none. `name` is interned (BSFixedString), as
        ///         the exe interns it: the map is keyed by the pool pointer.
        int EventIdOf(RE::BSTHashMap<char*, std::int32_t>& events, const char* name)
        {
            RE::BSFixedString key(name);
            if (key.empty()) return -1;
            const auto it = events.find(const_cast<char*>(key.data()));
            return it != events.end() ? it->second : -1;
        }

        /// @brief The binding set's annotation conversion (AnimationBindingSet_Install 0x140bc34f0): the whole text
        ///        as an event name, else the text up to its first '.' as the event and the rest as a string payload.
        bool Convert(RE::BSTHashMap<char*, std::int32_t>& events, const char* text, Converted& out)
        {
            if (!text) return false;
            if (const int id = EventIdOf(events, text); id != -1) {
                out.externalId = id;
                out.payload    = nullptr;
                return true;
            }
            const char* dot = std::strchr(text, '.');
            if (!dot) return false;
            const std::string head(text, dot);
            const int         id = EventIdOf(events, head.c_str());
            if (id == -1) return false;
            auto* payload   = new ::hkbStringEventPayload();
            payload->m_data = dot + 1;
            out.externalId  = id;
            out.payload     = payload;
            return true;
        }

        /// @brief The clip activate's trigger merge (hkbClipGenerator.cpp:71-111) over `animation`'s own annotations.
        void MergeAnnotations(::hkbClipGenerator& clip, const ::hkbContext& context, const ::hkaAnimation* animation)
        {
            RE::BShkbAnimationGraph* graph = game::GraphOf(context.m_character);
            if (!animation || !graph || !graph->projectDBData) return;
            auto& events = graph->projectDBData->unkA0;

            std::vector<Converted> converted;
            for (int t = 0; t < animation->m_annotationTracks.getSize(); ++t) {
                const ::hkaAnnotationTrack& track = animation->m_annotationTracks[t];
                for (int a = 0; a < track.m_annotations.getSize(); ++a) {
                    Converted c{ track.m_annotations[a].m_time, -1, nullptr };
                    if (Convert(events, track.m_annotations[a].m_text.cString(), c)) converted.push_back(c);
                }
            }
            if (converted.empty()) return;

            if (clip.m_triggers) clip.m_triggers.setAndDontIncrementRefCount(new ::hkbClipTriggerArray(*clip.m_triggers));
            else clip.m_triggers.setAndDontIncrementRefCount(new ::hkbClipTriggerArray());
            clip.m_triggers->m_triggers.reserve(clip.m_triggers->m_triggers.getSize() + static_cast<int>(converted.size()));

            const ::hkReal         duration = clip.computeUsableDuration();
            ::hkbSymbolIdMap* const map     = context.m_behavior ? static_cast<::hkbSymbolIdMap*>(context.m_behavior->m_eventIdMap) : nullptr;
            for (const Converted& c : converted) {
                const ::hkReal t = c.time - clip.m_cropStartAmountLocalTime;
                if (t >= 0.0f && t <= duration) {
                    const int internalId = map ? map->getInternal(c.externalId) : c.externalId;
                    if (internalId != -1) clip.addTrigger(t, ::hkbEvent(internalId, c.payload, nullptr), false, false, true);
                }
                if (c.payload) c.payload->removeReference();
            }
        }

    }  // namespace

    RE::BSResource::ID IdOf(std::string_view path)
    {
        RE::BSResource::ID id{};
        if (path.empty()) return id;
        const std::string relative = MeshesRelative(path);
        id.GenerateFromPath(relative.c_str());
        return id;
    }

    LoadedAnimation::~LoadedAnimation() { Reset(); }

    LoadedAnimation::LoadedAnimation(LoadedAnimation&& other) noexcept :
        entry_(std::exchange(other.entry_, nullptr)), binding_(std::exchange(other.binding_, nullptr))
    {}

    LoadedAnimation& LoadedAnimation::operator=(LoadedAnimation&& other) noexcept
    {
        if (this != &other) {
            Reset();
            entry_   = std::exchange(other.entry_, nullptr);
            binding_ = std::exchange(other.binding_, nullptr);
        }
        return *this;
    }

    void LoadedAnimation::Reset()
    {
        binding_ = nullptr;
        if (void* entry = std::exchange(entry_, nullptr)) {
            LoaderTaskScope task;
            game::AnimEntry_Release(entry);
        }
    }

    LoadedAnimation Acquire(const RE::BSResource::ID& id)
    {
        LoadedAnimation out;
        if (id.file == 0 && id.ext == 0 && id.dir == 0) return out;
        LoaderTaskScope task;
        void* entry = nullptr;
        if (!game::AnimEntryDB_FindLoaded(EntryDB(), &id, &entry)) {
            game::AnimEntryDB_Request(EntryDB(), &id);
            return out;
        }
        ::hkaAnimationBinding* binding = entry ? BindingOf(entry) : nullptr;
        if (!binding) {
            game::AnimEntry_Release(entry);
            return out;
        }
        out.entry_   = entry;
        out.binding_ = binding;
        return out;
    }

    bool Rebind(::hkbClipGenerator& clip, const ::hkbContext& context, ::hkaAnimationBinding* binding)
    {
        if (!binding || !clip.m_animationControl || clip.m_animationBindingIndex == -1) return false;

        clip.m_triggers = clip.m_originalTriggers;
        if (clip.m_mapperData) clip.m_mapperData->removeReference();
        clip.m_mapperData = nullptr;
        if (clip.m_mirroredAnimation) clip.m_mirroredAnimation->removeReference();
        clip.m_mirroredAnimation = nullptr;

        clip.m_animationControl->setAnimationBinding(binding);
        if (!(clip.m_flags & ::hkbClipGenerator::FLAG_DONT_CONVERT_ANNOTATIONS_TO_TRIGGERS)) MergeAnnotations(clip, context, binding->m_animation);
        clip.m_binding = binding;

        ::hkbCharacterSetup* setup = context.m_character->m_setup;
        if (binding->m_originalSkeletonName.cString() && setup && setup->m_animationSkeleton &&
            std::strcmp(setup->m_animationSkeleton->m_name.cString(), binding->m_originalSkeletonName.cString()) != 0) {
            if (const ::hkaSkeletonMapper* mapper = setup->getRetargetingSkeletonMapper(binding->m_originalSkeletonName.cString()))
                clip.m_mapperData = new ::hkaDefaultAnimationControlMapperData(mapper, binding);
        }
        clip.resetAtEnd();
        return true;
    }

}  // namespace CB::platform::anim
