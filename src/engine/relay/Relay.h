#pragma once

/// @file
/// @brief Animation Relay: CB's condition language, the one evaluator every consumer of a condition shares — the
///        conditional setdata (core/serve/conditions) gates on it, motion matching's chooser and cost biases weigh
///        with it.
/// @note  Three layers: PRIMITIVES are the only layer that reads game state, memoized per evaluation pass;
///        FUNCTIONS are pure over primitives, the grammar-callable vocabulary; an EXPRESSION is an author-written
///        string in Havok's expression grammar over the registered callables, compiled once and evaluated per pass.
///        A result is a Value (Bool, Int or Float): a consumer reads it as a gate (AsBool) or a weight (AsFloat).
/// @note  This core is CommonLib-free: forms, keywords and the actor are opaque pointers here. The game-state primitives
///        and the helpers casting those pointers back live in the plugin (<platform/relay/Relay.h>); an offline tool
///        registers RegisterCoreBuiltins instead, so an expression calling a game-state function does not compile there.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CB::engine::relay {

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
            void*           form;   ///< opaque to the engine: the plugin's RE::TESForm*, always as that exact type
            void*           kw;     ///< opaque to the engine: the plugin's RE::BGSKeyword*, always as that exact type
            ValueSpan       arr;   // borrows from the pass arena; trivially copyable
            const char*     str;   ///< String: from Intern, never freed
        };

        constexpr Value() : type(VType::None), i(0) {}

        static Value Bool(bool v)              { Value o; o.type = VType::Bool;    o.b = v;    return o; }
        static Value Int(std::int32_t v)       { Value o; o.type = VType::Int;     o.i = v;    return o; }
        static Value Float(float v)            { Value o; o.type = VType::Float;   o.f = v;    return o; }
        static Value Form(void* v)             { Value o; o.type = VType::Form;    o.form = v; return o; }
        static Value Keyword(void* v)          { Value o; o.type = VType::Keyword; o.kw = v;   return o; }
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
        void*                 actor = nullptr;   ///< the actor the pass evaluates against (opaque; null offline)
        /// @brief Set by the caller per expression (the conditional setdata: its config's id). Cleared by Reset.
        /// @note  Not read yet: Random() seeds by the actor alone until it folds this in.
        std::uint32_t         seed = 0;
        /// @brief Read by GraphVar before the actor's graph; null = the graph alone. Cleared by Reset.
        const VariableSource* variables = nullptr;

        // Compute-or-return-cached: a primitive runs at most once per (id, args) per pass.
        Value Primitive(PrimId id, std::span<const Value> args = {}) const;

        // Per-pass arena for array-valued primitive outputs (spans in Value borrow from here).
        std::vector<Value>& ArenaBlock() const;   // returns a fresh, stable block for this pass

        void Reset(void* a);   // clear memo/arena, set actor for a new pass

    private:
        struct MemoEntry { PrimId id; std::uint64_t argHash; Value value; };
        mutable std::vector<MemoEntry>              _memo;
        mutable std::vector<std::vector<Value>>     _arena;   // stable blocks (deque-like via vector-of-vector)
    };

    // ── Layer callbacks ───────────────────────────────────────────────────────────────────────────
    using PrimitiveFn = Value (*)(const EvalContext&, std::span<const Value> args);
    using FunctionFn  = Value (*)(const EvalContext&, std::span<const Value> args);

    // ── Registry (the public extensibility surface) ───────────────────────────────────────────────
    // Register before anything compiles an expression. Names should be namespaced by non-CB authors ("Mod:X").
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
    /// @note A catalog function nothing has bound (a game function, offline) does not compile: motion matching's offline
    ///       database build relies on that to warn that a gate cannot hold there. An editor checks with Check instead.
    CompiledExpr Compile(std::string_view source, std::string& err);

    // ── Syntax: an expression as written ──────────────────────────────────────────────────────────
    /// @brief One node of a parsed expression. Spellings are kept byte-exact: a number's digits, a string's text and quote.
    struct SyntaxNode {
        enum class Kind : std::uint8_t {
            Number,   ///< `text` = its spelling ("1", "0.5", "0x1F")
            String,   ///< `text` = what is between the quotes, `quote` = the quote character
            Name,     ///< a bare identifier: a nullary function written without parentheses, or a literal the ArgResolver resolves
            Call,     ///< `text` = the function's name, `children` = its arguments
            Unary,    ///< `text` = "!", "-" or "+", `children` = { operand }
            Binary,   ///< `text` = the operator, `children` = { left, right }
            Group,    ///< parentheses as written, `children` = { inner }
        };
        Kind                    kind = Kind::Number;
        std::string             text;
        char                    quote = '"';
        std::uint32_t           begin = 0;   ///< byte offset of its first character in the source
        std::uint32_t           end   = 0;   ///< one past its last
        std::vector<SyntaxNode> children;
    };

    struct ParseResult {
        bool          ok = false;
        SyntaxNode    root;
        std::string   error;        ///< why it does not parse; empty when ok
        std::uint32_t errorAt = 0;  ///< byte offset the error points at
    };

    /// @brief The syntax tree of `source`, resolving no name: an unknown function still parses.
    ParseResult Parse(std::string_view source);

    /// @brief Text for `node` that parses back to the same tree, Group nodes aside.
    /// @note  Parentheses come from Group nodes and from precedence: a child binding looser than its parent, or a right
    ///        operand binding as loosely (the operators associate left), is wrapped. One space each side of a binary
    ///        operator, none inside a call or after a unary one.
    std::string Print(const SyntaxNode& node);

    /// @brief Whether `source` parses and typechecks against the catalog and the registry, bound or not.
    /// @note  For an editor: a game function is declared offline but bound only in the plugin, where Compile and Check agree.
    bool Check(std::string_view source, std::string& err);

    // ── Catalog: every function CB declares, bound or not ─────────────────────────────────────────
    /// @brief One argument of a catalog function.
    struct ParamInfo {
        std::string_view name;
        VType            type;
        std::string_view choices;   ///< the Choices table an Int argument takes its values from; empty = any value
    };

    /// @brief A function the grammar can call: its signature and what an editor shows of it.
    struct CallableInfo {
        std::string_view           name;       ///< as written in an expression, byte-exact
        VType                      returns;
        std::span<const ParamInfo> params;
        std::string_view           category;   ///< an editor's group: Graph, Values, Actor, Movement, Equipment, Forms, World
        std::string_view           doc;        ///< one line: what it answers
    };

    /// @brief One named value of a choice table.
    struct Choice {
        std::string_view name;   ///< as the game's own enumerator spells it, without its k
        std::int32_t     value;
    };

    std::span<const CallableInfo> ListCallables();
    /// @return null when `name` (byte-exact) is not in the catalog
    const CallableInfo* FindCallable(std::string_view name);
    /// @return empty when no table is called `table`
    std::span<const Choice> Choices(std::string_view table);

    /// @brief Register `fn` as the implementation of the catalog function `name`, with the catalog's signature.
    /// @return false with `err` when `name` is not in the catalog or is already bound
    bool Bind(std::string_view name, FunctionFn fn, std::string& err);

    /// @brief The catalog functions nothing has bound yet.
    /// @note  The plugin binds every one at startup and logs an error per name this returns; offline, the game's stay unbound.
    std::vector<std::string_view> Unbound();

    // Resolves a bare-identifier literal argument (keyword/form EditorID, AV name, …) to a Value.
    // Set by the config loader (Inc 3, reuses the linker/oracle). Unset → such identifiers fail to compile.
    using ArgResolver = bool (*)(std::string_view token, Value& out);
    void SetArgResolver(ArgResolver);

    /// @brief Where the registry's warnings go; null (the default) drops them.
    /// @note  Why a sink: the engine has no logger. The plugin points it at its log before it registers anything.
    using WarnSink = void (*)(std::string_view message);
    void SetWarnSink(WarnSink sink);

    /// @brief Register the game-free roster: GraphVar, reading only EvalContext::variables (0.0 when absent).
    /// @note  Idempotent, for offline use. The plugin registers its own richer GraphVar and must not call this: the
    ///        first registration of a name wins.
    void RegisterCoreBuiltins();

}  // namespace CB::engine::relay
