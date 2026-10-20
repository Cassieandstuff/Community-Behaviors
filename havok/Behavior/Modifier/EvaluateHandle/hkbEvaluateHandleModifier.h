/// @file
/// @brief hkbEvaluateHandleModifier: outputs where a handle is (position and rotation), optionally moving at a constant
///        speed when the handle changes.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace havok
{
    /// @note Sized 0xf0. Member names are the reflected ones (the exe's class strings); the `slot N` annotations are
    ///       vanilla's vtable slots. The destructor is implicit: the deleting destructor (0x140ac8bb0) inlines it.
    class hkbEvaluateHandleModifier : public hkbModifier
    {
    public:
        enum HandleChangeMode
        {
            HANDLE_CHANGE_MODE_ABRUPT            = 0,
            HANDLE_CHANGE_MODE_CONSTANT_VELOCITY = 1,
        };

        hkRefPtr<hkbHandle> m_handle;                    ///< +0x50
        hkVector4           m_handlePositionOut;         ///< +0x60
        hkQuaternion        m_handleRotationOut;         ///< +0x70
        hkBool              m_isValidOut;                ///< +0x80
        hkReal              m_extrapolationTimeStep;     ///< +0x84
        hkReal              m_handleChangeSpeed;         ///< +0x88
        hkInt8              m_handleChangeMode;          ///< +0x8c — HandleChangeMode
        hkbHandle           m_oldHandle;                 ///< +0x90
        hkVector4           m_oldHandlePosition;         ///< +0xc0
        hkQuaternion        m_oldHandleRotation;         ///< +0xd0
        hkReal              m_timeSinceLastModify;       ///< +0xe0
        hkBool              m_smoothlyChangingHandles;   ///< +0xe4

        hkbEvaluateHandleModifier();
        /// @brief The cloneNode copy: the settings and outputs, without the handle or the change-tracking state.
        hkbEvaluateHandleModifier(const hkbEvaluateHandleModifier& mod);

        virtual void     activate(const hkbContext& context);                            ///< slot 4
        virtual void     update(const hkbContext& context, hkReal timestep);             ///< slot 5
        virtual void     deactivate(const hkbContext& context);                          ///< slot 7
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
