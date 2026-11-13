#include <relay/ConditionText.h>

#include <cctype>

namespace CB::engine::relay {

    namespace {

        bool IsNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        struct Parser {
            std::string_view      s;
            const ConditionTypes& types;
            std::string&          err;
            std::size_t           p = 0;

            bool Fail(const std::string& why)
            {
                if (err.empty()) err = why + " (at " + std::to_string(p) + ")";
                return false;
            }
            void Skip()
            {
                while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
            }
            bool Eat(std::string_view tok)
            {
                Skip();
                if (s.substr(p, tok.size()) != tok) return false;
                p += tok.size();
                return true;
            }
            std::string Name()
            {
                Skip();
                const std::size_t b = p;
                while (p < s.size() && IsNameChar(s[p])) ++p;
                return std::string(s.substr(b, p - b));
            }

            /// @brief The dotted name at `p` matched longest-first against `known`; `p` moves past it.
            template <class Map>
            std::string Longest(const Map& known)
            {
                Skip();
                const std::size_t start = p;
                std::string       best;
                std::size_t       bestEnd = start;
                std::string       name;
                std::size_t       q = start;
                while (true) {
                    const std::size_t b = q;
                    while (q < s.size() && IsNameChar(s[q])) ++q;
                    if (q == b) break;
                    name += s.substr(b, q - b);
                    if (known.find(name) != known.end()) {
                        best    = name;
                        bestEnd = q;
                    }
                    if (q < s.size() && s[q] == '.') {
                        name += '.';
                        ++q;
                    } else {
                        break;
                    }
                }
                p = bestEnd;
                return best;
            }

            bool Argument(std::vector<std::string>& out)
            {
                Skip();
                if (p < s.size() && s[p] == '"') {
                    ++p;
                    std::string v;
                    while (p < s.size() && s[p] != '"') {
                        if (s[p] == '\\' && p + 1 < s.size()) ++p;
                        v += s[p++];
                    }
                    if (p >= s.size()) return Fail("an unclosed string");
                    ++p;
                    out.push_back(std::move(v));
                    return true;
                }
                const std::size_t b = p;
                while (p < s.size() && !std::isspace(static_cast<unsigned char>(s[p])) && s[p] != ',' && s[p] != ')') ++p;
                if (p == b) return Fail("an argument is missing");
                out.emplace_back(s.substr(b, p - b));
                return true;
            }

            bool Arguments(std::vector<std::string>& out)
            {
                if (!Eat("(")) return true;
                if (Eat(")")) return true;
                do {
                    if (!Argument(out)) return false;
                } while (Eat(","));
                return Eat(")") || Fail("expected ')'");
            }

            /// @brief A scope chain with no test: `other`'s form.
            bool Chain(std::vector<ConditionStep>& out)
            {
                do {
                    ConditionStep step;
                    step.kind = Longest(types.scopes);
                    if (step.kind.empty()) return Fail("expected a scope");
                    if (!Arguments(step.arguments)) return false;
                    out.push_back(std::move(step));
                } while (Eat("."));
                return true;
            }

            bool Arity(const std::string& what, const std::vector<std::string>& want, std::size_t got)
            {
                std::size_t required = 0;
                for (const std::string& a : want)
                    if (a.empty() || a.back() != '?') ++required;
                if (got < required || got > want.size())
                    return Fail(what + " takes " + std::to_string(required) +
                                (required == want.size() ? "" : "-" + std::to_string(want.size())) + " arguments, got " +
                                std::to_string(got));
                return true;
            }

            bool Scoped(ConditionNode& n)
            {
                n.kind = ConditionNode::Kind::Scoped;
                for (const char* q : { "all", "any" }) {
                    const std::size_t save = p;
                    if (Name() == q && types.scopes.find(q) == types.scopes.end() && Eat(".")) {
                        n.all = q[0] == 'a' && q[1] == 'l';
                        break;
                    }
                    p = save;
                }
                while (true) {
                    const std::size_t save = p;
                    std::string       scope = Longest(types.scopes);
                    if (!scope.empty()) {
                        ConditionStep step{ scope, {} };
                        if (!Arguments(step.arguments)) return false;
                        if (Eat(".")) {
                            n.scope.push_back(std::move(step));
                            continue;
                        }
                    }
                    p = save;
                    break;
                }
                n.test = Longest(types.tests);
                if (n.test.empty()) return Fail("expected a test, got '" + Name() + "'");
                const auto& test = types.tests.find(n.test)->second;
                if (test.other) {
                    if (!Eat("(")) return Fail(n.test + " compares with a scope chain: " + n.test + "(<chain>)");
                    if (!Chain(n.other)) return false;
                    if (!Eat(")")) return Fail("expected ')'");
                } else if (!Arguments(n.arguments)) {
                    return false;
                }
                for (const ConditionStep& st : n.scope)
                    if (!Arity("scope '" + st.kind + "'", types.scopes.find(st.kind)->second.arguments, st.arguments.size())) return false;
                for (const ConditionStep& st : n.other)
                    if (!Arity("scope '" + st.kind + "'", types.scopes.find(st.kind)->second.arguments, st.arguments.size())) return false;
                if (!Arity("test '" + n.test + "'", test.arguments, n.arguments.size())) return false;
                std::vector<std::string> kinds;
                for (const ConditionStep& st : n.scope) kinds.push_back(st.kind);
                if (std::string why = types.Check(kinds, n.test); !why.empty()) return Fail(why);
                if (!n.other.empty()) {
                    kinds.clear();
                    for (const ConditionStep& st : n.other) kinds.push_back(st.kind);
                    if (std::string why = types.Check(kinds, "Exists"); !why.empty()) return Fail("in " + n.test + "'s chain: " + why);
                }
                return true;
            }

            bool Unary(ConditionNode& n)
            {
                if (Eat("!")) {
                    n.kind = ConditionNode::Kind::Not;
                    n.children.emplace_back();
                    return Unary(n.children.back());
                }
                if (Eat("(")) return Or(n) && (Eat(")") || Fail("expected ')'"));
                return Scoped(n);
            }

            bool Binary(ConditionNode& n, std::string_view op, ConditionNode::Kind kind, bool (Parser::*next)(ConditionNode&))
            {
                ConditionNode first;
                if (!(this->*next)(first)) return false;
                if (!Eat(op)) {
                    n = std::move(first);
                    return true;
                }
                n.kind = kind;
                n.children.push_back(std::move(first));
                do {
                    n.children.emplace_back();
                    if (!(this->*next)(n.children.back())) return false;
                } while (Eat(op));
                return true;
            }
            bool And(ConditionNode& n) { return Binary(n, "&&", ConditionNode::Kind::And, &Parser::Unary); }
            bool Or(ConditionNode& n) { return Binary(n, "||", ConditionNode::Kind::Or, &Parser::And); }
        };

