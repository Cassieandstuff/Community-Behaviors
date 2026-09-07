#include <havok-schema/HavokSchema.h>

#include <RymlInclude.h>   // the ONLY sanctioned way to pull in rapidyaml (C++20+ shim)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace havok::schema {

int ScalarWidth(Scalar s) {
    switch (s) {
        case Scalar::Int8:
        case Scalar::Byte:
        case Scalar::Bool:   return 1;
        case Scalar::UInt16:
        case Scalar::Int16:  return 2;
        case Scalar::Half:   return 2;
        case Scalar::UInt32:
        case Scalar::Int32:
        case Scalar::Float:  return 4;
        case Scalar::UInt64:
        case Scalar::Int64:  return 8;
    }
    return 0;
}

// ── Schema version + compatibility rule (the ONE home for the editor<->compiler gate) ─────────────
SchemaVersion SchemaVersion::Parse(const std::string& s) {
    SchemaVersion v;
    std::string   str = s;
    // trim surrounding whitespace and quotes (YAML may hand us either)
    {
        const std::string ws = " \t\r\n\"'";
        const auto a = str.find_first_not_of(ws);
        if (a == std::string::npos) return v;   // empty -> invalid
        const auto b = str.find_last_not_of(ws);
        str = str.substr(a, b - a + 1);
    }
    // split MAJOR.MINOR.PATCH  from  -prerelease
    std::string core = str;
    if (const auto d = str.find('-'); d != std::string::npos) {
        core         = str.substr(0, d);
        v.prerelease = str.substr(d + 1);
    }
    // core must be exactly three dotted, all-digit components
    int         out[3] = {0, 0, 0};
    int         idx    = 0;
    bool        any    = false;
    std::string tok;
    auto flush = [&]() -> bool {
        if (idx > 2 || tok.empty()) return false;
        for (char c : tok) if (c < '0' || c > '9') return false;
        try { out[idx++] = std::stoi(tok); } catch (const std::exception&) { return false; }
        tok.clear();
        return true;
    };
    for (char c : core) {
        if (c == '.') { if (!flush()) return v; }
        else          { tok.push_back(c); any = true; }
    }
    if (!any || !flush() || idx != 3) return v;

    v.major = out[0];
    v.minor = out[1];
    v.patch = out[2];
    v.valid = true;
    return v;
}

std::string SchemaVersion::Str() const {
    std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!prerelease.empty()) s += "-" + prerelease;
    return s;
}

SchemaCompat CheckSchemaCompat(const SchemaVersion& authored, const SchemaVersion& current,
                               std::string& why) {
    if (!authored.valid || !current.valid) {
        why = "unparseable schema version (authored='" + authored.Str() + "', current='" +
              current.Str() + "')";
        return SchemaCompat::Refuse;
    }
    // Candidate-frozen, not frozen: while EITHER side is a prerelease, only an EXACT match is safe —
    // a class can still be reordered between rc.1 and rc.2.
    if (authored.IsPrerelease() || current.IsPrerelease()) {
        if (authored.major == current.major && authored.minor == current.minor &&
            authored.patch == current.patch && authored.prerelease == current.prerelease)
            return SchemaCompat::Ok;
        why = "prerelease schema mismatch: authored " + authored.Str() + " vs current " +
              current.Str() + " (release candidates are exact-match-or-refuse)";
        return SchemaCompat::Refuse;
    }
    if (authored.major != current.major) {
        why = "schema major mismatch: authored " + authored.Str() + " vs current " + current.Str();
        return SchemaCompat::Refuse;
    }
    if (authored.minor > current.minor) {
        why = "authored against a newer schema minor (" + authored.Str() +
              ") than this compiler speaks (" + current.Str() + ")";
        return SchemaCompat::Refuse;
    }
    return SchemaCompat::Ok;   // compiler is a superset (>= authored minor); patch differences ignored
}

