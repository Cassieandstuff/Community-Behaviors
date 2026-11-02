#include <codec/serialization/MetaText.h>

#include <RymlInclude.h>
#include <text/Text.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CB::engine::codec::io {
namespace {

using schema::ClassSchema;
using schema::Field;
using schema::FieldKind;

struct ReadError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// @note Byte-positioned: a text line and an `encoding: binary` record read from the same stream (speeddata's text
///       path list is followed by raw bodies). `p` counts LINES only — frames are text.
struct Cursor {
    std::string_view text;
    std::size_t      pos = 0;
    std::size_t      p = 0;

    bool done() const { return pos >= text.size(); }
    std::string_view next(const std::string& what) {
        if (done()) throw ReadError("unexpected end of file reading " + what);
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pos = end + 1;
        ++p;
        return line;
    }
    std::size_t count(const std::string& what) {
        const std::string_view s = next(what);
        std::size_t v = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
            throw ReadError("expected a count for " + what + " at line " + std::to_string(p) + ", got '" + std::string(s) + "'");
        return v;
    }
    template <class T> T raw(const std::string& what) {
        if (text.size() - std::min(pos, text.size()) < sizeof(T))
            throw ReadError("unexpected end of file reading " + what + " at byte " + std::to_string(pos));
        T v;
        std::memcpy(&v, text.data() + pos, sizeof(T));
        pos += sizeof(T);
        return v;
    }
};

/// @brief The value a `when:` field gates on ("1" = present).
bool Gate(const SchemaObject& obj, const std::vector<const Field*>& fields, const std::string& when) {
    if (when.empty()) return true;
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i]->name == when) return obj.Values()[i].str == "1";
    throw ReadError(std::string(obj.ClassName()) + ": `when: " + when + "` names no field");
}

bool IsInt32(const Field& f) { return f.metaType == "int32" || f.metaType == "int"; }

/// @brief A binary float's text value: the round-tripping text form, or the raw bits (`0x%08X`) when text would not
///        carry them (NaN payloads, inf).
std::string BinaryFloatText(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    if (std::isfinite(f)) return CB::engine::text::FormatFloat(f);
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", bits);
    return b;
}

std::uint32_t BinaryFloatBits(const std::string& s) {
    if (s.rfind("0x", 0) == 0) return static_cast<std::uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
    const float   x = std::strtof(s.c_str(), nullptr);
    std::uint32_t bits;
    std::memcpy(&bits, &x, 4);
    return bits;
}

class Reader {
public:
    Reader(const schema::SchemaRegistry& reg, Cursor& cur) : m_reg(reg), m_cur(cur) {}

    /// @param hoisted the value of the record's header field, read ahead by the parent's list.
    std::shared_ptr<SchemaObject> Object(const std::string& cls, const std::string* hoistField, std::string_view hoisted) {
        const ClassSchema* cs = m_reg.Find(cls);
        if (!cs || !cs->metadata) throw ReadError("unknown metadata class '" + cls + "'");
        auto obj = std::make_shared<SchemaObject>(&m_reg, cs);
        obj->Init();
        const auto fields = obj->Fields();
        if (cs->binary) return Binary(obj, fields, hoistField, hoisted);
        std::string frame;
        std::size_t frameEnd = 0;
        const auto closeFrame = [&] {
            if (!frame.empty() && m_cur.p != frameEnd)
                throw ReadError(cls + ": frame " + frame + " ends at line " + std::to_string(m_cur.p) + ", declared " +
                                std::to_string(frameEnd));
            frame.clear();
        };
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Field& f = *fields[i];
            FieldValue&  v = obj->FieldAt(i);
            if (hoistField && f.name == *hoistField) { v.str = std::string(hoisted); continue; }
            if (f.frame != frame) {
                closeFrame();
                if (!f.frame.empty() && Gate(*obj, fields, f.when)) {
                    const std::size_t n = m_cur.count(cls + " frame " + f.frame);
                    frame = f.frame;
                    frameEnd = m_cur.p + n;
                }
            }
            if (!Gate(*obj, fields, f.when)) continue;
            switch (f.kind) {
                case FieldKind::Text: v.str = std::string(m_cur.next(cls + "." + f.name)); break;
                case FieldKind::StringArray: {
                    const std::size_t n = m_cur.count(cls + "." + f.name);
                    for (std::size_t k = 0; k < n; ++k) v.strs.emplace_back(m_cur.next(cls + "." + f.name));
                    break;
                }
                case FieldKind::PtrArray:
                    if (f.frameCounted) {
                        while (m_cur.p < frameEnd) v.objs.push_back(Object(f.ref, nullptr, {}));
                    } else {
                        const std::size_t n = m_cur.count(cls + "." + f.name);
                        std::vector<std::string_view> heads;
                        if (!f.header.empty())
                            for (std::size_t k = 0; k < n; ++k) heads.push_back(m_cur.next(cls + "." + f.name + " header"));
                        for (std::size_t k = 0; k < n; ++k)
                            v.objs.push_back(Object(f.ref, f.header.empty() ? nullptr : &f.header, f.header.empty() ? std::string_view{} : heads[k]));
                    }
                    break;
                default: throw ReadError(cls + "." + f.name + ": not a metadata field kind");
            }
        }
        closeFrame();
        if (cs->blankTerminated && !m_cur.next(cls + " terminator").empty())
            throw ReadError(cls + ": expected a blank terminator at line " + std::to_string(m_cur.p));
        return obj;
    }

