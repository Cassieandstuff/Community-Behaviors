#pragma once
#include "havok/model/defs/CommonDefs.h"

#include <optional>
#include <string>
#include <vector>

// Tier-B Def POCOs — behavior root + graph data.
// Faithful ports of HKBuild\src\Models\BehaviorDef.cs and BehaviorGraphDataDef.cs.

namespace havok::model {

// BehaviorDef.cs — the `behavior:` block.
struct BehaviorDef {
    std::string                name;
    std::string                variableMode = "VARIABLE_MODE_DISCARD_WHEN_INACTIVE";
    std::string                rootGenerator;
    std::optional<std::string> data = std::string("null");  // name of file in data/, or "null"
};

// BehaviorDef.cs — top-level behavior.yaml (BehaviorFile).
struct BehaviorFile {
    PackfileDef packfile;
    BehaviorDef behavior;
};

// BehaviorGraphDataDef.cs — VariableInfoDef.
struct VariableInfoDef {
    std::string                name;
    std::string                type      = "VARIABLE_TYPE_REAL";
    std::string                role      = "ROLE_DEFAULT";
    int                        roleFlags = 0;
    int                        value     = 0;   // raw word value (int representation of float/bool/int)
    std::optional<std::string> quadValue;       // quad initial value for VECTOR4/QUATERNION
};

// BehaviorGraphDataDef.cs — EventInfoDef.
struct EventInfoDef {
    std::string name;
    std::string flags = "0";
};

// BehaviorGraphDataDef.cs — CharacterPropertyDef.
struct CharacterPropertyDef {
    std::string name;
    std::string type  = "VARIABLE_TYPE_POINTER";
    std::string flags = "0";
};

// BehaviorGraphDataDef.cs — data/graphdata.yaml.
struct BehaviorGraphDataDef {
    std::vector<VariableInfoDef>      variables;
    std::vector<EventInfoDef>         events;
    int                               characterPropertyInfoCount = 0;
    std::vector<CharacterPropertyDef> characterPropertyNames;
    int                               attributeDefaultCount = 0;
    std::vector<std::string>          quadVariableValues;
    int                               variantVariableValueCount = 0;
    int                               wordMinVariableValueCount = 0;
    int                               wordMaxVariableValueCount = 0;
};

} // namespace havok::model
