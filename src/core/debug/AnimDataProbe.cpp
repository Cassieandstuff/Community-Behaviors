#include "PCH.h"

#include "core/debug/AnimDataProbe.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

namespace CB::animprobe {

    namespace {

        // ── Vendored AnimationClipDataSingleton layout ────────────────────────────────
        // CommonLibSSE-NG does not ship these types; layout is the CommonLibSSE community RE
        // (matches OAR's Havok.h and the Ghidra offsets: animDatas bucket array at
        // singleton+0x38, ClipData 0x18 header, BoundAnimationData 0x28). We only model what
        // the probe reads/writes.
        struct ClipTriggerData {          // 0x10
            RE::BSFixedString name;       // 0x00
            float             time;       // 0x08
            std::uint32_t     pad0C;      // 0x0C
        };
        static_assert(sizeof(ClipTriggerData) == 0x10);

        struct ClipData {                     // 0x08 header + numTriggers * 0x10
            float             motionSpeed;    // 0x00
            std::uint16_t     boundDataIndex; // 0x04  -> AnimationData::boundDatas[boundDataIndex]
            std::uint16_t     numTriggers;    // 0x06
            ClipTriggerData   triggerData[1]; // 0x08  -> actually [numTriggers]
        };

        struct BoundAnimationData {   // 0x28 — translation/rotation interpolators + duration
            std::uint8_t  pad00[0x20];
            float         duration;   // 0x20
            std::uint32_t pad24;
        };
        static_assert(sizeof(BoundAnimationData) == 0x28);

        // GetClipInformation out-param: pointers INTO the singleton's live records.
        struct AnimationClipData {              // 0x10
            ClipData*          clipGeneratorData;
            BoundAnimationData* boundAnimationData;
        };

        // The singleton pointer global (holds AnimationClipDataSingleton*).
        void* GetClipSingleton()
        {
            // AE Address Library id (BR is AE-only). SE id would be 515414.
            REL::Relocation<void**> singleton{ REL::ID(401553) };
            return *singleton;
        }

        // bool GetClipInformation(this, projectName, clipName, &out)
        bool GetClipInformation(void* a_singleton, const RE::BSFixedString& a_project,
                                const RE::BSFixedString& a_clip, AnimationClipData& a_out)
        {
            using func_t = bool (*)(void*, const RE::BSFixedString&, const RE::BSFixedString&,
                                    AnimationClipData&);
            REL::Relocation<func_t> func{ REL::ID(32573) };  // AE id (SE would be 31799)
            return func(a_singleton, a_project, a_clip, a_out);
        }

        bool MarkerPresent(const char* a_name)
        {
            std::error_code ec;
            return fs::exists(fs::path("Data") / "community_behaviors" / a_name, ec);
        }

        std::atomic<bool> s_probed{ false };

    }  // namespace

    void ProbeAnimClipData()
    {
        const bool doRead  = MarkerPresent("probe_animdata.txt");
        const bool doWrite = MarkerPresent("inject_animdata.txt");
        if (!doRead && !doWrite) return;                 // opt-in only
        if (s_probed.exchange(true)) return;             // once per session

        void* singleton = GetClipSingleton();
        LOG_INFO("[animprobe] AnimationClipDataSingleton = {}", singleton);
        if (!singleton) {
            LOG_INFO("[animprobe] singleton not built yet — will retry on a later message.");
            s_probed.store(false);                       // allow a retry when it's ready
            return;
        }

        // BR-1 (slow-walk ice-skating) bisector. The whole static path — compile, merge, the
        // collated serve, and the ".br" alias — is verified faithful (see docs/community-behaviors/
        // bugs/README.md), so the only remaining question is what the ENGINE actually holds at
        // runtime for a redirected actor. Read the locomotion clips the skate hinges on under BOTH
        // the ".br" key a redirected actor queries AND the stock key, and compare to ground truth:
        //   MT_WalkForwardSlow motionSpeed 0.1 | MT_WalkForward 1.0 | MT_WalkForwardFast 1.2,
        //   all boundData.duration ~1.13333 (shared anim idx 1100).
        //   1HM_TurnRightFast = validation: 1.5, duration 1.16667, FootRight@0.4/FootLeft@0.777778.
        // VERDICT: if the ".br" query MISSES, or motionSpeed/duration is wrong on the slow clip,
        // the redirect's clip->motion binding IS the skate cause (fix here). If ".br" matches the
        // stock values exactly, #1 is faithful at runtime and the fault is SpeedSampled / the
        // movement-speed feeding the parametric blend — pivot to lead #2.
        static const char* const kClips[] = {
            "MT_WalkForwardSlow", "MT_WalkForward", "MT_WalkForwardFast", "1HM_TurnRightFast"
        };
        for (const char* projName : { "DefaultMale.br", "DefaultMale.txt", "DefaultMale" }) {
            const RE::BSFixedString project(projName);
            for (const char* clipName : kClips) {
                AnimationClipData      out{};
                const RE::BSFixedString clip(clipName);
                if (!GetClipInformation(singleton, project, clip, out) || !out.clipGeneratorData) {
                    LOG_INFO("[animprobe] '{}' / '{}' = MISS", projName, clipName);
                    continue;
                }
                ClipData*   cd  = out.clipGeneratorData;
                const float dur = out.boundAnimationData ? out.boundAnimationData->duration : -1.0f;
                LOG_INFO("[animprobe] '{}' / '{}' : motionSpeed={} boundDataIndex={} duration={} triggers={}",
                         projName, clipName, cd->motionSpeed, cd->boundDataIndex, dur, cd->numTriggers);
                for (std::uint16_t i = 0; i < cd->numTriggers && i < 16; ++i)
                    LOG_INFO("[animprobe]     trigger[{}] '{}' @ {}", i,
                             cd->triggerData[i].name.c_str(), cd->triggerData[i].time);

                // WRITE PROOF (opt-in): only on the validation turn clip under the stock key —
                // set a distinctive motionSpeed the engine consumes, read back to confirm the
                // singleton is writable (the in-engine BYPASS end-goal).
                if (doWrite && std::string_view(clipName) == "1HM_TurnRightFast" &&
                    std::string_view(projName) == "DefaultMale") {
                    const float was = cd->motionSpeed;
                    cd->motionSpeed = 3.0f;
                    AnimationClipData verify{};
                    const bool ok2 = GetClipInformation(singleton, project, clip, verify);
                    LOG_INFO("[animprobe]   WRITE motionSpeed {} -> 3.0 ; read-back={} (ok={})",
                             was, (ok2 && verify.clipGeneratorData) ? verify.clipGeneratorData->motionSpeed : -1.0f, ok2);
                }
            }
        }
    }

}  // namespace CB::animprobe
