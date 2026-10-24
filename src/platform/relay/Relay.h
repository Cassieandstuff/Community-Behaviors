#pragma once

/// @file
/// @brief Animation Relay: CB's condition language, the one evaluator every consumer of a condition shares — the
///        conditional setdata (core/serve/conditions) gates on it, motion matching's chooser and cost biases weigh
///        with it.
/// @note  Three layers: PRIMITIVES are the only layer that reads game state, memoized per evaluation pass;
///        FUNCTIONS are pure over primitives, the grammar-callable vocabulary; an EXPRESSION is an author-written
///        string in Havok's expression grammar over the registered callables, compiled once and evaluated per pass.
///        A result is a Value (Bool, Int or Float): a consumer reads it as a gate (AsBool) or a weight (AsFloat).
/// @note  Plugin-only (RE / SKSE / REL).

#include <PCH.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RE { class Actor; class TESForm; class BGSKeyword; }

namespace CB::platform::relay {

    // ── Value: the currency between layers ────────────────────────────────────────────────────────
    enum class VType : std::uint8_t {
        Bool, Int, Float, Form, Keyword,
        IntArray, FloatArray, FormArray, KeywordArray,
        String,   ///< a quoted literal, interned (Intern): equal text is one pointer
        None,   // uninitialised / error
    };

    struct Value;

    // A borrowed, trivially-copyable view over Value[] (safe inside Value's own union — no incomplete
    // std::span-of-self). Backed by the pass arena; valid until EvalContext::Reset.
    struct ValueSpan {
        const Value* ptr;
        std::uint32_t n;
        const Value* begin() const { return ptr; }
        const Value* end()   const;   // defined after Value (needs its size for ptr arithmetic)
        std::uint32_t size() const { return n; }
    };

    struct Value {
        VType type = VType::None;
        union {
            bool            b;
            std::int32_t    i;
            float           f;
            RE::TESForm*    form;
            RE::BGSKeyword* kw;
            ValueSpan       arr;   // borrows from the pass arena; trivially copyable
            const char*     str;   ///< String: from Intern, never freed
        };

        constexpr Value() : type(VType::None), i(0) {}

        static Value Bool(bool v)              { Value o; o.type = VType::Bool;    o.b = v;    return o; }
        static Value Int(std::int32_t v)       { Value o; o.type = VType::Int;     o.i = v;    return o; }
        static Value Float(float v)            { Value o; o.type = VType::Float;   o.f = v;    return o; }
        static Value Form(RE::TESForm* v)      { Value o; o.type = VType::Form;    o.form = v; return o; }
        static Value Keyword(RE::BGSKeyword* v){ Value o; o.type = VType::Keyword; o.kw = v;   return o; }
        /// @pre `v` came from Intern.
        static Value String(const char* v)     { Value o; o.type = VType::String;  o.str = v;  return o; }
        static Value Array(VType t, ValueSpan v)              { Value o; o.type = t; o.arr = v; return o; }
        static Value Array(VType t, const std::vector<Value>& v) {
            Value o; o.type = t; o.arr = { v.data(), static_cast<std::uint32_t>(v.size()) }; return o; }

        // Coercions used by the grammar (bool/int/float interchange; everything numeric in Havok).
        bool  AsBool()  const;
        float AsFloat() const;
        std::int32_t AsInt() const;
    };

    inline const Value* ValueSpan::end() const { return ptr + n; }   // Value now complete

    /// @brief The process-lifetime copy of `text`, byte-exact (case kept): equal text always yields the same pointer.
    /// @note  Why interned: a String's pointer is its identity, so the primitive memo (which hashes the argument's bits)
    ///        shares one read between every expression naming the same variable, and a Value stays trivially copyable.
    const char* Intern(std::string_view text);

    /// @brief Graph variables a caller knows for this pass, by name (byte-exact), consulted by GraphVar before the
    ///        actor's graph.
    /// @note  Why: a caller that computes a variable this frame (motion matching's chooser inputs) knows its value and
    ///        type before the graph does; the inputs are declared on a referenced behavior, outside the root graph's
    ///        variable table GraphVar takes types from.
    class VariableSource {
    public:
        virtual ~VariableSource() = default;
        /// @return true with `out` set (Bool, Int or Float) when `name` is one of this source's variables
        virtual bool Read(const char* name, Value& out) const = 0;
    };