private:
    /// @note A binary record: little-endian scalars in field order, a recordarray = u32 count + its records. Values
    ///       land in the same text form a text record carries, so the tree is encoding-blind above the codec.
    std::shared_ptr<SchemaObject> Binary(std::shared_ptr<SchemaObject> obj, const std::vector<const Field*>& fields,
                                         const std::string* hoistField, std::string_view hoisted) {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Field&      f = *fields[i];
            FieldValue&       v = obj->FieldAt(i);
            const std::string what = std::string(obj->ClassName()) + "." + f.name;
            if (hoistField && f.name == *hoistField) { v.str = std::string(hoisted); continue; }
            if (f.kind == FieldKind::PtrArray) {
                const auto n = m_cur.raw<std::uint32_t>(what);
                for (std::uint32_t k = 0; k < n; ++k) v.objs.push_back(Object(f.ref, nullptr, {}));
            } else if (f.kind == FieldKind::Text && f.metaType == "float") {
                v.str = BinaryFloatText(m_cur.raw<std::uint32_t>(what));
            } else if (f.kind == FieldKind::Text && IsInt32(f)) {
                v.str = std::to_string(m_cur.raw<std::int32_t>(what));
            } else if (f.kind == FieldKind::Text && f.metaType == "uint32") {
                v.str = std::to_string(m_cur.raw<std::uint32_t>(what));
            } else {
                throw ReadError(what + ": no binary encoding for this field");
            }
        }
        return obj;
    }

    const schema::SchemaRegistry& m_reg;
    Cursor&                       m_cur;
};

/// @brief One output piece: a text line (CRLF appended on join) or a binary record's raw bytes.
struct Chunk {
    std::string s;
    bool        raw = false;
    Chunk(std::string v, bool r = false) : s(std::move(v)), raw(r) {}
};

class Writer {
public:
    Writer(std::vector<Chunk>& out, bool frameLimits) : m_out(out), m_frameLimits(frameLimits) {}

