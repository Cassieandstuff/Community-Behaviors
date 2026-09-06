#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

// Tier-B Def POCOs — shared/common definitions.
//
// Faithful field-for-field ports of HKBuild\src\Models\*Def.cs. These are the
// in-memory representation of the authored YAML behavior tree, BEFORE the
// BehaviorBuilder turns them into Tier-A havok-core objects.
//
// Fidelity rule (per the porting brief): where the C# Def stores a float as a
// *string* (e.g. PlaybackSpeed = "1.000000") we keep std::string. This preserves
// the exact text→float rounding HKBuild relied on; BehaviorBuilder parses it via
// std::stof. Numeric Def fields (int ids/indices) stay numeric.
//
// No external dependencies (no ryml) — these headers are pure C++.

namespace havok::model {

// hkbVariableBindingSetBinding source (BindingDef.cs, inside BlenderGeneratorDef.cs).
// A single variable→property binding inlined on any node that supports bindings.
struct BindingDef {
    std::string                 memberPath;
    int                         variableIndex = -1;   // raw index (fallback)
    std::optional<std::string>  variable;              // named (preferred); resolved to index
    int                         bitIndex      = -1;
    std::string                 bindingType   = "BINDING_TYPE_VARIABLE";
    // Marks the one binding that hkbVariableBindingSet::m_indexOfBindingToEnable
    // points at (the binding whose variable gates the object's m_enable). The
    // authored value is -1 far more often than it points at the "enable" binding,
    // so it must be preserved, not re-derived from memberPath.
    bool                        enableTarget  = false;
};

// BoneWeightsDef.cs (in PropertyDef.cs). Raw, named, or preset bone-weight source.
//
// `raw` (count + values) is the PRIMARY and only exercised path — every bone-weight
// source in all shipped/vanilla data is raw. `named` and `preset` are an OPTIONAL,
// currently-unused nice-to-have (there is no bone_presets.yaml anywhere). Those two
// paths COMPUTE the emitted array length; BehaviorBuilder/CharacterBuilder harden that
// computation (length validated, indices bounds-checked, non-empty skeleton required)
// so a mismatched boneCount / named mask / wrong skeleton fails loudly instead of
// emitting a wrong-length or OOB hkbBoneWeightArray. See buildBoneWeights in both.
struct BoneWeightsDef {
    int                                       count = 0;        // raw format: bone count
    std::string                               values;            // raw format: space-separated floats
    std::optional<std::map<std::string, std::string>> named;     // named format: bone name -> weight
    std::optional<std::string>                preset;            // preset name (expanded to `named` at load)
    std::optional<int>                        boneCount;         // explicit emitted-array length override
    std::string                               defaultWeight = "0.0";  // named format: fill for bones absent from
                                                                 // `named` (per-array; 0 = "list what to activate",
                                                                 // 1 = "list what to mask off"). Stored as a string
                                                                 // like the named weights; parsed to float at build.

    bool IsNamed()  const { return named.has_value(); }
    bool IsPreset() const { return preset.has_value() && !preset->empty(); }
    bool HasData()  const {
        return IsPreset() || IsNamed() || (count > 0 && !values.empty());
    }
};

// InlineEventDef.cs (in BSCyclicBlendTransitionGeneratorDef.cs). An hkbEvent /
// hkbEventProperty authored inline (id + optional payload). `event` is the named
// form, resolved to an id at build time.
struct InlineEventDef {
    int                        id = -1;
    std::optional<std::string> event;
    std::optional<std::string> payload;
};

// PackfileDef — the `packfile:` block of behavior.yaml.
struct PackfileDef {
    int         classVersion = 8;
    std::string contentsVersion = "hk_2010.2.0-r1";
};

} // namespace havok::model