namespace {

std::string toStr(c4::csubstr s) { return std::string(s.str, s.len); }

// Templated over the node type so it accepts ryml Const/NodeRef alike.
template <class N> bool hasKey(N n, const char* key) {
    return n.is_map() && n.has_child(c4::to_csubstr(key));
}
template <class N> std::string childStr(N n, const char* key) {
    if (!hasKey(n, key)) return {};
    auto c = n[c4::to_csubstr(key)];
    return c.has_val() ? toStr(c.val()) : std::string{};
}

bool parseScalar(const std::string& t, Scalar& out) {
    if (t == "int8")   { out = Scalar::Int8;   return true; }
    if (t == "byte")   { out = Scalar::Byte;   return true; }
    if (t == "uint16") { out = Scalar::UInt16; return true; }
    if (t == "int16")  { out = Scalar::Int16;  return true; }
    if (t == "uint32") { out = Scalar::UInt32; return true; }
    if (t == "int32")  { out = Scalar::Int32;  return true; }
    if (t == "uint64") { out = Scalar::UInt64; return true; }
    if (t == "int64")  { out = Scalar::Int64;  return true; }
    if (t == "float")  { out = Scalar::Float;  return true; }
    if (t == "half")   { out = Scalar::Half;   return true; }
    if (t == "bool")   { out = Scalar::Bool;   return true; }
    return false;
}

int alignUp(int cursor, int n) { return n <= 0 ? cursor : ((cursor + n - 1) / n) * n; }

} // namespace

