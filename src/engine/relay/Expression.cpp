// Expression.cpp — parse the condition DSL (Havok grammar + registered callables) to a syntax tree, lower that tree to
// RPN, and evaluate it. Leaves are function calls resolved against the registry.
//
// Grammar (subset of hkbCompiledExpressionSet, evaluated by us — see havok-expression-grammar):
//   unary   ! - +
//   binary  * / %   + -   < > <= >=   == !=   &&   ||   (C precedence)
//   atoms   number literal | "string" / 'string' literal | ident | ident( args... )
// Identifier atoms/args that aren't numbers resolve via the ArgResolver (forms/keywords/AVs by name)
// or as a registered nullary function. Full arg-literal resolution across the load order is an Inc 3 item.
#include "Detail.h"

#include <cctype>
#include <cmath>
#include <format>

namespace CB::engine::relay {

    // ── operator codes ──────────────────────────────────────────────────────────────────────────
    enum Op : std::uint16_t {
        kNot, kNeg, kPos,                       // unary
        kMul, kDiv, kMod, kAdd, kSub,           // arithmetic
        kLt, kGt, kLe, kGe, kEq, kNe,           // comparison
        kAnd, kOr                               // logical
    };

    // (ExprNode is defined in Relay.h so the expr is copyable where Node is unseen.)

    // ── arg-literal resolver seam (set by the config loader; Inc 3) ─────────────────────────────
    namespace {
        ArgResolver g_argResolver = nullptr;
    }
    void SetArgResolver(ArgResolver r) { g_argResolver = r; }

    // ── tokenizer ────────────────────────────────────────────────────────────────────────────────
    namespace {
        struct Tok {
            enum K { Num, Ident, Str, Op, LParen, RParen, Comma, End, Bad } k;
            std::string   s;
            std::uint32_t begin = 0, end = 0;
            char          quote = '"';
        };

        struct Lexer {
            std::string_view src; size_t i = 0;
            static bool identStart(char c) { return std::isalpha((unsigned char)c) || c == '_' || c == ':'; }
            static bool identCont(char c)  { return std::isalnum((unsigned char)c) || c == '_' || c == ':'; }

            Tok make(Tok::K k, std::string s, size_t from) const {
                return { k, std::move(s), static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(i) };
            }

