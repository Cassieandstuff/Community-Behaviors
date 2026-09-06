#pragma once
#include "havok/classes/IHavokObject.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

// The shared base inheritance chain for behavior objects, hand-ported from
// HKX2E's Manual/ + Autogen/ sources. Fields + Signature constants are faithful
// to the `size:` / `Signatire:` header comments; Read/Write arrive at M2.
//
//   hkBaseObject(8) -> hkReferencedObject(16) -> hkbBindable(48)
//                   -> hkbNode(72) -> hkbGenerator(72)

namespace havok {

class hkbVariableBindingSet;  // pointer target (fwd; defined with the M1 leaf set)

// hkBaseObject — size 8, sig 0xe0708a00. Serialized form is a single void* slot
// (the runtime vtable), written as 0 / fixed up by the loader.
class hkBaseObject : public IHavokObject {
public:
    HK_CLASS_ID(0xe0708a00u, "hkBaseObject")
};

// hkReferencedObject — size 16, sig 0x3b1c1113.
class hkReferencedObject : public hkBaseObject {
public:
    std::uint16_t m_memSizeAndFlags = 0;  // SERIALIZE_IGNORED (occupies bytes)
    std::int16_t  m_referenceCount  = 0;  // SERIALIZE_IGNORED
    HK_CLASS_ID(0x3b1c1113u, "hkReferencedObject")
};

// hkbBindable — size 48, sig 0x2c1432d7.
class hkbBindable : public hkReferencedObject {
public:
    std::shared_ptr<hkbVariableBindingSet> m_variableBindingSet;
    bool m_areBindablesCached = false;    // SERIALIZE_IGNORED
    HK_CLASS_ID(0x2c1432d7u, "hkbBindable")
};

// hkbNode — size 72, sig 0x6d26f61d.
class hkbNode : public hkbBindable {
public:
    std::uint64_t       m_userData   = 0;
    std::string         m_name;
    std::int16_t        m_id         = 0;  // SERIALIZE_IGNORED
    std::int8_t         m_cloneState = 0;  // SERIALIZE_IGNORED (enum int8)
    std::array<bool, 1> m_padNode{};       // SERIALIZE_IGNORED (C-style array)
    HK_CLASS_ID(0x6d26f61du, "hkbNode")
};

// hkbGenerator — size 72, sig 0x0d68aefc. Adds no fields of its own.
class hkbGenerator : public hkbNode {
public:
    HK_CLASS_ID(0x0d68aefcu, "hkbGenerator")
};

} // namespace havok
