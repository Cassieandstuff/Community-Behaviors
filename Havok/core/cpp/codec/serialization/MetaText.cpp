#include <codec/serialization/MetaText.h>

#include <charconv>
#include <stdexcept>
#include <vector>

namespace CB::core::codec::io {
namespace {

using schema::ClassSchema;
using schema::Field;
using schema::FieldKind;

struct ReadError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Cursor {
    std::vector<std::string_view> lines;
    std::size_t                   p = 0;

    std::string_view next(const std::string& what) {
        if (p >= lines.size()) throw ReadError("unexpected end of file reading " + what);
        return lines[p++];
    }
    std::size_t count(const std::string& what) {
        const std::string_view s = next(what);
        std::size_t v = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
            throw ReadError("expected a count for " + what + " at line " + std::to_string(p) + ", got '" + std::string(s) + "'");
        return v;
    }
};

std::vector<std::string_view> SplitLines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t nl = text.find('\n', start);
        std::size_t end = nl == std::string_view::npos ? text.size() : nl;
        std::size_t stop = end;
        if (stop > start && text[stop - 1] == '\r') --stop;
        out.push_back(text.substr(start, stop - start));
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return out;
}

/// @brief The value a `when:` field gates on ("1" = present).
bool Gate(const SchemaObject& obj, const std::vector<const Field*>& fields, const std::string& when) {
    if (when.empty()) return true;
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i]->name == when) return obj.Values()[i].str == "1";
    throw ReadError(std::string(obj.ClassName()) + ": `when: " + when + "` names no field");
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
    const schema::SchemaRegistry& m_reg;
    Cursor&                       m_cur;
};

class Writer {
public:
    explicit Writer(std::vector<std::string>& out) : m_out(out) {}

    void Object(const SchemaObject& obj, const std::string* hoistField) {
        const ClassSchema* cs = obj.Schema();
        const auto fields = obj.Fields();
        std::vector<std::string>* sink = &m_out;
        std::vector<std::string>  frameLines;
        std::string               frame;
        const auto closeFrame = [&] {
            if (frame.empty()) return;
            if (cs->frameLimit > 0 && frameLines.size() > static_cast<std::size_t>(cs->frameLimit))
                throw ReadError(std::string(cs->name) + " frame " + frame + " is " + std::to_string(frameLines.size()) +
                                " lines — over the engine's " + std::to_string(cs->frameLimit) + "-line limit");
            m_out.push_back(std::to_string(frameLines.size()));
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
            Writer w(*sink);
            switch (f.kind) {
                case FieldKind::Text: sink->push_back(v.str); break;
                case FieldKind::StringArray:
                    sink->push_back(std::to_string(v.strs.size()));
                    sink->insert(sink->end(), v.strs.begin(), v.strs.end());
                    break;
                case FieldKind::PtrArray: {
                    if (!f.frameCounted) sink->push_back(std::to_string(v.objs.size()));
                    std::size_t hi = 0;
                    if (!f.header.empty())
                        for (const auto& o : v.objs) {
                            const auto& so = static_cast<const SchemaObject&>(*o);
                            const auto  sf = so.Fields();
                            for (hi = 0; hi < sf.size() && sf[hi]->name != f.header; ++hi) {}
                            sink->push_back(hi < sf.size() ? so.Values()[hi].str : std::string());
                        }
                    for (const auto& o : v.objs)
                        w.Object(static_cast<const SchemaObject&>(*o), f.header.empty() ? nullptr : &f.header);
                    break;
                }
                default: throw ReadError(std::string(cs->name) + "." + f.name + ": not a metadata field kind");
            }
        }
        closeFrame();
        if (cs->blankTerminated) m_out.emplace_back();
    }

private:
    std::vector<std::string>& m_out;
};

} // namespace

std::shared_ptr<SchemaObject> ReadMetaText(const schema::SchemaRegistry& reg, const std::string& rootClass,
                                           std::string_view text, std::string& err) {
    try {
        Cursor cur{ SplitLines(text) };
        Reader r(reg, cur);
        auto root = r.Object(rootClass, nullptr, {});
        if (cur.p != cur.lines.size())
            throw ReadError(std::to_string(cur.lines.size() - cur.p) + " line(s) left after the last record");
        return root;
    } catch (const std::exception& e) {
        err = e.what();
        return nullptr;
    }
}

bool WriteMetaText(const SchemaObject& root, std::string& out, std::string& err) {
    try {
        std::vector<std::string> lines;
        Writer(lines).Object(root, nullptr);
        out.clear();
        for (const auto& l : lines) { out += l; out += "\r\n"; }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace CB::core::codec::io
