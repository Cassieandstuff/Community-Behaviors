/// @brief engine-tests — unit tests over the engine lib (never deployed). `engine-tests <schemaDir>`; every test runs,
///        each failure prints, and the exit code is the failure count.

#include "TestSupport.h"

#include <decompile/BehaviorDecompile.h>
#include <RymlInclude.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace CB::engine::tests {

namespace {
int g_failures = 0;
}  // namespace

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++g_failures;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
}

namespace {
namespace sch = CB::engine::schema;
namespace dec = CB::engine::decompile;

/// @brief The int16 `field` of `so`, or a value no test expects when the field is missing.
std::int64_t Int16Of(const CB::engine::codec::io::SchemaObject& so, const char* field) {
    const auto j = so.IndexOf(field);
    if (!j) return 0x7fffffff;
    const auto& raw = so.Values()[*j].raw;
    if (raw.size() != 2) return 0x7fffffff;
    std::int16_t v = 0;
    std::memcpy(&v, raw.data(), 2);
    return v;
}

std::shared_ptr<CB::engine::codec::io::SchemaObject> ParseClip(const std::string& yaml, const sch::SchemaRegistry& reg,
                                                                std::string& err) {
    std::string   buf  = yaml;
    c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(buf));
    return dec::ParseEmbeddedObject(tree.rootref(), "hkbClipGenerator", reg, "test", err);
}

/// @brief A `user:` field writes `user` exactly when it holds the sentinel, and `user` reads back to the sentinel.
void UserSentinelRoundTrip(const sch::SchemaRegistry& reg) {
    std::string err;
    const auto user = ParseClip("animationBindingIndex: user\n", reg, err);
    Check(user != nullptr, "parse `animationBindingIndex: user`: " + err);
    if (!user) return;
    Check(Int16Of(*user, "animationBindingIndex") == -1, "`user` parses to the sentinel -1");
    const std::string text = dec::EmitEmbeddedObject(*user, reg);
    Check(text.find("animationBindingIndex: user\n") != std::string::npos, "the sentinel emits as `user`:\n" + text);

    const auto bound = ParseClip("animationBindingIndex: 7\n", reg, err);
    Check(bound != nullptr, "parse `animationBindingIndex: 7`: " + err);
    if (!bound) return;
    Check(Int16Of(*bound, "animationBindingIndex") == 7, "a number still parses as itself");
    const std::string bt = dec::EmitEmbeddedObject(*bound, reg);
    Check(bt.find("animationBindingIndex: 7\n") != std::string::npos, "a non-sentinel emits its number:\n" + bt);

    const auto num = ParseClip("animationBindingIndex: -1\n", reg, err);
    Check(num && Int16Of(*num, "animationBindingIndex") == -1, "the sentinel written as a number still parses");
    Check(num && dec::EmitEmbeddedObject(*num, reg).find("animationBindingIndex: user\n") != std::string::npos,
          "the sentinel written as a number re-emits as `user`");

    Check(ParseClip("animationBindingIndex: 'user'\n", reg, err) == nullptr, "a QUOTED 'user' is not the marker");
}

/// @brief LoadDir's descriptor parse refuses `user:` on anything but a single integer scalar.
void UserSentinelSchemaChecks() {
    const auto parses = [](const std::string& field) {
        sch::ClassSchema cs;
        std::string      err;
        return sch::ParseSchema("name: T\nfields:\n  - " + field + "\n", cs, err);
    };
    Check(parses("{ name: a, type: int16, user: -1 }"), "user: on an int16 loads");
    Check(!parses("{ name: a, type: float, user: -1 }"), "user: on a float is refused");
    Check(!parses("{ name: a, type: string, user: -1 }"), "user: on a string is refused");
    Check(!parses("{ name: a, type: int16, count: 2, user: -1 }"), "user: on an inline array is refused");
    Check(!parses("{ name: a, type: int16, user: none }"), "a non-integer user: value is refused");
    sch::ClassSchema cs;
    std::string      err;
    Check(sch::ParseSchema("name: T\nfields:\n  - { name: a, type: int32, user: -5 }\n", cs, err) && cs.fields.size() == 1
              && cs.fields[0].user && cs.fields[0].userValue == -5,
          "user: carries its sentinel value");
}
}  // namespace

void RunUserSentinelTests(const schema::SchemaRegistry& reg) {
    UserSentinelSchemaChecks();
    UserSentinelRoundTrip(reg);
}

}  // namespace CB::engine::tests

int main(int argc, char** argv) {
    namespace t = CB::engine::tests;
    if (argc < 2) { std::fprintf(stderr, "usage: engine-tests <schemaDir>\n"); return 2; }
    CB::engine::schema::SchemaRegistry reg;
    std::string                        err;
    if (!reg.LoadDir(argv[1], err)) { std::fprintf(stderr, "schema load: %s\n", err.c_str()); return 2; }

    t::RunUserSentinelTests(reg);
    t::RunRulesTests(reg);

    if (t::g_failures) std::fprintf(stderr, "%d failure(s)\n", t::g_failures);
    else std::printf("engine-tests: all passed\n");
    return t::g_failures;
}
