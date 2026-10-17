/// @file
/// @brief hkError: the warning/assert sink.
/// @note Havok's warning macros pass `__FILE__` relative to the product root (`Generator\Clip\hkbClipGenerator.cpp`)
///       and a literal line number; reimplemented call sites pass the same literals so the strings and immediates match.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    struct hkError
    {
        enum Message { MESSAGE_REPORT, MESSAGE_WARNING, MESSAGE_ASSERT, MESSAGE_ERROR };

        virtual ~hkError();
        virtual void v1();
        virtual void v2();
        virtual int  message(Message m, int id, const char* description, const char* file, int line);   ///< slot 3

        static hkError* s_instance;
        static HK_FORCE_INLINE hkError& getInstance() { return *s_instance; }
    };
} }