    // ── Handles ───────────────────────────────────────────────────────────────────────────────────
    // Stable id for a registered primitive (used by functions to pull memoized reads).
    enum class PrimId : std::uint32_t { Invalid = 0xFFFFFFFF };
    enum class FnId   : std::uint32_t { Invalid = 0xFFFFFFFF };

    // ── EvalContext: the actor + per-pass primitive memo + a scratch arena ────────────────────────
    class EvalContext {
    public:
        RE::Actor*            actor = nullptr;   ///< the actor the pass evaluates against
        /// @brief Set by the caller per expression (the conditional setdata: its config's id). Cleared by Reset.
        /// @note  Not read yet: Random() seeds by the actor alone until it folds this in.
        std::uint32_t         seed = 0;
        /// @brief Read by GraphVar before the actor's graph; null = the graph alone. Cleared by Reset.
        const VariableSource* variables = nullptr;

        // Compute-or-return-cached: a primitive runs at most once per (id, args) per pass.
        Value Primitive(PrimId id, std::span<const Value> args = {}) const;

        // Per-pass arena for array-valued primitive outputs (spans in Value borrow from here).
        std::vector<Value>& ArenaBlock() const;   // returns a fresh, stable block for this pass

        void Reset(RE::Actor* a);   // clear memo/arena, set actor for a new pass

    private:
        struct MemoEntry { PrimId id; std::uint64_t argHash; Value value; };
        mutable std::vector<MemoEntry>              _memo;
        mutable std::vector<std::vector<Value>>     _arena;   // stable blocks (deque-like via vector-of-vector)
    };

    // ── Layer callbacks ───────────────────────────────────────────────────────────────────────────
    using PrimitiveFn = Value (*)(const EvalContext&, std::span<const Value> args);
    using FunctionFn  = Value (*)(const EvalContext&, std::span<const Value> args);

    // ── Registry (the public extensibility surface) ───────────────────────────────────────────────
    // Register during SKSE kPostLoad or earlier. Names should be namespaced by non-CB authors ("Mod:X").
    PrimId RegisterPrimitive(std::string_view name, VType returns,
                             std::span<const VType> params, PrimitiveFn fn);
    void   RegisterFunction (std::string_view name, VType returns,
                             std::span<const VType> params, FunctionFn fn);

    PrimId LookupPrimitive(std::string_view name);   // Invalid if absent
    FnId   LookupFunction (std::string_view name);   // Invalid if absent

    // Flat RPN node. Const → _consts[index]; CallFn → FnId in index, argc args; Unary/Binary → op.
    struct ExprNode {
        enum Kind : std::uint8_t { Const, CallFn, Unary, Binary } kind;
        std::uint16_t op = 0;      // Unary/Binary: operator code (Expression.cpp Op enum)
        std::uint16_t argc = 0;    // CallFn: argument count
        std::uint32_t index = 0;   // Const: into _consts; CallFn: FnId
    };

    // ── Compiled expression ───────────────────────────────────────────────────────────────────────
    class CompiledExpr {
    public:
        bool  Valid() const { return _ok; }
        Value Evaluate(const EvalContext& ctx) const;   // walks the RPN; leaves are fn/prim calls

    private:
        friend CompiledExpr Compile(std::string_view, std::string&);
        std::vector<ExprNode> _rpn;
        std::vector<Value>    _consts;
        bool  _ok = false;
    };

    // Parse + resolve (callables against the registry, arg literals against the load order) + typecheck.
    // On failure returns an invalid expr and fills `err`.
    CompiledExpr Compile(std::string_view source, std::string& err);

    // Resolves a bare-identifier literal argument (keyword/form EditorID, AV name, …) to a Value.
    // Set by the config loader (Inc 3, reuses the linker/oracle). Unset → such identifiers fail to compile.
    using ArgResolver = bool (*)(std::string_view token, Value& out);
    void SetArgResolver(ArgResolver);

    /// @brief Register the built-in primitive/function roster (Builtins.cpp).
    /// @note  Idempotent; installs no hook. Called at SKSEPluginLoad, before anything compiles an expression.
    void RegisterBuiltins();

}  // namespace CB::platform::relay