    void Object(const SchemaObject& obj, const std::string* hoistField) {
        const ClassSchema* cs = obj.Schema();
        const auto fields = obj.Fields();
        if (cs->binary) {
            std::string bytes;
            Binary(obj, hoistField, bytes);
            m_out.emplace_back(std::move(bytes), true);
            return;
        }
        std::vector<Chunk>* sink = &m_out;
        std::vector<Chunk>  frameLines;
        std::string         frame;
        const auto closeFrame = [&] {
            if (frame.empty()) return;
            if (m_frameLimits && cs->frameLimit > 0 && frameLines.size() > static_cast<std::size_t>(cs->frameLimit))
                throw ReadError(std::string(cs->name) + " frame " + frame + " is " + std::to_string(frameLines.size()) +
                                " lines — over the engine's " + std::to_string(cs->frameLimit) + "-line limit");
            m_out.emplace_back(std::to_string(frameLines.size()));
            m_out.insert(m_out.end(), frameLines.begin(), frameLines.end());
            frameLines.clear();
            frame.clear();
            sink = &m_out;
        };
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Field&      f = *fields[i];
            const FieldValue& v = obj.Values()[i];
            if (hoistField && f.name == *hoistField) continue;
            const bool present = Gate(obj, fields, f.when);
            if (f.frame != frame) {
                closeFrame();
                if (!f.frame.empty() && present) { frame = f.frame; sink = &frameLines; }
            }
            if (!present) continue;
            Writer w(*sink, m_frameLimits);
            switch (f.kind) {
                case FieldKind::Text: sink->emplace_back(v.str); break;
                case FieldKind::StringArray:
                    sink->emplace_back(std::to_string(v.strs.size()));
                    for (const auto& s : v.strs) sink->emplace_back(s);
                    break;
                case FieldKind::PtrArray: {
                    if (!f.frameCounted) sink->emplace_back(std::to_string(v.objs.size()));
                    std::size_t hi = 0;
                    if (!f.header.empty())
                        for (const auto& o : v.objs) {
                            const auto& so = static_cast<const SchemaObject&>(*o);
                            const auto  sf = so.Fields();
                            for (hi = 0; hi < sf.size() && sf[hi]->name != f.header; ++hi) {}
                            sink->emplace_back(hi < sf.size() ? so.Values()[hi].str : std::string());
                        }
                    for (const auto& o : v.objs)
                        w.Object(static_cast<const SchemaObject&>(*o), f.header.empty() ? nullptr : &f.header);
                    break;
                }
                default: throw ReadError(std::string(cs->name) + "." + f.name + ": not a metadata field kind");
            }
        }
        closeFrame();
        if (cs->blankTerminated) m_out.emplace_back(std::string());
    }

private:
    template <class T> static void Put(std::string& b, T v) { b.append(reinterpret_cast<const char*>(&v), sizeof(T)); }

    static void Binary(const SchemaObject& obj, const std::string* hoistField, std::string& b) {
        const auto fields = obj.Fields();
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Field&      f = *fields[i];
            const FieldValue& v = obj.Values()[i];
            if (hoistField && f.name == *hoistField) continue;
            if (f.kind == FieldKind::PtrArray) {
                Put(b, static_cast<std::uint32_t>(v.objs.size()));
                for (const auto& o : v.objs) Binary(static_cast<const SchemaObject&>(*o), nullptr, b);
            } else if (f.kind == FieldKind::Text && f.metaType == "float") {
                Put(b, BinaryFloatBits(v.str));
            } else if (f.kind == FieldKind::Text && IsInt32(f)) {
                Put(b, static_cast<std::int32_t>(std::strtol(v.str.c_str(), nullptr, 10)));
            } else if (f.kind == FieldKind::Text && f.metaType == "uint32") {
                Put(b, static_cast<std::uint32_t>(std::strtoul(v.str.c_str(), nullptr, 10)));
            } else {
                throw ReadError(std::string(obj.ClassName()) + "." + f.name + ": no binary encoding for this field");
            }
        }
    }

    std::vector<Chunk>& m_out;
    bool                m_frameLimits;
};

} // namespace

std::shared_ptr<SchemaObject> ReadMetaText(const schema::SchemaRegistry& reg, const std::string& rootClass,
                                           std::string_view text, std::string& err) {
    try {
        Cursor cur{ text };
        Reader r(reg, cur);
        auto root = r.Object(rootClass, nullptr, {});
        if (!cur.done())
            throw ReadError(std::to_string(text.size() - cur.pos) + " byte(s) left after the last record");
        return root;
    } catch (const std::exception& e) {
        err = e.what();
        return nullptr;
    }
}