bool ParseSchema(const std::string& yamlText, ClassSchema& out, std::string& err) {
    try {
        std::string storage = yamlText;   // parse_in_place mutates the buffer
        c4::yml::Tree tree  = c4::yml::parse_in_place(c4::to_substr(storage));
        auto root = tree.rootref();
        if (!root.readable() || !root.is_map()) { err = "root is not a map"; return false; }

        out = ClassSchema{};
        out.name = childStr(root, "name");
        if (out.name.empty()) { err = "missing 'name'"; return false; }

        { const std::string p = childStr(root, "parent"); if (p != "null" && !p.empty()) out.parent = p; }
        { const std::string s = childStr(root, "signature"); if (!s.empty()) out.signature = static_cast<std::uint32_t>(std::stoul(s, nullptr, 0)); }
        { const std::string s = childStr(root, "size");      if (!s.empty()) out.size = std::stoi(s); }

        if (hasKey(root, "runtime"))
            out.runtime = (childStr(root[c4::to_csubstr("runtime")], "source") == "self")
                              ? RuntimeSource::Self : RuntimeSource::Game;

        if (hasKey(root, "fields")) {
            for (auto fn : root[c4::to_csubstr("fields")]) {
                Field f;
                if (hasKey(fn, "vtable")) {
                    f.kind = FieldKind::Vtable;
                } else if (hasKey(fn, "empty")) {
                    f.kind = (childStr(fn, "empty") == "ptr") ? FieldKind::EmptyPtr : FieldKind::EmptyArray;
                } else if (hasKey(fn, "pad")) {
                    f.kind = FieldKind::Pad;  f.count = std::stoi(childStr(fn, "pad"));
                } else if (hasKey(fn, "skip")) {
                    f.kind = FieldKind::Skip; f.count = std::stoi(childStr(fn, "skip"));
                } else if (hasKey(fn, "name")) {
                    f.name = childStr(fn, "name");
                    const std::string t = childStr(fn, "type");
                    if      (t == "string")      f.kind = FieldKind::String;
                    else if (t == "cstring")     f.kind = FieldKind::CString;
                    else if (t == "quaternion")  f.kind = FieldKind::Quaternion;
                    else if (t == "ptr")         { f.kind = FieldKind::Ptr;         f.ref = childStr(fn, "ref"); }
                    else if (t == "structarray") { f.kind = FieldKind::StructArray; f.ref = childStr(fn, "ref"); }
                    else if (t == "ptrarray")    { f.kind = FieldKind::PtrArray;    f.ref = childStr(fn, "ref"); }
                    else if (t == "vec4array")   f.kind = FieldKind::Vec4Array;
                    else if (t == "qstransformarray") f.kind = FieldKind::QsTransformArray;
                    else if (t == "stringarray") f.kind = FieldKind::StringArray;
                    else if (t == "scalararray") {
                        f.kind = FieldKind::ScalarArray;
                        const std::string el = childStr(fn, "scalar");
                        if (!parseScalar(el, f.scalar)) { err = out.name + "." + f.name + ": scalararray needs a valid 'scalar' element (got '" + el + "')"; return false; }
                    }
                    else if (t == "struct")      { f.kind = FieldKind::Struct;      f.ref = childStr(fn, "ref"); }
                    else if (t == "vector4")     f.kind = FieldKind::Vector4;
                    else if (t == "qstransform") f.kind = FieldKind::QsTransform;
                    else if (t == "boolarray")   { f.kind = FieldKind::BoolArray;   f.count = std::stoi(childStr(fn, "count")); }
                    else {
                        Scalar sc;
                        if (!parseScalar(t, sc)) { err = out.name + "." + f.name + ": unknown type '" + t + "'"; return false; }
                        f.kind = FieldKind::Scalar; f.scalar = sc;
                    }
                    f.ignored  = (childStr(fn, "ignored") == "true");
                    f.merge    = childStr(fn, "merge");
                    f.enumName = childStr(fn, "enum");   // Scalar enum type for .hky name rendering
                    f.isFlags  = (childStr(fn, "flags") == "true");   // bitfield → FormatFlags in .hky
                    f.hkyEmit  = (childStr(fn, "hky")   == "true");   // force-emit an ignored field in .hky
                    f.eventRef  = childStr(fn, "eventref");           // event-index companion key
                    f.varRef    = childStr(fn, "varref");             // variable-index companion key
                    f.rosterRef = childStr(fn, "rosterref");          // string is a member of a character roster
                                                                     // (names the target roster field, e.g.
                                                                     // "animationNames"); the cross membrane
                                                                     // COLLECTS these into that roster at resolve.
                    // fixed inline repeat count (std::array<T,N>) for scalar/ptr/vector4 — boolarray set its own above
                    if (f.kind != FieldKind::BoolArray) {
                        const std::string c = childStr(fn, "count");
                        if (!c.empty()) f.count = std::stoi(c);
                    }
                } else {
                    err = out.name + ": unrecognized field entry"; return false;
                }
                out.fields.push_back(std::move(f));
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("parse: ") + e.what();
        return false;
    }
}

bool ParseEnumDef(const std::string& yamlText, EnumDef& out, std::string& err) {
    try {
        std::string   storage = yamlText;   // parse_in_place mutates the buffer
        c4::yml::Tree tree    = c4::yml::parse_in_place(c4::to_substr(storage));
        auto          root    = tree.rootref();
        if (!root.readable() || !root.is_map()) { err = "root is not a map"; return false; }

        out      = EnumDef{};
        out.name = childStr(root, "name");
        if (out.name.empty()) { err = "missing 'name'"; return false; }
        out.isFlags = (childStr(root, "flags") == "true");

        if (hasKey(root, "items")) {
            for (auto it : root[c4::to_csubstr("items")]) {
                EnumItem e;
                e.name = childStr(it, "name");
                if (e.name.empty()) { err = out.name + ": an item is missing 'name'"; return false; }
                const std::string v = childStr(it, "value");
                if (v.empty()) { err = out.name + "." + e.name + ": missing 'value'"; return false; }
                // Base 10 (signed): values are plain decimals incl. negatives (e.g. -1). NOT base 0,
                // whose octal-on-leading-zero would silently reinterpret a value (Havok never means octal).
                e.value = std::stol(v, nullptr, 10);
                out.items.push_back(std::move(e));
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("parse: ") + e.what();
        return false;
    }
}

bool SchemaRegistry::LoadDir(const std::string& root, std::string& err) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        const fs::path p = it->path();
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".yaml" && ext != ".yml") continue;
        // The schema VERSION stamp (Havok/SCHEMA.yaml) is NOT a class descriptor — it carries the
        // one `schema_version:` for the whole tree (the editor <-> compiler contract stamp). Read it
        // into m_schemaVersion and move on; never ParseSchema it (it has no `name:`/`fields:`, which
        // would otherwise fail the whole load). Recognized by stem, case-insensitively.
        {
            std::string stem = p.stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (stem == "schema") {
                // The file EXISTS — record that, so a present-but-unreadable stamp (parse failure or
                // a missing schema_version key, both of which leave m_schemaVersion empty below) is
                // distinguishable from a truly unstamped tree and can be refused, not trusted.
                m_schemaStampPresent = true;
                std::ifstream vf(p, std::ios::binary);
                std::stringstream vss; vss << vf.rdbuf();
                std::string vtext = vss.str();
                try {
                    c4::yml::Tree vt = c4::yml::parse_in_place(c4::to_substr(vtext));
                    m_schemaVersion  = childStr(vt.rootref(), "schema_version");
                } catch (const std::exception&) { /* leave version empty; present flag still set */ }
                continue;
            }
        }
        // The enums/ subtree carries Havok enum DEFINITIONS (name + (name,value) items), not classes.
        // Route them to ParseEnumDef into m_enums; never ParseSchema them (no fields:, would fail the
        // whole load). They are the single source for both .hky name rendering and the signature CRC.
        if (p.parent_path().filename() == "enums") {
            std::ifstream ef(p, std::ios::binary);
            std::stringstream ess; ess << ef.rdbuf();
            EnumDef ed; std::string eerr;
            if (!ParseEnumDef(ess.str(), ed, eerr)) { err = p.string() + ": " + eerr; return false; }
            m_enums[ed.name] = std::move(ed);
            continue;
        }
        // Skip the metadata/ tree: those are text-format cache descriptors (kind: metadata —
        // animationdata/setdata), NOT Havok classes. The class-schema loader only consumes Havok
        // class descriptors; a metadata schema's vocabulary (recordarray/when/header) is not a class type.
        if (p.parent_path().filename() == "metadata") continue;
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        ClassSchema cs; std::string perr;
        if (!ParseSchema(ss.str(), cs, perr)) { err = p.string() + ": " + perr; return false; }
        m_byName[cs.name] = std::move(cs);
    }

    // DERIVE-AND-ASSERT object size. ComputeSize (from the field layout) is the authority the
    // serializer already uses everywhere; the authored `size:` is only a cross-check. Enforce it once
    // the whole registry is loaded (ComputeSize recurses parent/struct refs) so a field-width or layout
    // error — or a foreign/corrupt schema — is refused HERE, not silently served. `size:` is OPTIONAL:
    // a class may omit it (size == 0) and rely wholly on the derived size.
    for (const auto& [name, cs] : m_byName) {
        if (cs.size <= 0) continue;               // omitted → derived-only, nothing to assert against
        std::string serr;
        const int computed = ComputeSize(name, &serr);
        if (computed != cs.size) {
            err = "class '" + name + "': declared size " + std::to_string(cs.size) + " != derived "
                  + std::to_string(computed) + (serr.empty() ? std::string{} : " (" + serr + ")");
            return false;
        }
    }
    return true;
}

const ClassSchema* SchemaRegistry::Find(const std::string& name) const {
    auto it = m_byName.find(name);
    return it == m_byName.end() ? nullptr : &it->second;
}

const EnumDef* SchemaRegistry::FindEnum(const std::string& name) const {
    auto it = m_enums.find(name);
    return it == m_enums.end() ? nullptr : &it->second;
}

std::string SchemaRegistry::MergeTag(const std::string& className, const std::string& field) const {
    const ClassSchema* cs = Find(className);
    if (!cs) return {};
    for (const Field& f : cs->fields)
        if (f.name == field) return f.merge;
    return {};
}

int SchemaRegistry::ComputeSize(const std::string& className, std::string* err, int depth) const {
    if (depth > 256) { if (err) *err = "schema recursion too deep (cyclic parent/struct ref?) at '" + className + "'"; return -1; }
    const ClassSchema* cs = Find(className);
    if (!cs) { if (err) *err = "unresolved class '" + className + "'"; return -1; }
    int cursor = 0;
    if (!cs->parent.empty()) {
        cursor = ComputeSize(cs->parent, err, depth + 1);
        if (cursor < 0) return -1;
    }
    for (const Field& f : cs->fields) {
        const int n = f.count > 0 ? f.count : 1;   // fixed inline repeat (std::array<T,N>); default 1
        switch (f.kind) {
            case FieldKind::Vtable:     cursor += 8; break;
            case FieldKind::Scalar:     cursor += ScalarWidth(f.scalar) * n; break;
            case FieldKind::String:     cursor += 8; break;
            case FieldKind::CString:    cursor += 8; break;
            case FieldKind::Quaternion: cursor += 16 * n; break;
            case FieldKind::Ptr:        cursor += 8 * n; break;
            case FieldKind::EmptyPtr:   cursor += 8; break;
            case FieldKind::StructArray:
            case FieldKind::PtrArray:
            case FieldKind::Vec4Array:
            case FieldKind::QsTransformArray:
            case FieldKind::StringArray:
            case FieldKind::ScalarArray:
            case FieldKind::EmptyArray: cursor += 16; break;
            case FieldKind::Vector4:    cursor += 16 * n; break;
            case FieldKind::QsTransform: cursor += 48 * n; break;
            case FieldKind::BoolArray:  cursor += f.count; break;
            case FieldKind::Struct: {
                const int inner = ComputeSize(f.ref, err, depth + 1);
                if (inner < 0) return -1;
                cursor += inner;
                break;
            }
            case FieldKind::Pad:  cursor = alignUp(cursor, f.count); break;
            case FieldKind::Skip: cursor += f.count; break;
        }
    }
    return cursor;
}

} // namespace havok::schema
