#pragma once

#include <rules/Rules.h>

#include <cstddef>
#include <string>
#include <vector>

namespace CB::engine::rules {

/// @brief `op: HoistBoneData` — moves the bone data of every `charprop` member (schema rows `charprop:`) out of the
///        behaviors and into character properties, written into every character that runs the graph and resolved
///        against that character's own rig.
/// @note  Per site, in graph order (sorted keys) and walk order:
///        - a member bound to a character property keeps it; the inline value was the fallback of a character that
///          lacks the property, so each such runner gets the property with that value;
///        - an unbound member is bound to a property: one already holding exactly this content in every runner that
///          has it (vanilla's presets; a property this run made for the same content), else a new one — bone sets
///          named `{node}_{field}` or, for a child object, `{node}_{child}`, single bones `Bone_{bone}` — made
///          identifier-safe (hkbEvaluateExpressionModifier compiles expressions against property names);
///        - the member then holds its neutral value (CharPropNeutral) and writes as `{ charprop: '<name>' }`.
///        A neutral member, one bound to a variable, and every member of a graph no character runs (or whose rig or a
///        runner's rig is unknown) is left as it is. The record holds every original byte; Inverse restores them and
///        removes every binding, vocabulary entry and property the run added.
/// @warning A character whose rig names bones differently gets bone sets by NAME: a weight bone the source does not
///          cover takes the mask's most common weight, an index the rig lacks drops out, a missing single bone is -1.
///          Data past the graph's rig (vanilla's longer-than-rig masks) copies exactly to a character on that rig and
///          drops out of any other.
class HoistBoneData final : public IOp {
public:
    HoistBoneData(const schema::RuleDef& rule, std::string& err);
    bool        Apply(Context& ctx, std::unique_ptr<Record>& out, std::string& err) override;
    bool        Inverse(Context& ctx, const Record& record, std::string& err) override;
    std::string Summary() const override;

private:
    const schema::RuleDef& m_rule;
    std::size_t m_hoisted = 0, m_properties = 0, m_otherBound = 0, m_unresolved = 0, m_unrun = 0, m_rigless = 0;
};

/// @brief Every `charprop` member of a graph some character runs that holds bone data inline (not bound to a
///        variable): what HoistBoneData leaves behind, which a rule-applied unit set must not hold.
bool CharPropViolations(Context& ctx, std::vector<std::string>& out, std::string& err);

}  // namespace CB::engine::rules
