#pragma once
#include "havok/classes/Base.h"
#include "havok/core/HkTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Variable / binding / role / value-set classes, hand-ported from HKX2E Autogen.

namespace havok {

// hkbRoleAttribute — size 4, sig 0x3eb2e082.
class hkbRoleAttribute : public IHavokObject {
public:
    std::int16_t m_role  = 0;  // enum Role
    std::int16_t m_flags = 0;  // enum RoleFlags
    HK_CLASS_ID(0x3eb2e082u, "hkbRoleAttribute")
};

// hkbVariableInfo — size 6, sig 0x9e746ba2.
class hkbVariableInfo : public IHavokObject {
public:
    hkbRoleAttribute m_role{};      // inline
    std::int8_t      m_type = 0;    // enum VariableType
    HK_CLASS_ID(0x9e746ba2u, "hkbVariableInfo")
};

// hkbVariableValue — size 4, sig 0x0b99bd6a.
class hkbVariableValue : public IHavokObject {
public:
    std::int32_t m_value = 0;
    HK_CLASS_ID(0x0b99bd6au, "hkbVariableValue")
};

// hkbEventInfo — size 4, sig 0x5874eed4.
class hkbEventInfo : public IHavokObject {
public:
    std::uint32_t m_flags = 0;  // enum Flags
    HK_CLASS_ID(0x5874eed4u, "hkbEventInfo")
};

// hkbVariableBindingSetBinding — size 40, sig 0x4d592f72.
class hkbVariableBindingSetBinding : public IHavokObject {
public:
    std::string  m_memberPath;
    std::int32_t m_offsetInObjectPlusOne = 0;  // SERIALIZE_IGNORED
    std::int32_t m_offsetInArrayPlusOne  = 0;  // SERIALIZE_IGNORED
    std::int32_t m_rootVariableIndex     = 0;  // SERIALIZE_IGNORED
    std::int32_t m_variableIndex         = 0;
    std::int8_t  m_bitIndex              = 0;
    std::int8_t  m_bindingType           = 0;  // enum BindingType
    std::uint8_t m_memberType            = 0;  // SERIALIZE_IGNORED
    std::int8_t  m_variableType          = 0;  // SERIALIZE_IGNORED
    std::int8_t  m_flags                 = 0;  // SERIALIZE_IGNORED
    HK_CLASS_ID(0x4d592f72u, "hkbVariableBindingSetBinding")
};

// hkbVariableBindingSet — size 40, sig 0x338ad4ff.
class hkbVariableBindingSet : public hkReferencedObject {
public:
    std::vector<hkbVariableBindingSetBinding> m_bindings;
    std::int32_t m_indexOfBindingToEnable = 0;
    bool         m_hasOutputBinding       = false;  // SERIALIZE_IGNORED
    HK_CLASS_ID(0x338ad4ffu, "hkbVariableBindingSet")
};

// hkbVariableValueSet — size 64, sig 0x27812d8d.
class hkbVariableValueSet : public hkReferencedObject {
public:
    std::vector<hkbVariableValue>                    m_wordVariableValues;
    std::vector<Vector4>                             m_quadVariableValues;
    std::vector<std::shared_ptr<hkReferencedObject>> m_variantVariableValues;
    HK_CLASS_ID(0x27812d8du, "hkbVariableValueSet")
};

} // namespace havok
