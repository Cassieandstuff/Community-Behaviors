// Registry.cpp — the primitive/function registry + Value coercions.
#include "Detail.h"
#include <PluginLogger.h>

#include <unordered_map>

namespace CB::conditions {

    // ── Value coercions ─────────────────────────────────────────────────────────────────────────
    bool Value::AsBool() const {
        switch (type) {
            case VType::Bool:  return b;
            case VType::Int:   return i != 0;
            case VType::Float: return f != 0.0f;
            case VType::Form:  return form != nullptr;
            case VType::Keyword: return kw != nullptr;
            default:           return false;
        }
    }
    float Value::AsFloat() const {
        switch (type) {
            case VType::Bool:  return b ? 1.0f : 0.0f;
            case VType::Int:   return static_cast<float>(i);
            case VType::Float: return f;
            default:           return 0.0f;
        }
    }
    std::int32_t Value::AsInt() const {
        switch (type) {
            case VType::Bool:  return b ? 1 : 0;
            case VType::Int:   return i;
            case VType::Float: return static_cast<std::int32_t>(f);
            default:           return 0;
        }
    }

    // ── Registry storage ────────────────────────────────────────────────────────────────────────
    namespace {
        struct PrimRec { std::string name; VType returns; std::vector<VType> params; PrimitiveFn fn; };
        struct FnRec   { std::string name; VType returns; std::vector<VType> params; FunctionFn  fn; };

        std::vector<PrimRec>&              Prims()     { static std::vector<PrimRec> v; return v; }
        std::vector<FnRec>&               Fns()       { static std::vector<FnRec>   v; return v; }
        std::unordered_map<std::string, std::uint32_t>& PrimIndex() {
            static std::unordered_map<std::string, std::uint32_t> m; return m; }
        std::unordered_map<std::string, std::uint32_t>& FnIndex() {
            static std::unordered_map<std::string, std::uint32_t> m; return m; }
    }

    PrimId RegisterPrimitive(std::string_view name, VType returns,
                             std::span<const VType> params, PrimitiveFn fn) {
        auto& idx = PrimIndex();
        if (auto it = idx.find(std::string(name)); it != idx.end()) {
            LOG_WARN("[cond] primitive '{}' already registered — keeping first.", name);
            return static_cast<PrimId>(it->second);
        }
        const std::uint32_t id = static_cast<std::uint32_t>(Prims().size());
        Prims().push_back({ std::string(name), returns, { params.begin(), params.end() }, fn });
        idx.emplace(std::string(name), id);
        return static_cast<PrimId>(id);
    }

    void RegisterFunction(std::string_view name, VType returns,
                          std::span<const VType> params, FunctionFn fn) {
        auto& idx = FnIndex();
        if (idx.contains(std::string(name))) {
            LOG_WARN("[cond] function '{}' already registered — keeping first.", name);
            return;
        }
        const std::uint32_t id = static_cast<std::uint32_t>(Fns().size());
        Fns().push_back({ std::string(name), returns, { params.begin(), params.end() }, fn });
        idx.emplace(std::string(name), id);
    }

    PrimId LookupPrimitive(std::string_view name) {
        auto& idx = PrimIndex();
        auto it = idx.find(std::string(name));
        return it == idx.end() ? PrimId::Invalid : static_cast<PrimId>(it->second);
    }
    FnId LookupFunction(std::string_view name) {
        auto& idx = FnIndex();
        auto it = idx.find(std::string(name));
        return it == idx.end() ? FnId::Invalid : static_cast<FnId>(it->second);
    }

    // Internal accessors used by Expression.cpp / EvalContext.cpp (declared there via extern helpers).
    namespace detail {
        PrimitiveFn PrimFn(PrimId id) {
            const auto n = static_cast<std::uint32_t>(id);
            return n < Prims().size() ? Prims()[n].fn : nullptr;
        }
        VType PrimReturns(PrimId id) {
            const auto n = static_cast<std::uint32_t>(id);
            return n < Prims().size() ? Prims()[n].returns : VType::None;
        }
        FunctionFn FnFn(FnId id) {
            const auto n = static_cast<std::uint32_t>(id);
            return n < Fns().size() ? Fns()[n].fn : nullptr;
        }
        VType FnReturns(FnId id) {
            const auto n = static_cast<std::uint32_t>(id);
            return n < Fns().size() ? Fns()[n].returns : VType::None;
        }
        std::span<const VType> FnParams(FnId id) {
            const auto n = static_cast<std::uint32_t>(id);
            return n < Fns().size() ? std::span<const VType>{ Fns()[n].params } : std::span<const VType>{};
        }
    }

}  // namespace CB::conditions
