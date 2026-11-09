#include "Relay.h"

#include <array>
#include <utility>

/// @file
/// @brief The game-free roster: what an offline tool evaluates expressions with (the plugin's Builtins.cpp is the game's).

namespace CB::engine::relay {

    namespace {

        Value Prim_GraphVar(const EvalContext& c, std::span<const Value> a) {
            if (a.empty() || a[0].type != VType::String || !a[0].str) return Value::Float(0.0f);
            if (Value v; c.variables && c.variables->Read(a[0].str, v)) return v;
            return Value::Float(0.0f);
        }

        Value Fn_GraphVar(const EvalContext& c, std::span<const Value> a) {
            static const PrimId p = LookupPrimitive("GraphVar");
            if (a.empty()) return Value::Float(0.0f);
            const std::array<Value, 1> args{ a[0] };
            return c.Primitive(p, args);
        }

    }  // namespace

    void RegisterCoreBuiltins() {
        static bool registered = false;
        if (std::exchange(registered, true)) return;
        static constexpr std::array kP_String{ VType::String };
        RegisterPrimitive("GraphVar", VType::Float, kP_String, &Prim_GraphVar);
        std::string err;
        Bind("GraphVar", &Fn_GraphVar, err);
    }

}  // namespace CB::engine::relay
