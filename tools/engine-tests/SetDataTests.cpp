/// @brief The setdata gate's enum hint (`enumby`): the schema loads it, refuses a malformed one, and GateValueName reads a
///        bound by the enum its variable picks.

#include "TestSupport.h"

#include <codec/format/AnimationSetData.h>

#include <string>

namespace CB::engine::tests {

namespace {
namespace sch = CB::engine::schema;
namespace asd = CB::engine::animsetdata;

void EnumBySchemaChecks() {
    const auto parses = [](const std::string& field) {
        sch::ClassSchema cs;
        std::string      err;
        return sch::ParseSchema("name: T\nkind: metadata\nfields:\n  - " + field + "\n", cs, err);
    };
    Check(parses("{ name: a, type: int, enumby: { field: v, iLeftHandType: HandType } }"), "enumby with a field and an enum loads");
    Check(!parses("{ name: a, type: int, enumby: { iLeftHandType: HandType } }"), "enumby without `field` is refused");
    Check(!parses("{ name: a, type: int, enumby: { field: v } }"), "enumby without an enum is refused");
    Check(!parses("{ name: a, type: int, enumby: HandType }"), "a scalar enumby is refused");
    sch::ClassSchema cs;
    std::string      err;
    Check(sch::ParseSchema("name: T\nkind: metadata\nfields:\n  - { name: a, type: int, enumby: { field: v, x: E1, y: E2 } }\n", cs, err)
              && cs.fields.size() == 1 && cs.fields[0].enumByField == "v" && cs.fields[0].enumBy.size() == 2
              && cs.fields[0].enumBy.at("y") == "E2",
          "enumby carries its field and each value's enum: " + err);
}

void GateNames(const sch::SchemaRegistry& reg) {
    Check(asd::GateValueName(reg, "iLeftHandType", 10) == "Shield", "iLeftHandType 10 reads as Shield");
    Check(asd::GateValueName(reg, "iRightHandType", 0) == "HandToHandMelee", "iRightHandType 0 reads as HandToHandMelee");
    Check(asd::GateValueName(reg, "iRightHandType", 12) == "Crossbow", "iRightHandType 12 reads as Crossbow");
    Check(asd::GateValueName(reg, "iWantMountedWeaponAnims", 0).empty(), "a variable with no enum reads as a number");
    Check(asd::GateValueName(reg, "iLeftHandType", 99).empty(), "a value no item has reads as a number");
    Check(asd::GateValueName(reg, "ileftHandType", 10).empty(), "the variable is matched byte-exact");
}
}  // namespace

void RunSetDataTests(const schema::SchemaRegistry& reg) {
    EnumBySchemaChecks();
    GateNames(reg);
}

}  // namespace CB::engine::tests