            Tok next() {
                while (i < src.size() && std::isspace((unsigned char)src[i])) ++i;
                const size_t from = i;
                if (i >= src.size()) return make(Tok::End, "", from);
                char c = src[i];
                if (c == '(') { ++i; return make(Tok::LParen, "(", from); }
                if (c == ')') { ++i; return make(Tok::RParen, ")", from); }
                if (c == ',') { ++i; return make(Tok::Comma, ",", from); }
                /// @note A string literal is "..." or '...' (either, so a YAML scalar can quote with the other), no
                ///       escapes, its text kept byte-exact.
                if (c == '"' || c == '\'') {
                    const size_t close = src.find(c, i + 1);
                    if (close == std::string_view::npos) { i = src.size(); Tok t = make(Tok::Bad, "unterminated string literal", from); t.end = t.begin + 1; return t; }
                    std::string text(src.substr(i + 1, close - i - 1));
                    i = close + 1;
                    Tok t = make(Tok::Str, std::move(text), from);
                    t.quote = c;
                    return t;
                }
                if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < src.size())) {
                    size_t j = i; bool hex = false;
                    if (c == '0' && i + 1 < src.size() && (src[i+1] == 'x' || src[i+1] == 'X')) { hex = true; j = i + 2; while (j < src.size() && std::isxdigit((unsigned char)src[j])) ++j; }
                    else { while (j < src.size() && (std::isdigit((unsigned char)src[j]) || src[j] == '.')) ++j; }
                    std::string s(src.substr(i, j - i));
                    /// @note A lone '.', a bare 0x and a number past double's range all throw; a load must get an error from them, not an exception.
                    try {
                        (void)(hex ? static_cast<double>(std::stoull(s, nullptr, 16)) : std::stod(s));
                    } catch (const std::logic_error&) {
                        i = j;
                        return make(Tok::Bad, "bad number '" + s + "'", from);
                    }
                    i = j; return make(Tok::Num, std::move(s), from);
                }
                if (identStart(c)) { size_t j = i; while (j < src.size() && identCont(src[j])) ++j; std::string s(src.substr(i, j - i)); i = j; return make(Tok::Ident, std::move(s), from); }
                // operators (2-char first)
                auto two = src.substr(i, 2);
                for (auto o : { "<=", ">=", "==", "!=", "&&", "||" })
                    if (two == o) { i += 2; return make(Tok::Op, std::string(o), from); }
                i += 1; return make(Tok::Op, std::string(1, c), from);
            }
        };

        std::uint16_t binOp(std::string_view s) {
            if (s == "*") return kMul; if (s == "/") return kDiv; if (s == "%") return kMod;
            if (s == "+") return kAdd; if (s == "-") return kSub;
            if (s == "<") return kLt;  if (s == ">") return kGt; if (s == "<=") return kLe; if (s == ">=") return kGe;
            if (s == "==") return kEq; if (s == "!=") return kNe;
            if (s == "&&") return kAnd; if (s == "||") return kOr;
            return 0xFFFF;
        }
        int prec(std::uint16_t op) {
            switch (op) { case kOr: return 1; case kAnd: return 2; case kEq: case kNe: return 3;
                case kLt: case kGt: case kLe: case kGe: return 4; case kAdd: case kSub: return 5;
                case kMul: case kDiv: case kMod: return 6; default: return 0; }
        }
        std::uint16_t unOp(std::string_view s) { return s == "!" ? kNot : s == "-" ? kNeg : kPos; }
    }

    // ── parser (Pratt → syntax tree) ──────────────────────────────────────────────────────────────
    namespace {
        using Kind = SyntaxNode::Kind;

        struct Parser {
            Lexer         lex;
            Tok           cur;
            bool          ok = true;
            std::string   error;
            std::uint32_t errorAt = 0;

            void advance() { cur = lex.next(); }
            void fail(std::string m, std::uint32_t at) {
                if (!ok) return;
                ok      = false;
                error   = std::move(m);
                errorAt = at;
            }

            SyntaxNode node(Kind k, std::string text, std::uint32_t begin, std::uint32_t end) {
                SyntaxNode n;
                n.kind  = k;
                n.text  = std::move(text);
                n.begin = begin;
                n.end   = end;
                return n;
            }

            SyntaxNode parseAtom() {
                if (cur.k == Tok::Num) { SyntaxNode n = node(Kind::Number, cur.s, cur.begin, cur.end); advance(); return n; }
                if (cur.k == Tok::Str) { SyntaxNode n = node(Kind::String, cur.s, cur.begin, cur.end); n.quote = cur.quote; advance(); return n; }
                if (cur.k == Tok::Bad) { fail(cur.s, cur.begin); return {}; }
                if (cur.k == Tok::LParen) {
                    const std::uint32_t begin = cur.begin;
                    advance();
                    SyntaxNode inner = parseExpr(0);
                    if (!ok) return {};
                    if (cur.k != Tok::RParen) { fail("expected ')'", cur.begin); return {}; }
                    SyntaxNode n = node(Kind::Group, "", begin, cur.end);
                    n.children.push_back(std::move(inner));
                    advance();
                    return n;
                }
                if (cur.k == Tok::Op && (cur.s == "!" || cur.s == "-" || cur.s == "+")) {
                    SyntaxNode n = node(Kind::Unary, cur.s, cur.begin, cur.end);
                    advance();
                    SyntaxNode operand = parseAtom();
                    if (!ok) return {};
                    n.end = operand.end;
                    n.children.push_back(std::move(operand));
                    return n;
                }
                if (cur.k == Tok::Ident) {
                    SyntaxNode n = node(Kind::Name, cur.s, cur.begin, cur.end);
                    advance();
                    if (cur.k != Tok::LParen) return n;
                    n.kind = Kind::Call;
                    advance();
                    if (cur.k != Tok::RParen) {
                        while (true) {
                            n.children.push_back(parseExpr(0));
                            if (!ok) return {};
                            if (cur.k == Tok::Comma) { advance(); continue; }
                            break;
                        }
                    }
                    if (cur.k != Tok::RParen) { fail("expected ')' in call to '" + n.text + "'", cur.begin); return {}; }
                    n.end = cur.end;
                    advance();
                    return n;
                }
                fail("unexpected token", cur.begin);
                return {};
            }

            SyntaxNode parseExpr(int minPrec) {
                SyntaxNode left = parseAtom();
                while (ok && cur.k == Tok::Op) {
                    const std::uint16_t op = binOp(cur.s);
                    if (op == 0xFFFF) break;
                    const int p = prec(op);
                    if (p < minPrec || p == 0) break;
                    SyntaxNode n = node(Kind::Binary, cur.s, left.begin, 0);
                    advance();
                    SyntaxNode right = parseExpr(p + 1);
                    if (!ok) return {};
                    n.end = right.end;
                    n.children.push_back(std::move(left));
                    n.children.push_back(std::move(right));
                    left = std::move(n);
                }
                return left;
            }
        };
    }

    ParseResult Parse(std::string_view source) {
        Parser p;
        p.lex.src = source;
        p.advance();
        ParseResult out;
        out.root = p.parseExpr(0);
        if (p.ok && p.cur.k != Tok::End) p.fail("trailing tokens", p.cur.begin);
        out.ok      = p.ok;
        out.error   = p.error;
        out.errorAt = p.errorAt;
        if (!out.ok) out.root = {};
        return out;
    }

    // ── printer ───────────────────────────────────────────────────────────────────────────────────
    namespace {
        /// @brief How tightly a node binds when printed: binaries by operator, a unary above them all, an atom tightest.
        int Binding(const SyntaxNode& n) {
            if (n.kind == Kind::Binary) return prec(binOp(n.text));
            if (n.kind == Kind::Unary) return 7;
            return 8;
        }

        void PrintTo(const SyntaxNode& n, std::string& out);

        void PrintWrapped(const SyntaxNode& n, bool wrap, std::string& out) {
            if (wrap) out += '(';
            PrintTo(n, out);
            if (wrap) out += ')';
        }

        void PrintTo(const SyntaxNode& n, std::string& out) {
            switch (n.kind) {
                case Kind::Number:
                case Kind::Name: out += n.text; break;
                case Kind::String: {
                    /// @note A string can't hold the quote that closes it; the other quote keeps the text exact.
                    char q = n.quote == '\'' ? '\'' : '"';
                    if (n.text.find(q) != std::string::npos) q = q == '"' ? '\'' : '"';
                    out += q;
                    out += n.text;
                    out += q;
                    break;
                }
                case Kind::Call:
                    out += n.text;
                    out += '(';
                    for (std::size_t k = 0; k < n.children.size(); ++k) {
                        if (k) out += ", ";
                        PrintTo(n.children[k], out);
                    }
                    out += ')';
                    break;
                case Kind::Unary:
                    out += n.text;
                    if (!n.children.empty()) PrintWrapped(n.children[0], Binding(n.children[0]) < 7, out);
                    break;
                case Kind::Binary: {
                    const int p = Binding(n);
                    if (n.children.size() > 0) PrintWrapped(n.children[0], Binding(n.children[0]) < p, out);
                    out += ' ';
                    out += n.text;
                    out += ' ';
                    if (n.children.size() > 1) PrintWrapped(n.children[1], Binding(n.children[1]) <= p, out);
                    break;
                }
                case Kind::Group:
                    out += '(';
                    if (!n.children.empty()) PrintTo(n.children[0], out);
                    out += ')';
                    break;
            }
        }
    }

    std::string Print(const SyntaxNode& node) {
        std::string out;
        PrintTo(node, out);
        return out;
    }

    // ── lowering (syntax tree → RPN), resolving and typechecking ─────────────────────────────────
    namespace {
        struct Signature {
            VType              returns = VType::None;
            std::vector<VType> params;
            FnId               id = FnId::Invalid;   ///< Invalid: declared in the catalog, bound by nothing
        };

        struct Lowerer {
            bool                   declaredOk = false;   ///< Check: a catalog function nothing bound still resolves
            std::string*           err        = nullptr;
            std::vector<ExprNode>* rpn        = nullptr;
            std::vector<Value>*    consts     = nullptr;
            bool                   ok         = true;

            void fail(std::string m) { if (ok && err) *err = std::move(m); ok = false; }
            std::uint32_t addConst(Value v) { consts->push_back(v); return static_cast<std::uint32_t>(consts->size() - 1); }

            /// @return false with the reason said when `name` resolves to nothing callable here
            bool resolve(const std::string& name, Signature& sig) {
                if (const FnId id = LookupFunction(name); id != FnId::Invalid) {
                    sig.returns = detail::FnReturns(id);
                    const auto params = detail::FnParams(id);
                    sig.params.assign(params.begin(), params.end());
                    sig.id = id;
                    return true;
                }
                const CallableInfo* declared = FindCallable(name);
                if (declared && declaredOk) {
                    sig.returns = declared->returns;
                    for (const ParamInfo& p : declared->params) sig.params.push_back(p.type);
                    return true;
                }
                fail(declared ? "'" + name + "' has no implementation here (a game function, bound only in the game)" : "unknown function '" + name + "'");
                return false;
            }

            /// @brief A primitive reads its argument as the type it declared, so `Global(1)` would read a number as a form pointer.
            bool checkArgs(const std::string& name, const Signature& sig, const std::vector<VType>& got) {
                const auto numeric = [](VType t) { return t == VType::Bool || t == VType::Int || t == VType::Float; };
                for (std::size_t k = 0; k < sig.params.size(); ++k) {
                    if (numeric(sig.params[k]) ? numeric(got[k]) : got[k] == sig.params[k]) continue;
                    fail(std::format("'{}': argument {} has the wrong type", name, k + 1));
                    return false;
                }
                return true;
            }

            void pushCall(const Signature& sig, std::uint16_t argc) {
                rpn->push_back({ ExprNode::CallFn, 0, argc, static_cast<std::uint32_t>(sig.id) });
            }

            /// @return the type the node leaves on the stack
            VType lower(const SyntaxNode& n) {
                if (!ok) return VType::None;
                switch (n.kind) {
                    case Kind::Number: {
                        const bool hex = n.text.size() > 1 && n.text[0] == '0' && (n.text[1] == 'x' || n.text[1] == 'X');
                        double     v   = 0;
                        try {
                            v = hex ? static_cast<double>(std::stoull(n.text, nullptr, 16)) : std::stod(n.text);
                        } catch (const std::logic_error&) {
                            fail("bad number '" + n.text + "'");
                            return VType::None;
                        }
                        rpn->push_back({ ExprNode::Const, 0, 0, addConst(Value::Float(static_cast<float>(v))) });
                        return VType::Float;
                    }
                    case Kind::String:
                        rpn->push_back({ ExprNode::Const, 0, 0, addConst(Value::String(Intern(n.text))) });
                        return VType::String;
                    case Kind::Group: return n.children.empty() ? VType::None : lower(n.children[0]);
                    case Kind::Unary: {
                        if (n.children.empty()) { fail("unexpected token"); return VType::None; }
                        lower(n.children[0]);
                        const std::uint16_t op = unOp(n.text);
                        rpn->push_back({ ExprNode::Unary, op, 0, 0 });
                        return op == kNot ? VType::Bool : VType::Float;
                    }
                    case Kind::Binary: {
                        const std::uint16_t op = binOp(n.text);
                        if (op == 0xFFFF || n.children.size() != 2) { fail("unexpected token"); return VType::None; }
                        lower(n.children[0]);
                        lower(n.children[1]);
                        rpn->push_back({ ExprNode::Binary, op, 0, 0 });
                        return op >= kLt ? VType::Bool : VType::Float;
                    }
                    case Kind::Call: {
                        std::vector<VType> got;
                        for (const SyntaxNode& arg : n.children) got.push_back(lower(arg));
                        if (!ok) return VType::None;
                        Signature sig;
                        if (!resolve(n.text, sig)) return VType::None;
                        if (sig.params.size() != n.children.size()) {
                            fail(std::format("'{}' expects {} args, got {}", n.text, sig.params.size(), n.children.size()));
                            return VType::None;
                        }
                        if (!checkArgs(n.text, sig, got)) return VType::None;
                        pushCall(sig, static_cast<std::uint16_t>(n.children.size()));
                        return sig.returns;
                    }
                    case Kind::Name: {
                        /// @note A bare identifier: a nullary function, else a literal the ArgResolver knows.
                        const bool callable = LookupFunction(n.text) != FnId::Invalid || (declaredOk && FindCallable(n.text));
                        if (callable) {
                            Signature sig;
                            if (!resolve(n.text, sig)) return VType::None;
                            if (!sig.params.empty()) { fail("'" + n.text + "' needs arguments"); return VType::None; }
                            pushCall(sig, 0);
                            return sig.returns;
                        }
                        Value lit;
                        if (g_argResolver && g_argResolver(n.text, lit)) {
                            rpn->push_back({ ExprNode::Const, 0, 0, addConst(lit) });
                            return lit.type;
                        }
                        fail(FindCallable(n.text) ? "'" + n.text + "' has no implementation here (a game function, bound only in the game)"
                                                  : "unresolved identifier '" + n.text + "'");
                        return VType::None;
                    }
                }
                return VType::None;
            }
        };
    }

    CompiledExpr Compile(std::string_view source, std::string& err) {
        CompiledExpr      ce;
        const ParseResult parsed = Parse(source);
        if (!parsed.ok) {
            err = parsed.error;
            return ce;
        }
        Lowerer l;
        l.err    = &err;
        l.rpn    = &ce._rpn;
        l.consts = &ce._consts;
        l.lower(parsed.root);
        ce._ok = l.ok && !ce._rpn.empty();
        if (!ce._ok && err.empty()) err = "empty expression";
        return ce;
    }

    bool Check(std::string_view source, std::string& err) {
        const ParseResult parsed = Parse(source);
        if (!parsed.ok) {
            err = parsed.error;
            return false;
        }
        std::vector<ExprNode> rpn;
        std::vector<Value>    consts;
        Lowerer               l;
        l.declaredOk = true;
        l.err        = &err;
        l.rpn        = &rpn;
        l.consts     = &consts;
        l.lower(parsed.root);
        return l.ok;
    }

    // ── evaluator ─────────────────────────────────────────────────────────────────────────────────
    Value CompiledExpr::Evaluate(const EvalContext& ctx) const {
        if (!_ok) return Value::Bool(false);
        std::vector<Value> st; st.reserve(_rpn.size());
        std::vector<Value> argbuf;
        for (const ExprNode& n : _rpn) {
            switch (n.kind) {
                case ExprNode::Const: st.push_back(_consts[n.index]); break;
                case ExprNode::Unary: {
                    Value a = st.back(); st.pop_back();
                    switch (n.op) { case kNot: st.push_back(Value::Bool(!a.AsBool())); break;
                                    case kNeg: st.push_back(Value::Float(-a.AsFloat())); break;
                                    default:   st.push_back(a); break; }
                    break;
                }
                case ExprNode::Binary: {
                    Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back();
                    const float x = a.AsFloat(), y = b.AsFloat();
                    switch (n.op) {
                        case kMul: st.push_back(Value::Float(x * y)); break;
                        case kDiv: st.push_back(Value::Float(y != 0 ? x / y : 0)); break;
                        case kMod: st.push_back(Value::Float(y != 0 ? std::fmod(x, y) : 0)); break;
                        case kAdd: st.push_back(Value::Float(x + y)); break;
                        case kSub: st.push_back(Value::Float(x - y)); break;
                        case kLt:  st.push_back(Value::Bool(x <  y)); break;
                        case kGt:  st.push_back(Value::Bool(x >  y)); break;
                        case kLe:  st.push_back(Value::Bool(x <= y)); break;
                        case kGe:  st.push_back(Value::Bool(x >= y)); break;
                        case kEq:  st.push_back(Value::Bool(x == y)); break;
                        case kNe:  st.push_back(Value::Bool(x != y)); break;
                        case kAnd: st.push_back(Value::Bool(a.AsBool() && b.AsBool())); break;
                        case kOr:  st.push_back(Value::Bool(a.AsBool() || b.AsBool())); break;
                    }
                    break;
                }
                case ExprNode::CallFn: {
                    argbuf.clear();
                    argbuf.insert(argbuf.end(), st.end() - n.argc, st.end());
                    st.erase(st.end() - n.argc, st.end());
                    FunctionFn fn = detail::FnFn(static_cast<FnId>(n.index));
                    st.push_back(fn ? fn(ctx, argbuf) : Value{});
                    break;
                }
            }
        }
        return st.empty() ? Value::Bool(false) : st.back();
    }

}  // namespace CB::engine::relay
