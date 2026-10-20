/// @file
/// @brief BSTweenerModifier: blends the character's world transform from where it was at activation to a target.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note Member names are the class's reflected names; m_duration, m_startTransform and m_time are reflected too,
    ///       flagged serialize-ignored. The destructor is implicit: vanilla's deleting destructor (slot 0) inlines it.
    class BSTweenerModifier : public hkbModifier
    {
    public:
        hkBool        m_tweenPosition;      ///< +0x50
        hkBool        m_tweenRotation;      ///< +0x51
        hkBool        m_useTweenDuration;   ///< +0x52
        hkReal        m_tweenDuration;      ///< +0x54 — seconds
        hkVector4     m_targetPosition;     ///< +0x60
        hkQuaternion  m_targetRotation;     ///< +0x70
        /// @note Seconds: m_tweenDuration, else one cycle of the wrapped generator (updateSync); 0 = jump to the target.
        hkReal        m_duration;           ///< +0x80
        /// @brief The character's world transform at activation.
        hkQsTransform m_startTransform;     ///< +0x90
        hkReal        m_time;               ///< +0xc0 — seconds since activation

        BSTweenerModifier();
        /// @note Called by the class's packfile finish function (in the type-registration block).
        BSTweenerModifier(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: everything, runtime state included.
        BSTweenerModifier(const BSTweenerModifier& mod);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                         ///< slot 1
        virtual void        activate(const hkbContext& context);                          ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);           ///< slot 5
        virtual bool        isValid(hkStringPtr& errorString) const;                      ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12
        /// @brief Moves the output's world-from-model translation and/or rotation m_time / m_duration of the way to the
        ///        target.
        virtual void        modify(const hkbContext& context, hkbGeneratorOutput& inOut); ///< slot 23
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);     ///< slot 24

        /// @brief The normalized blend from `from` to `to` (on `from`'s hemisphere) by `t`, into `out`.
        /// @note Name provisional. Out of line in vanilla (0x140b9f8c0) and a member, though it reads nothing of `this`.
        void interpolateRotation(const hkQuaternion& from, const hkQuaternion& to, hkReal t, hkQuaternion& out);
    };
    static_assert(sizeof(BSTweenerModifier) == 0xd0, "BSTweenerModifier size");
} }
