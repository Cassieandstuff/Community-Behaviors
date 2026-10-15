/// @file
/// @brief hkLocalFrame: a named transform attached to a bone or a rigid body; collectors gather the frames near a point.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkLocalFrame;

    /// @note Opaque so far: only compared and passed along.
    class hkLocalFrameGroup : public hkReferencedObject {};

    /// @brief Receives the frames hkLocalFrame::getNearbyFrames finds.
    class hkLocalFrameCollector : public hkReferencedObject
    {
    public:
        virtual void addFrame(const hkLocalFrame* localFrame, hkReal distance) = 0;   ///< slot 3
    };

    /// @note Slot order per the Havok SDK; slots 3 (getLocalTransform), 6 (getNearbyFrames), 8 (getParentFrame) and 12
    ///       (getGroup) are confirmed by their callers.
    class hkLocalFrame : public hkReferencedObject
    {
    public:
        virtual void                     getLocalTransform(hkTransform& transform) const = 0;                                         ///< slot 3
        virtual void                     setLocalTransform(const hkTransform& transform) = 0;                                         ///< slot 4
        virtual void                     getLocalPosition(hkVector4& position) const;                                                 ///< slot 5
        virtual void                     getNearbyFrames(const hkVector4& target, hkReal maxDistance, hkLocalFrameCollector& collector) const = 0;   ///< slot 6
        virtual const char*              getName() const = 0;                                                                         ///< slot 7
        virtual const hkLocalFrame*      getParentFrame() const = 0;                                                                  ///< slot 8
        virtual void                     setParentFrame(const hkLocalFrame* parentFrame) = 0;                                         ///< slot 9
        virtual int                      getNumChildFrames() const = 0;                                                               ///< slot 10
        virtual hkLocalFrame*            getChildFrame(int i) const = 0;                                                              ///< slot 11
        virtual const hkLocalFrameGroup* getGroup() const = 0;                                                                        ///< slot 12

        /// @brief The frame's transform composed up its parent chain.
        /// @note Out of line in vanilla.
        void getTransformToRoot(hkTransform& transformOut) const;
    };
} }
