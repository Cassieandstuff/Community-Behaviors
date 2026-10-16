/// @file
/// @brief hkbAttachmentInstance: one live attachment the attachment manager keeps.
#pragma once

#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace schema
{
    class hkbAttachmentSetup;

    /// @note 0x78 bytes, not reference counted (hkbAttachmentManager::detach frees it with its size). Only what the
    ///       reimplemented code reads is declared; names provisional.
    struct hkbAttachmentInstance
    {
        hkbAttachmentSetup* m_setup;            ///< +0x00 — referenced
        hkbHandle           m_attacherHandle;   ///< +0x08
        hkbHandle           m_attacheeHandle;   ///< +0x38
        hkUint8             m_pad68[0x70 - 0x68];
        /// @note An hkpConstraintInstance (the constraint attachment types).
        hkReferencedObject* m_constraint;       ///< +0x70
    };
} }
