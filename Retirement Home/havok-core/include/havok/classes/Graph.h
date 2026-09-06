#pragma once
#include "havok/classes/Base.h"
#include "havok/classes/Variables.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Behavior-graph root + container, hand-ported from HKX2E Autogen / Manual.

namespace havok {

// hkbBehaviorGraphStringData — size 80, sig 0xc713064e. The graph's name tables.
class hkbBehaviorGraphStringData : public hkReferencedObject {
public:
    std::vector<std::string> m_eventNames;
    std::vector<std::string> m_attributeNames;
    std::vector<std::string> m_variableNames;
    std::vector<std::string> m_characterPropertyNames;
    HK_CLASS_ID(0xc713064eu, "hkbBehaviorGraphStringData")
};

// hkbBehaviorGraphData — size 128, sig 0x095aca5d. Variable/event metadata + defaults.
class hkbBehaviorGraphData : public hkReferencedObject {
public:
    std::vector<float>            m_attributeDefaults;
    std::vector<hkbVariableInfo>  m_variableInfos;
    std::vector<hkbVariableInfo>  m_characterPropertyInfos;
    std::vector<hkbEventInfo>     m_eventInfos;
    std::vector<hkbVariableValue> m_wordMinVariableValues;
    std::vector<hkbVariableValue> m_wordMaxVariableValues;
    std::shared_ptr<hkbVariableValueSet>        m_variableInitialValues;
    std::shared_ptr<hkbBehaviorGraphStringData> m_stringData;
    HK_CLASS_ID(0x095aca5du, "hkbBehaviorGraphData")
};

// hkbBehaviorGraph — size 304, sig 0xb1218f86. The graph root (rootGenerator + data).
class hkbBehaviorGraph : public hkbGenerator {
public:
    std::int8_t                           m_variableMode = 0;  // enum VariableMode
    std::shared_ptr<hkbGenerator>         m_rootGenerator;
    std::shared_ptr<hkbBehaviorGraphData> m_data;
    // ── SERIALIZE_IGNORED tail (typed) ──
    std::int32_t m_numIntermediateOutputs   = 0;
    std::int16_t m_numStaticNodes           = 0;
    std::int16_t m_nextUniqueId             = 0;
    bool         m_isActive                 = false;
    bool         m_isLinked                 = false;
    bool         m_updateActiveNodes        = false;
    bool         m_stateOrTransitionChanged = false;
    HK_CLASS_ID(0xb1218f86u, "hkbBehaviorGraph")
};

// hkRootLevelContainerNamedVariant — size 24, sig 0xb103a2cd.
class hkRootLevelContainerNamedVariant : public IHavokObject {
public:
    std::string                         m_name;
    std::string                         m_className;
    std::shared_ptr<hkReferencedObject> m_variant;
    HK_CLASS_ID(0xb103a2cdu, "hkRootLevelContainerNamedVariant")
};

// hkRootLevelContainer — size 16, sig 0x2772c11e. The packfile's top-level root.
class hkRootLevelContainer : public IHavokObject {
public:
    std::vector<hkRootLevelContainerNamedVariant> m_namedVariants;
    HK_CLASS_ID(0x2772c11eu, "hkRootLevelContainer")
};

} // namespace havok
