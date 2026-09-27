// Expression.cpp — compile the condition DSL (Havok grammar + registered callables) to RPN, and
// evaluate it. Pratt parser → flat RPN; leaves are function calls resolved against the registry.
//
// Grammar (subset of hkbCompiledExpressionSet, evaluated by us — see havok-expression-grammar):
//   unary   ! - +
//   binary  * / %   + -   < > <= >=   == !=   &&   ||   (C precedence)
//   atoms   number literal | ident | ident( args... )
// Identifier atoms/args that aren't numbers resolve via the ArgResolver (forms/keywords/AVs by name)
// or as a registered nullary function. Full arg-literal resolution across the load order is an Inc 3 item.
#include "Detail.h"
#include <PluginLogger.h>

#include <cctype>
#include <cmath>
#include <format>

namespace CB::conditions {

    // ── operator codes ──────────────────────────────────────────────────────────────────────────
    enum Op : std::uint16_t {
        kNot, kNeg, kPos,                       // unary
        kMul, kDiv, kMod, kAdd, kSub,           // arithmetic
        kLt, kGt, kLe, kGe, kEq, kNe,           // comparison
        kAnd, kOr                               // logical
    };

    // (ExprNode is defined in Conditions.h so the expr is copyable where Node is unseen.)

    // ── arg-literal resolver seam (set by the config loader; Inc 3) ─────────────────────────────
    namespace {
        ArgResolver g_argResolver = nullptr;
    }
    void SetArgResolver(ArgResolver r) { g_argResolver = r; }

    // ── tokenizer ────────────────────────────────────────────────────────────────────────────────
    namespace {
        struct Tok { enum K { Num, Ident, Op, LParen, RParen, Comma, End } k; std::string s; double num = 0; };

        struct Lexer {
            std::string_view src; size_t i = 0;
            static bool identStart(char c) { return std::isalpha((unsigned char)c) || c == '_' || c == ':'; }
            static bool identCont(char c)  { return std::isalnum((unsigned char)c) || c == '_' || c == ':'; }

            Tok next() {
                while (i < src.size() && std::isspace((unsigned char)src[i])) ++i;
                if (i >= src.size()) return { Tok::End, "" };
                char c = src[i];
                if (c == '(') { ++i; return { Tok::LParen, "(" }; }
                if (c == ')') { ++i; return { Tok::RParen, ")" }; }
                if (c == ',') { ++i; return { Tok::Comma, "," }; }
                if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < src.size())) {
                    size_t j = i; bool hex = false;
                    if (c == '0' && i + 1 < src.size() && (src[i+1] == 'x' || src[i+1] == 'X')) { hex = true; j = i + 2; while (j < src.size() && std::isxdigit((unsigned char)src[j])) ++j; }
                    else { while (j < src.size() && (std::isdigit((unsigned char)src[j]) || src[j] == '.')) ++j; }
                    Tok t{ Tok::Num, std::string(src.substr(i, j - i)) };
                    t.num = hex ? static_cast<double>(std::stoull(t.s, nullptr, 16)) : std::stod(t.s);
                    i = j; return t;
                }
                if (identStart(c)) { size_t j = i; while (j < src.size() && identCont(src[j])) ++j; Tok t{ Tok::Ident, std::string(src.substr(i, j - i)) }; i = j; return t; }
                // operators (2-char first)
                auto two = src.substr(i, 2);
                for (auto o : { "<=", ">=", "==", "!=", "&&", "||" })
                    if (two == o) { i += 2; return { Tok::Op, std::string(o) }; }
                i += 1; return { Tok::Op, std::string(1, c) };
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
    }

    // ── parser (Pratt → RPN) ──────────────────────────────────────────────────────────────────────
    namespace {
        struct Parser {
            Lexer lex; Tok cur; std::string* err;
            std::vector<ExprNode>* rpn; std::vector<Value>* consts;
            bool ok = true;

            void advance() { cur = lex.next(); }
            void fail(std::string m) { if (ok && err) *err = std::move(m); ok = false; }
            std::uint32_t addConst(Value v) { consts->push_back(v); return static_cast<std::uint32_t>(consts->size() - 1); }

            void parseAtom() {
                if (cur.k == Tok::Num) { rpn->push_back({ ExprNode::Const, 0, 0, addConst(Value::Float((float)cur.num)) }); advance(); return; }
                if (cur.k == Tok::LParen) { advance(); parseExpr(0); if (cur.k != Tok::RParen) { fail("expected ')'"); return; } advance(); return; }
                if (cur.k == Tok::Op && (cur.s == "!" || cur.s == "-" || cur.s == "+")) {
                    std::uint16_t u = cur.s == "!" ? kNot : cur.s == "-" ? kNeg : kPos; advance(); parseAtom();
                    rpn->push_back({ ExprNode::Unary, u, 0, 0 }); return;
                }
                if (cur.k == Tok::Ident) {
                    std::string name = cur.s; advance();
                    if (cur.k == Tok::LParen) {                       // function call
                        advance(); std::uint16_t argc = 0;
                        if (cur.k != Tok::RParen) {
                            // Each arg is a full sub-expression; parseAtom() resolves bare-identifier
                            // literals (keyword/form/AV) via the ArgResolver, so no special-casing here.
                            while (true) { parseExpr(0); ++argc; if (cur.k == Tok::Comma) { advance(); continue; } break; }
                        }
                        if (cur.k != Tok::RParen) { fail("expected ')' in call to '" + name + "'"); return; }
                        advance();
                        FnId id = LookupFunction(name);
                        if (id == FnId::Invalid) { fail("unknown function '" + name + "'"); return; }
                        if (detail::FnParams(id).size() != argc) { fail(std::format("'{}' expects {} args, got {}", name, detail::FnParams(id).size(), argc)); return; }
                        rpn->push_back({ ExprNode::CallFn, 0, argc, static_cast<std::uint32_t>(id) });
                        return;
                    }
                    // bare identifier: registered nullary function, else a resolvable literal
                    FnId id = LookupFunction(name);
                    if (id != FnId::Invalid) {
                        if (!detail::FnParams(id).empty()) { fail("'" + name + "' needs arguments"); return; }
                        rpn->push_back({ ExprNode::CallFn, 0, 0, static_cast<std::uint32_t>(id) }); return;
                    }
                    Value lit;
                    if (g_argResolver && g_argResolver(name, lit)) { rpn->push_back({ ExprNode::Const, 0, 0, addConst(lit) }); return; }
                    fail("unresolved identifier '" + name + "'"); return;
                }
                fail("unexpected token");
            }

            void parseExpr(int minPrec) {
                parseAtom();
                while (ok && cur.k == Tok::Op) {
                    std::uint16_t op = binOp(cur.s);
                    if (op == 0xFFFF) break;
                    int p = prec(op);
                    if (p < minPrec || p == 0) break;
                    advance(); parseExpr(p + 1);
                    rpn->push_back({ ExprNode::Binary, op, 0, 0 });
                }
            }
        };
    }

    CompiledExpr Compile(std::string_view source, std::string& err) {
        CompiledExpr ce;
        Parser p; p.err = &err; p.rpn = &ce._rpn; p.consts = &ce._consts; p.lex.src = source; p.advance();
        p.parseExpr(0);
        if (p.ok && p.cur.k != Tok::End) { err = "trailing tokens"; p.ok = false; }
        ce._ok = p.ok && !ce._rpn.empty();
        ce._resultType = VType::Bool;   // conditions reduce to bool at the gate; grammar coerces
        if (!ce._ok && err.empty()) err = "empty expression";
        return ce;
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

}  // namespace CB::conditions
