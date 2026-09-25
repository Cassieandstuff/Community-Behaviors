#pragma once
// Conditions.h — CB's OAR-parity conditional-setdata evaluator (public API).
//
// Three layers (see ~/.claude/plans/setdata-conditional-framework.md §5/§5a):
//   PRIMITIVE  — the only layer that reads game state, memoized per evaluation pass.
//   FUNCTION   — pure logic over primitives, returns a scalar; the grammar-callable vocabulary.
//   EXPRESSION — an author-written condition string in Havok's grammar + our registered callables,
//                compiled once, evaluated JIT at the setdata matcher hook.
//
// Inc 1 (this): registry + compiler/evaluator + the matcher/producer hook skeleton + global toggle.
// The built-in primitive/function ROSTER is a separate seam (Builtins.cpp) filled on its own branch.
//
// Plugin-world only (RE / SKSE / REL). Namespace CB::conditions.

#include <PCH.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RE { class Actor; class TESForm; class BGSKeyword; class hkbClipGenerator; }

namespace CB::conditions {

    // ── Value: the currency between layers ────────────────────────────────────────────────────────
    enum class VType : std::uint8_t {
        Bool, Int, Float, Form, Keyword,
        IntArray, FloatArray, FormArray, KeywordArray,
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
        };

        constexpr Value() : type(VType::None), i(0) {}

        static Value Bool(bool v)              { Value o; o.type = VType::Bool;    o.b = v;    return o; }
        static Value Int(std::int32_t v)       { Value o; o.type = VType::Int;     o.i = v;    return o; }
        static Value Float(float v)            { Value o; o.type = VType::Float;   o.f = v;    return o; }
        static Value Form(RE::TESForm* v)      { Value o; o.type = VType::Form;    o.form = v; return o; }
        static Value Keyword(RE::BGSKeyword* v){ Value o; o.type = VType::Keyword; o.kw = v;   return o; }
        static Value Array(VType t, ValueSpan v)              { Value o; o.type = t; o.arr = v; return o; }
        static Value Array(VType t, const std::vector<Value>& v) {
            Value o; o.type = t; o.arr = { v.data(), static_cast<std::uint32_t>(v.size()) }; return o; }

        // Coercions used by the grammar (bool/int/float interchange; everything numeric in Havok).
        bool  AsBool()  const;
        float AsFloat() const;
        std::int32_t AsInt() const;
        bool  IsArray() const {
            return type == VType::IntArray || type == VType::FloatArray ||
                   type == VType::FormArray || type == VType::KeywordArray;
        }
    };

    inline const Value* ValueSpan::end() const { return ptr + n; }   // Value now complete

    // ── Handles ───────────────────────────────────────────────────────────────────────────────────
    // Stable id for a registered primitive (used by functions to pull memoized reads).
    enum class PrimId : std::uint32_t { Invalid = 0xFFFFFFFF };
    enum class FnId   : std::uint32_t { Invalid = 0xFFFFFFFF };

    // ── EvalContext: the actor + per-pass primitive memo + a scratch arena ────────────────────────
    class EvalContext {
    public:
        RE::Actor*            actor = nullptr;   // = the producer's param_1 (verified Actor*)
        RE::hkbClipGenerator* clip  = nullptr;   // may be null

        // Compute-or-return-cached: a primitive runs at most once per (id, args) per pass.
        Value Primitive(PrimId id, std::span<const Value> args = {}) const;

        // Per-pass arena for array-valued primitive outputs (spans in Value borrow from here).
        std::vector<Value>& ArenaBlock() const;   // returns a fresh, stable block for this pass

        void Reset(RE::Actor* a, RE::hkbClipGenerator* c);   // clear memo/arena, set actor for a new pass

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

    // ── Compiled expression (the config's condition) ──────────────────────────────────────────────
    class CompiledExpr {
    public:
        bool  Valid() const { return _ok; }
        VType ResultType() const { return _resultType; }
        Value Evaluate(const EvalContext& ctx) const;   // walks the RPN; leaves are fn/prim calls

    private:
        friend CompiledExpr Compile(std::string_view, std::string&);
        std::vector<ExprNode> _rpn;
        std::vector<Value>    _consts;
        VType _resultType = VType::None;
        bool  _ok = false;
    };

    // Parse + resolve (callables against the registry, arg literals against the load order) + typecheck.
    // On failure returns an invalid expr and fills `err`.
    CompiledExpr Compile(std::string_view source, std::string& err);

    // Resolves a bare-identifier literal argument (keyword/form EditorID, AV name, …) to a Value.
    // Set by the config loader (Inc 3, reuses the linker/oracle). Unset → such identifiers fail to compile.
    using ArgResolver = bool (*)(std::string_view token, Value& out);
    void SetArgResolver(ArgResolver);

    // ── Runtime condition instances (populated by config loading in Inc 2) ────────────────────────
    // A live gate: when active, its expression result is fed as {gateName -> int} into the matcher's
    // assignment array so the engine's native setdata evaluates the gate. `enabled` is the per-config
    // toggle (§8). Registration is what config-compose produces; empty until Inc 2 → the hook is inert.
    struct ConditionInstance {
        RE::BSFixedString gateName;   // interned; matched by pointer against the set's condition name
        CompiledExpr      expr;
        const bool*       enabled;    // per-config toggle flag (owned by the toggle store); nullptr = on
    };
    void RegisterConditionInstance(ConditionInstance inst);
    void ClearConditionInstances();

    // ── Global toggle + hook install ──────────────────────────────────────────────────────────────
    void SetGlobalEnabled(bool on);   // false → the whole layer short-circuits (zero cost, native behaviour)
    bool GlobalEnabled();

    // Installs the producer actor-stash + the matcher-append hook (MinHook). Idempotent; call at load.
    void InstallConditionHooks();

    // Register the built-in primitive/function roster (Builtins.cpp — SEAM for the roster branch).
    void RegisterBuiltins();

}  // namespace CB::conditions
