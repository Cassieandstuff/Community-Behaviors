// EvalContext.cpp — per-pass primitive memoization + scratch arena.
#include "Detail.h"

#include <cstring>

namespace CB::conditions {

    namespace {
        // Hash a primitive call site (id + raw arg bits) for the memo key.
        std::uint64_t HashCall(PrimId id, std::span<const Value> args) {
            std::uint64_t h = 1469598103934665603ull ^ static_cast<std::uint32_t>(id);
            for (const Value& a : args) {
                std::uint64_t bits = 0;
                std::memcpy(&bits, &a.form, sizeof(bits));   // 8 bytes from the union start (any member)
                h ^= static_cast<std::uint64_t>(a.type); h *= 1099511628211ull;
                h ^= bits;                                h *= 1099511628211ull;
            }
            return h;
        }
    }

    void EvalContext::Reset(RE::Actor* a, RE::hkbClipGenerator* c) {
        actor = a;
        clip  = c;
        _memo.clear();
        _arena.clear();
    }

    std::vector<Value>& EvalContext::ArenaBlock() const {
        _arena.emplace_back();            // moves of inner vectors preserve their data() → spans stay valid
        return _arena.back();
    }

    Value EvalContext::Primitive(PrimId id, std::span<const Value> args) const {
        if (id == PrimId::Invalid) return {};
        const std::uint64_t key = HashCall(id, args);
        for (const MemoEntry& e : _memo)
            if (e.id == id && e.argHash == key) return e.value;

        PrimitiveFn fn = detail::PrimFn(id);
        Value v = fn ? fn(*this, args) : Value{};
        _memo.push_back({ id, key, v });
        return v;
    }

}  // namespace CB::conditions
