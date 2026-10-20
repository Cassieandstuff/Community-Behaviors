/// @file
/// @brief The animation classes' reflection unit: one getVtable per class a packfile can instantiate.
/// @note File name provisional. Vanilla keeps these together, alphabetically, after the classes' scalar deleting
///       destructors (COMDAT copies the linker kept from this unit).
#include <Animation/hkaAnimationContainer.h>
#include <Animation/Animation/hkaAnimationPreviewColorContainer.h>
#include <Animation/Animation/hkaFootstepAnalysisInfo.h>
#include <Animation/Animation/Interleaved/hkaInterleavedUncompressedAnimation.h>
#include <Animation/Animation/Quantized/hkaQuantizedAnimation.h>
#include <Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.h>
#include <Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Motion/Default/hkaDefaultAnimatedReferenceFrame.h>

/// @brief Havok's HK_REFLECTION_DEFINE_VIRTUAL getVtable: finish-constructs a CLASS in a scratch buffer and returns the
///        vtable pointer the constructor stored, for the vtable → class registry.
/// @note External, not file-static as in Havok, so the matcher finds them by name.
#define HKA_DEFINE_GET_VTABLE(CLASS)                                \
    const void* getVtable##CLASS()                                  \
    {                                                               \
        union                                                       \
        {                                                           \
            __declspec(align(16)) void* ptr;                        \
            char buf[sizeof(CLASS)];                                \
        } u;                                                        \
        hkFinishLoadedObjectFlag f;                                 \
        f.m_finishing = 0;                                          \
        new (u.buf) CLASS(f);                                       \
        return u.ptr;                                               \
    }

namespace CB { namespace havok
{
    HKA_DEFINE_GET_VTABLE(hkaAnimationBinding)
    HKA_DEFINE_GET_VTABLE(hkaAnimationContainer)
    HKA_DEFINE_GET_VTABLE(hkaAnimationPreviewColorContainer)
    HKA_DEFINE_GET_VTABLE(hkaBoneAttachment)
    HKA_DEFINE_GET_VTABLE(hkaDefaultAnimatedReferenceFrame)
    HKA_DEFINE_GET_VTABLE(hkaFootstepAnalysisInfo)
    HKA_DEFINE_GET_VTABLE(hkaFootstepAnalysisInfoContainer)
    HKA_DEFINE_GET_VTABLE(hkaInterleavedUncompressedAnimation)
    HKA_DEFINE_GET_VTABLE(hkaMeshBinding)
    HKA_DEFINE_GET_VTABLE(hkaQuantizedAnimation)
    HKA_DEFINE_GET_VTABLE(hkaSkeleton)
    HKA_DEFINE_GET_VTABLE(hkaSkeletonMapper)
    HKA_DEFINE_GET_VTABLE(hkaSplineCompressedAnimation)
} }
