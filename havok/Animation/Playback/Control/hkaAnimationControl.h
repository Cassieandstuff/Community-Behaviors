/// @file
/// @brief hkaAnimationControl: plays one animation binding (local time, weights, listeners).
#pragma once

#include <Animation/Animation/hkaAnimationBinding.h>

namespace CB { namespace havok
{
    class hkaChunkCache;
    struct hkaAnimationControl;

    /// @brief Told when a control it listens to is deleted.
    struct hkaAnimationControlListener
    {
        virtual void controlDeletedCallback(hkaAnimationControl* control);   ///< slot 0
    };

    struct hkaAnimationControl : hkReferencedObject
    {
        hkReal                                m_localTime;               ///< +0x10
        hkReal                                m_weight;                  ///< +0x14
        hkArray<hkUint8>                      m_transformTrackWeights;   ///< +0x18, 0xff = full weight
        hkArray<hkUint8>                      m_floatTrackWeights;       ///< +0x28, 0xff = full weight
        /// @note Not reference-counted: the control neither adds nor releases a reference.
        const hkaAnimationBinding*            m_binding;                 ///< +0x38
        hkArray<hkaAnimationControlListener*> m_listeners;               ///< +0x40
        hkReal                                m_motionTrackWeight;       ///< +0x50

        /// @brief A control at local time 0, full weight, every track weight 0xff (none without a binding).
        hkaAnimationControl(const hkaAnimationBinding* binding);
        /// @brief Tells every listener the control is going away.
        virtual ~hkaAnimationControl();   ///< slot 0

        virtual void update(hkReal stepDelta) = 0;                                                    ///< slot 3
        /// @brief Where `timestep` from now would land: the local time and the loops crossed on the way.
        virtual void getFutureTime(hkReal timestep, hkReal& localTimeOut, int& loopsOut) const = 0;   ///< slot 4
        /// @brief Samples every track of the animation at the local time.
        virtual void sampleTracks(hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 5
        /// @brief Samples the first `maxNumTransformTracks` / `maxNumFloatTracks` tracks at the local time.
        virtual void samplePartialTracks(hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                         hkReal* floatTracksOut, hkaChunkCache* cache) const;   ///< slot 6
        /// @brief The extracted motion over the next `deltaTime` (uncropped).
        virtual void getExtractedMotionDeltaReferenceFrame(hkReal deltaTime, hkQsTransform& deltaMotionOut) const;   ///< slot 7
        /// @note Slots 8–13 are the animation's track counts and the binding's track → bone / float-slot tables, which a
        ///       retargeting control reports for the target skeleton instead. Names provisional.
        virtual int            getNumTransformTracks() const;                ///< slot 8
        virtual int            getNumFloatTracks() const;                    ///< slot 9
        virtual int            getNumTransformTrackToBoneIndices() const;    ///< slot 10
        virtual const hkInt16* getTransformTrackToBoneIndices() const;       ///< slot 11
        virtual int            getNumFloatTrackToFloatSlotIndices() const;   ///< slot 12
        virtual const hkInt16* getFloatTrackToFloatSlotIndices() const;      ///< slot 13

        HK_FORCE_INLINE const hkaAnimationBinding* getAnimationBinding() const { return m_binding; }
        /// @brief Binds `binding` (not reference-counted) and resets every track weight to 0xff.
        void setBinding(const hkaAnimationBinding* binding);
        void addListener(hkaAnimationControlListener* listener);
        /// @brief Whether the bound animation carries extracted motion.
        /// @pre A non-null binding has an animation.
        /// @note Name provisional.
        bool hasExtractedMotion() const;
        /// @brief The extracted motion from the local time to `deltaTime` later, the animation cropped by the two
        ///        amounts; the identity without extracted motion.
        void getExtractedMotionDeltaReferenceFrame(hkReal deltaTime, hkQsTransform& deltaMotionOut, hkReal cropStartAmount, hkReal cropEndAmount) const;
    };
} }
