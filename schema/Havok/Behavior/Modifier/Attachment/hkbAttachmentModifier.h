/// @file
/// @brief hkbAttachmentModifier: attaches the attachee handle to the attacher handle while active.
#pragma once

#include <Behavior/Attachment/hkbAttachmentSetup.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    extern const hkClass hkbAttachmentModifierClass;
    struct hkbAttachmentInstance;
    class hkbHandle;
    class hkpRigidBody;

    /// @note Layout from the exe's reflected class (0xc8 bytes). The `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: it releases the setup and the event payloads, not the handles (raw pointers).
    class hkbAttachmentModifier : public hkbModifier
    {
    public:
        hkbEventProperty             m_sendToAttacherOnAttach;   ///< +0x50
        hkbEventProperty             m_sendToAttacheeOnAttach;   ///< +0x60
        hkbEventProperty             m_sendToAttacherOnDetach;   ///< +0x70
        hkbEventProperty             m_sendToAttacheeOnDetach;   ///< +0x80
        hkRefPtr<hkbAttachmentSetup> m_attachmentSetup;          ///< +0x90
        /// @note Raw pointers: deactivate releases the handles by hand; the destructor leaves them.
        hkbHandle*                   m_attacherHandle;           ///< +0x98
        hkbHandle*                   m_attacheeHandle;           ///< +0xa0
        /// @brief The collision layer the attachee body gets while attached; -1 keeps its own (and, with an attacher
        ///        body, takes on the attacher's system group).
        hkInt32                      m_attacheeLayer;            ///< +0xa8
        /// @brief The attachee's rigid body while attached (serialize-ignored).
        hkpRigidBody*                m_attacheeRB;               ///< +0xb0
        /// @brief The motion type the attachee body had before a keyframed attachment (an hkpMotion::MotionType).
        hkUint8                      m_oldMotionType;            ///< +0xb8
        /// @brief The attachee body's collision filter info before the attachment.
        hkInt32                      m_oldFilterInfo;            ///< +0xbc
        hkbAttachmentInstance*       m_attachment;               ///< +0xc0

        hkbAttachmentModifier();
        /// @brief The cloneNode copy: the settings, with a copy of the attachment setup and nothing attached.
        /// @warning Vanilla block-copies the event properties, the setup and the handles without taking references, then
        ///          releases the copied setup pointer when it installs its own copy.
        hkbAttachmentModifier(const hkbAttachmentModifier& mod);

        /// @brief The reflected members, then the live attachment and its constraint.
        virtual void        calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        /// @brief Starts with nothing attached.
        virtual void        activate(const hkbContext& context);                                     ///< slot 4
        /// @brief Attaches, until it succeeds.
        virtual void        update(const hkbContext& context, hkReal timestep);                      ///< slot 5
        /// @brief Detaches and releases the handles.
        virtual void        deactivate(const hkbContext& context);                                   ///< slot 7
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12

        void setAttachmentSetup(hkbAttachmentSetup* attachmentSetup);
        /// @brief Forms the attachment when both handles are set and fit the attachment type: makes a keyframed attachee
        ///        body keyframed, moves it into the attacher's collision group (or m_attacheeLayer), then attaches through
        ///        the context's attachment manager and sends the attach events.
        /// @note Name provisional.
        void attach(const hkbContext& context);
        /// @brief Undoes attach: restores the attachee body's motion type and collision filter info, sends the detach
        ///        events and releases the attachment.
        /// @note Name provisional.
        void detach(const hkbContext& context);
    };
    static_assert(sizeof(hkbAttachmentModifier) == 0xc8, "hkbAttachmentModifier size");
} }
