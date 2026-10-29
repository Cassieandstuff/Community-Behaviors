#include <rules/CapsuleFromBound.h>
#include <rules/HoistBoneData.h>

namespace CB::engine::rules {

/// @brief Register every op the engine itself implements; OpRegistry::Instance() calls it once.
/// @note  An op of the engine is listed here, never self-registered: the engine is a static library, and a TU only a
///        static registrar points into is dropped by the linker.
void RegisterBuiltinOps(OpRegistry& reg) {
    reg.Register("HoistBoneData", [](const schema::RuleDef& r, std::string& e) -> std::unique_ptr<IOp> {
        return std::make_unique<HoistBoneData>(r, e);
    });
    reg.Register("CapsuleFromBound", [](const schema::RuleDef& r, std::string& e) -> std::unique_ptr<IOp> {
        return std::make_unique<CapsuleFromBound>(r, e);
    });
}

}  // namespace CB::engine::rules