        std::string Quote(const std::string& v)
        {
            std::string out = "\"";
            for (char c : v) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            return out + '"';
        }

        std::string Steps(const std::vector<ConditionStep>& steps)
        {
            std::string out;
            for (const ConditionStep& st : steps) {
                if (!out.empty()) out += '.';
                out += st.kind;
                if (!st.arguments.empty()) {
                    out += '(';
                    for (std::size_t i = 0; i < st.arguments.size(); ++i) out += (i ? ", " : "") + Quote(st.arguments[i]);
                    out += ')';
                }
            }
            return out;
        }

        std::string Format(const ConditionNode& n, int parent)
        {
            using K = ConditionNode::Kind;
            switch (n.kind) {
            case K::Not:
                return "!" + (n.children.empty() ? std::string("()") : Format(n.children[0], 3));
            case K::And:
            case K::Or: {
                const int   self = n.kind == K::And ? 2 : 1;
                std::string out;
                for (std::size_t i = 0; i < n.children.size(); ++i)
                    out += (i ? (self == 2 ? " && " : " || ") : "") + Format(n.children[i], self + 1);
                return parent > self ? "(" + out + ")" : out;
            }
            case K::Scoped:
            default: {
                std::string out = n.all ? "all." : "";
                if (!n.scope.empty()) out += Steps(n.scope) + ".";
                out += n.test;
                if (!n.other.empty()) {
                    out += "(" + Steps(n.other) + ")";
                } else if (!n.arguments.empty()) {
                    out += '(';
                    for (std::size_t i = 0; i < n.arguments.size(); ++i) out += (i ? ", " : "") + Quote(n.arguments[i]);
                    out += ')';
                }
                return out;
            }
            }
        }

    }  // namespace

    std::optional<ConditionNode> ParseConditionText(std::string_view text, const ConditionTypes& types, std::string& err)
    {
        err.clear();
        Parser        parser{ text, types, err };
        ConditionNode n;
        if (!parser.Or(n)) return std::nullopt;
        parser.Skip();
        if (parser.p != text.size()) {
            parser.Fail("unexpected text '" + std::string(text.substr(parser.p)) + "'");
            return std::nullopt;
        }
        return n;
    }

    std::string FormatConditionText(const ConditionNode& node) { return Format(node, 0); }

}  // namespace CB::engine::relay
