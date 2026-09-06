#pragma once
#include "havok/classes/IHavokObject.h"

// hkbReferencePoseGenerator — a generator that outputs the skeleton's reference pose.
// Hand-ported (the transpiler is gone): hkbGenerator + one SERIALIZE_IGNORED m_skeleton
// pointer (always null in behavior packfiles) that occupies the trailing 8-byte slot.
// size 80, sig 0x26a5675a. Used by boar/riekling and other creature graphs.

#include "havok/classes/Base.h"

namespace havok {

class hkbReferencePoseGenerator : public hkbGenerator {
public:
    HK_CLASS_ID(0x26a5675au, "hkbReferencePoseGenerator")
};

} // namespace havok