bool WriteMetaText(const SchemaObject& root, std::string& out, std::string& err, const MetaTextWriteOptions& opts) {
    try {
        std::vector<Chunk> chunks;
        Writer(chunks, opts.frameLimits).Object(root, nullptr);
        out.clear();
        for (const auto& c : chunks) { out += c.s; if (!c.raw) out += "\r\n"; }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

namespace {

/// @note Plain only when YAML cannot read the scalar as anything else or bend it (no spaces, no indicators): numbers,
///       identifiers, paths. Everything else is double-quoted, so every value reads back as the exact text written.
std::string YamlScalar(const std::string& v) {
    bool plain = !v.empty() && (v[0] != '-' || (v.size() > 1 && v[1] >= '0' && v[1] <= '9'));
    for (const unsigned char c : v)
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+' || c == '\\' || c == '/')) { plain = false; break; }
    return plain ? v : CB::engine::text::QuoteDouble(v);
}

bool ScalarsOnly(const SchemaObject& o) {
    for (const Field* f : o.Fields())
        if (f->kind != FieldKind::Text) return false;
    return true;
}

void EmitObject(const SchemaObject& obj, const std::string& ind, std::string& out, bool firstInSeq) {
    const auto fields = obj.Fields();
    bool first = true;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field&      f = *fields[i];
        const FieldValue& v = obj.Values()[i];
        out += (first && firstInSeq) ? std::string() : ind;
        first = false;
        out += f.name + ":";
        switch (f.kind) {
            case FieldKind::Text: out += " " + YamlScalar(v.str) + "\n"; break;
            case FieldKind::StringArray:
                out += v.strs.empty() ? " []\n" : "\n";
                for (const auto& s : v.strs) out += ind + "  - " + YamlScalar(s) + "\n";
                break;
            case FieldKind::PtrArray:
                out += v.objs.empty() ? " []\n" : "\n";
                for (const auto& o : v.objs) {
                    const auto& so = static_cast<const SchemaObject&>(*o);
                    out += ind + "  - ";
                    if (ScalarsOnly(so)) {
                        out += "{ ";
                        const auto sf = so.Fields();
                        for (std::size_t k = 0; k < sf.size(); ++k)
                            out += (k ? ", " : "") + sf[k]->name + ": " + YamlScalar(so.Values()[k].str);
                        out += " }\n";
                    } else {
                        EmitObject(so, ind + "    ", out, true);
                    }
                }
                break;
            default: throw ReadError(std::string(obj.ClassName()) + "." + f.name + ": not a metadata field kind");
        }
    }
}

std::string NodeText(c4::yml::ConstNodeRef n, const std::string& what) {
    if (!n.has_val()) throw ReadError(what + ": expected a scalar");
    const auto v = n.val();
    return std::string(v.str ? v.str : "", v.len);
}

std::shared_ptr<SchemaObject> ParseObject(const schema::SchemaRegistry& reg, const std::string& cls, c4::yml::ConstNodeRef n) {
    const ClassSchema* cs = reg.Find(cls);
    if (!cs || !cs->metadata) throw ReadError("unknown metadata class '" + cls + "'");
    if (!n.is_map()) throw ReadError(cls + ": expected a map");
    auto obj = std::make_shared<SchemaObject>(&reg, cs);
    obj->Init();
    const auto fields = obj->Fields();
    for (auto ch : n.children()) {
        const auto        k = ch.key();
        const std::string key(k.str, k.len);
        std::size_t       i = 0;
        while (i < fields.size() && fields[i]->name != key) ++i;
        if (i == fields.size()) throw ReadError(cls + ": unknown key '" + key + "'");
        const Field& f = *fields[i];
        FieldValue&  v = obj->FieldAt(i);
        const std::string what = cls + "." + key;
        switch (f.kind) {
            case FieldKind::Text: v.str = NodeText(ch, what); break;
            case FieldKind::StringArray:
                if (!ch.is_seq()) throw ReadError(what + ": expected a sequence");
                for (auto e : ch.children()) v.strs.push_back(NodeText(e, what));
                break;
            case FieldKind::PtrArray:
                if (!ch.is_seq()) throw ReadError(what + ": expected a sequence");
                for (auto e : ch.children()) v.objs.push_back(ParseObject(reg, f.ref, e));
                break;
            default: throw ReadError(what + ": not a metadata field kind");
        }
    }
    return obj;
}

} // namespace

std::string EmitMetaYaml(const SchemaObject& obj) {
    std::string out;
    EmitObject(obj, "", out, false);
    return out;
}

std::shared_ptr<SchemaObject> ParseMetaYaml(const schema::SchemaRegistry& reg, const std::string& cls, std::string_view yaml,
                                            std::string& err) {
    try {
        std::string   storage(CB::engine::text::StripBom(yaml));
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        return ParseObject(reg, cls, tree.crootref());
    } catch (const std::exception& e) {
        err = e.what();
        return nullptr;
    }
}

} // namespace CB::engine::codec::io
