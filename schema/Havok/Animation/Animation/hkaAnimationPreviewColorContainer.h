/// @file
/// @brief hkaAnimationPreviewColorContainer: the colours a preview tool draws each animation in.
/// @note Header path provisional.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaAnimationPreviewColorContainer : hkReferencedObject
    {
        hkArray<hkUint32> m_previewColor;   ///< +0x10

        HK_FORCE_INLINE hkaAnimationPreviewColorContainer(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag), m_previewColor(flag) {}
        virtual ~hkaAnimationPreviewColorContainer();
    };
} }
