#include <decompile/EmbeddedFile.h>

#include <decompile/BehaviorDecompile.h>
#include <RymlInclude.h>
#include <text/Text.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <vector>

namespace CB::engine::decompile {

namespace {

/// @brief One line of a text: where it starts, its length without the line break, and its length with it.
struct Line {
    std::size_t at = 0, len = 0, full = 0;
};

std::vector<Line> SplitLines(std::string_view text) {
    std::vector<Line> lines;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t nl   = text.find('\n', at);
        const std::size_t end  = nl == std::string_view::npos ? text.size() : nl;
        std::size_t       len  = end - at;
        if (len > 0 && text[at + len - 1] == '\r') --len;
        lines.push_back({ at, len, (nl == std::string_view::npos ? text.size() : nl + 1) - at });
        at = nl == std::string_view::npos ? text.size() : nl + 1;
    }
    return lines;
}

std::size_t Indent(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    return i;
}

bool Blank(std::string_view line) { return Indent(line) == line.size(); }

/// @return the key a `key:` line at `indent` spaces names ("" when the line is not one)
std::string_view KeyAt(std::string_view line, std::size_t indent) {
    if (Indent(line) != indent) return {};
    line.remove_prefix(indent);
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) return {};
    if (colon + 1 < line.size() && line[colon + 1] != ' ') return {};
    return line.substr(0, colon);
}

}  // namespace

const schema::EmbeddedKind* EmbeddedKindOf(const schema::SchemaRegistry& reg, std::string_view text) {
    text = text::StripBom(text);
    for (const Line& l : SplitLines(text)) {
        const std::string_view line = text.substr(l.at, l.len);
        if (Blank(line) || line[Indent(line)] == '#') continue;
        const std::string_view key = KeyAt(line, 0);
        for (const auto& [name, kind] : reg.EmbeddedKinds())
            if (!key.empty() && key == kind.root) return &kind;
        return nullptr;
    }
    return nullptr;
}

std::shared_ptr<codec::io::SchemaObject> ParseEmbeddedFile(std::string_view text, const schema::EmbeddedKind& kind,
                                                           const schema::SchemaRegistry& reg, const std::string& where,
                                                           std::string& err) {
    common::EnsureRymlThrows();
    std::string buffer(text::StripBom(text));
    try {
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(buffer));
        const auto    root = tree.crootref();
        const auto    key  = c4::to_csubstr(kind.root);
        if (!root.is_map() || !root.has_child(key)) {
            err = where + ": no top-level '" + kind.root + ":' map";
            return nullptr;
        }
        return ParseEmbeddedObject(root[key], kind.cls, reg, where, err);
    } catch (const std::exception& e) {
        err = where + ": " + e.what();
        return nullptr;
    }
}

std::optional<std::string> SpliceEmbeddedFile(std::string_view text, const schema::EmbeddedKind& kind,
                                              const codec::io::SchemaObject& obj, const schema::SchemaRegistry& reg,
                                              std::string& err) {
    const std::vector<Line> lines = SplitLines(text);
    const auto              lineAt = [&](std::size_t i) { return text.substr(lines[i].at, lines[i].len); };

    std::size_t rootLine = lines.size();
    for (std::size_t i = 0; i < lines.size() && rootLine == lines.size(); ++i)
        if (KeyAt(lineAt(i), 0) == kind.root) rootLine = i;
    if (rootLine == lines.size()) {
        err = "no top-level '" + kind.root + ":' map";
        return std::nullopt;
    }
    std::size_t mapEnd = rootLine + 1;
    while (mapEnd < lines.size() && (Blank(lineAt(mapEnd)) || Indent(lineAt(mapEnd)) > 0)) ++mapEnd;

    std::vector<std::string_view> fields;
    for (const schema::Field* f : obj.Fields())
        if (!f->name.empty()) fields.push_back(f->name);

    std::vector<char> removed(lines.size(), 0);
    std::size_t       insertAt = mapEnd;
    for (std::size_t i = rootLine + 1; i < mapEnd; ++i) {
        const std::string_view key = KeyAt(lineAt(i), 2);
        if (key.empty() || std::find(fields.begin(), fields.end(), key) == fields.end()) continue;
        insertAt = (std::min)(insertAt, i);
        removed[i] = 1;
        for (std::size_t j = i + 1; j < mapEnd && (Blank(lineAt(j)) || Indent(lineAt(j)) > 2); ++j) removed[j] = 1;
    }

    const bool  crlf = !lines.empty() && lines.front().full >= lines.front().len + 2;
    const char* nl   = crlf ? "\r\n" : "\n";
    std::string block;
    const std::string body = EmitEmbeddedObject(obj, reg);
    for (std::size_t at = 0; at < body.size();) {
        const std::size_t end = (std::min)(body.find('\n', at), body.size());
        block += "  " + body.substr(at, end - at) + nl;
        at = end + 1;
    }

    std::string out;
    out.reserve(text.size() + block.size());
    for (std::size_t i = 0; i <= lines.size(); ++i) {
        if (i == insertAt) {
            if (!out.empty() && out.back() != '\n') out += nl;
            out += block;
        }
        if (i < lines.size() && !removed[i]) out += text.substr(lines[i].at, lines[i].full);
    }
    return out;
}

std::optional<float> SampleCurve(const codec::io::SchemaObject& curve, float t, const schema::SchemaRegistry& reg) {
    const schema::ClassSchema* cs = curve.Schema();
    if (!cs || cs->view.kind != "curve") return std::nullopt;
    const codec::io::FieldValue* keys = curve.FindField(cs->view.roles.at("keys"));
    if (!keys || keys->objs.empty()) return std::nullopt;

    struct Key {
        float time = 0, value = 0;
        bool  step = false;
    };
    std::vector<Key> ks;
    for (const auto& p : keys->objs) {
        const auto* k = dynamic_cast<const codec::io::SchemaObject*>(p.get());
        if (!k || !k->Schema() || k->Schema()->view.kind != "curvekey") return std::nullopt;
        const auto& roles = k->Schema()->view.roles;
        Key key{ k->GetFloat(roles.at("time")), k->GetFloat(roles.at("value")), false };
        const std::string& interp = roles.at("interp");
        for (const schema::Field* f : k->Fields()) {
            if (f->name != interp) continue;
            const schema::EnumDef* e = f->enumName.empty() ? nullptr : reg.FindEnum(f->enumName);
            const long             v = static_cast<long>(k->GetInt(interp));
            if (e)
                for (const auto& item : e->items)
                    if (item.value == v) key.step = item.name == "CONSTANT";
        }
        ks.push_back(key);
    }
    if (t <= ks.front().time) return ks.front().value;
    if (t >= ks.back().time) return ks.back().value;
    std::size_t hi = 1;
    while (ks[hi].time < t) ++hi;
    const Key& a = ks[hi - 1];
    const Key& b = ks[hi];
    if (a.step || b.time <= a.time) return a.value;
    return a.value + (b.value - a.value) * ((t - a.time) / (b.time - a.time));
}

}  // namespace CB::engine::decompile
