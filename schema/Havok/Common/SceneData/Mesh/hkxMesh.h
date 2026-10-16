/// @file
/// @brief hkxMesh: a scene mesh (sections of vertex/index buffers).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note Opaque so far: only held by reference (hkaMeshBinding).
    class hkxMesh : public hkReferencedObject {};
} }
