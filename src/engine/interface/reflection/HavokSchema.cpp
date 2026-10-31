#include <interface/reflection/HavokSchema.h>

#include <codec/resource/PathKey.h>   // path identity — schema file discovery (extension / stamp stem)
#include <RymlInclude.h>   // the ONLY sanctioned way to pull in rapidyaml (C++20+ shim)
#include <common/miniz.h>
#include <disk/Disk.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace CB::engine::schema {

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
        out.unwrap  = childStr(root, "unwrap");
        out.vocab   = childStr(root, "vocab");
        out.factory = childStr(root, "factory");
        out.metadata        = childStr(root, "kind") == "metadata";
        out.blankTerminated = childStr(root, "terminator") == "blank";
        { const std::string s = childStr(root, "framelimit"); if (!s.empty()) out.frameLimit = std::stoi(s); }
        { const std::string s = childStr(root, "interfaces"); if (!s.empty()) out.interfaces = std::stoi(s); }
        if (hasKey(root, "enums"))
            for (auto en : root[c4::to_csubstr("enums")])
                if (en.has_val()) out.enums.push_back(toStr(en.val()));
        if (hasKey(root, "view")) {
            const auto v = root[c4::to_csubstr("view")];
            if (!v.is_map()) { err = "'view' is not a map"; return false; }
            for (auto r : v) {
                if (!r.has_val()) { err = "'view' holds a non-scalar"; return false; }
                const std::string key = toStr(r.key()), val = toStr(r.val());
                if (key == "kind") out.view.kind = val;
                else out.view.roles[key] = val;
            }
            if (out.view.kind.empty()) { err = "'view' has no kind"; return false; }
        }

        if (hasKey(root, "fields")) {
            for (auto fn : root[c4::to_csubstr("fields")]) {
                Field f;
                if (hasKey(fn, "vtable")) {
                    f.kind = FieldKind::Vtable;
                } else if (hasKey(fn, "empty")) {
                    f.kind = (childStr(fn, "empty") == "ptr") ? FieldKind::EmptyPtr : FieldKind::EmptyArray;
                    f.name = childStr(fn, "name");   ///< @note The reflected member's name (signature only); may be absent.
                    f.hkType = childStr(fn, "hktype");
                    if (!f.hkType.empty() && !(f.kind == FieldKind::EmptyPtr && f.hkType == "stringptr")) {
                        err = out.name + "." + f.name + ": an empty slot's hktype can only be 'stringptr', on `empty: ptr`"; return false;
                    }
                } else if (hasKey(fn, "pad")) {
                    f.kind = FieldKind::Pad;  f.count = std::stoi(childStr(fn, "pad"));
                } else if (hasKey(fn, "skip")) {
                    f.kind = FieldKind::Skip; f.count = std::stoi(childStr(fn, "skip"));
                } else if (hasKey(fn, "name")) {
                    f.name = childStr(fn, "name");
                    const std::string t = childStr(fn, "type");
                    /// @note A metadata record's scalars are text lines carried verbatim; its record lists are object arrays.
                    if (out.metadata && (t == "string" || t == "int" || t == "float" || t == "uint32" || t == "bool"))
                        f.kind = FieldKind::Text;
                    else if (t == "recordarray") { f.kind = FieldKind::PtrArray; f.ref = childStr(fn, "ref"); }
                    else if (t == "string")      f.kind = FieldKind::String;
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
                    if (const std::string h = childStr(fn, "hkyenum"); !h.empty()) { f.enumName = h; f.enumHkyOnly = true; }
                    f.boneRef  = (childStr(fn, "boneref")  == "true");
                    f.boneList = (childStr(fn, "bonelist") == "true");
                    f.boneSet  = (childStr(fn, "boneset")  == "true");
                    f.boneOf   = childStr(fn, "boneof");
                    f.charPropIf = childStr(fn, "charpropif");
                    f.roster     = childStr(fn, "roster");
                    if (const std::string bw = childStr(fn, "boneweights"); !bw.empty()) { f.boneWeights = true; f.boneWeightDefault = bw; }
                    f.isFlags  = (childStr(fn, "flags") == "true");   // bitfield → FormatFlags in .hky
                    f.hkyEmit  = (childStr(fn, "hky")   == "true");   // force-emit an ignored field in .hky
                    f.eventRef  = childStr(fn, "eventref");           // event-index companion key
                    f.varRef    = childStr(fn, "varref");             // variable-index companion key
                    f.header       = childStr(fn, "header");
                    f.when         = childStr(fn, "when");
                    f.frame        = childStr(fn, "frame");
                    f.frameCounted = childStr(fn, "count") == "frame";
                    f.hkType       = childStr(fn, "hktype");
                    f.cbAdded      = childStr(fn, "cb") == "true";
                    if (hasKey(fn, "role")) {
                        auto r = fn[c4::to_csubstr("role")];
                        if (!r.is_map()) { err = out.name + "." + f.name + ": `role` must be a map ({ role: …, flags: […] })"; return false; }
                        f.roleName = childStr(r, "role");
                        if (hasKey(r, "flags")) {
                            auto fl = r[c4::to_csubstr("flags")];
                            if (!fl.is_seq()) { err = out.name + "." + f.name + ": `role.flags` must be a list"; return false; }
                            for (auto x : fl)
                                if (x.has_val()) f.roleFlagNames.push_back(toStr(x.val()));
                        }
                    }
                    f.unitRef      = childStr(fn, "unitref");
                    if (!f.unitRef.empty()) {
                        if (f.unitRef != "behavior" && f.unitRef != "character" && f.unitRef != "skeleton" && f.unitRef != "animation") {
                            err = out.name + "." + f.name + ": unknown unitref '" + f.unitRef + "' (behavior, character, skeleton or animation)"; return false;
                        }
                        if (f.kind != FieldKind::String && f.kind != FieldKind::CString && f.kind != FieldKind::StringArray) {
                            err = out.name + "." + f.name + ": unitref needs a string, cstring or stringarray field"; return false;
                        }
                    }
                    if (!f.hkType.empty() && f.hkType != "ulong" && f.hkType != "enum" && f.hkType != "flags") {
                        err = out.name + "." + f.name + ": unknown hktype '" + f.hkType + "' (ulong, enum or flags)"; return false;
                    }
                    // fixed inline repeat count (std::array<T,N>) for scalar/ptr/vector4 — boolarray set its own above
                    if (f.kind != FieldKind::BoolArray && !f.frameCounted) {
                        const std::string c = childStr(fn, "count");
                        if (!c.empty()) f.count = std::stoi(c);
                    }
                    if (const std::string u = childStr(fn, "user"); !u.empty()) {
                        if (f.kind != FieldKind::Scalar || f.count > 1 || f.scalar == Scalar::Float || f.scalar == Scalar::Half
                            || f.scalar == Scalar::Bool || !f.enumName.empty()) {
                            err = out.name + "." + f.name + ": `user` needs a single integer scalar"; return false;
                        }
                        char* end = nullptr;
                        f.userValue = std::strtoll(u.c_str(), &end, 10);
                        if (!end || *end) { err = out.name + "." + f.name + ": `user` value '" + u + "' is not an integer"; return false; }
                        f.user = true;
                    }
                    f.charProp = childStr(fn, "charprop");
                    if (!f.charProp.empty()) {
                        const bool ptr  = f.kind == FieldKind::Ptr && f.count <= 1 && f.charProp == f.ref;
                        const bool bone = f.kind == FieldKind::Scalar && f.count <= 1 && f.boneRef && f.charProp == "bone";
                        if (!ptr && !bone) {
                            err = out.name + "." + f.name + ": `charprop` names its pointer's own class, or `bone` on a boneref scalar";
                            return false;
                        }
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

bool DescriptorKind(const std::string& yamlText, std::string& out, std::string& err) {
    try {
        std::string   storage = yamlText;   // parse_in_place mutates the buffer
        c4::yml::Tree tree    = c4::yml::parse_in_place(c4::to_substr(storage));
        auto          root    = tree.rootref();
        if (!root.readable() || !root.is_map()) { err = "root is not a map"; return false; }
        out = childStr(root, "kind");
        return true;
    } catch (const std::exception& e) {
        err = std::string("parse: ") + e.what();
        return false;
    }
}

bool RuleSelects(const Field& f, const std::string& token) {
    if (token == "boneweights") return f.boneWeights;
    if (token == "bonelist")    return f.boneList;
    if (token == "boneref")     return f.boneRef;
    if (token == "user")        return f.user;
    if (token == "charprop")    return !f.charProp.empty();
    return false;
}

bool ParseRuleDef(const std::string& yamlText, RuleDef& out, std::string& err) {
    try {
        std::string   storage = yamlText;
        c4::yml::Tree tree    = c4::yml::parse_in_place(c4::to_substr(storage));
        auto          root    = tree.rootref();
        if (!root.readable() || !root.is_map()) { err = "root is not a map"; return false; }
        out      = RuleDef{};
        out.text = yamlText;
        out.name = childStr(root, "name");
        out.op   = childStr(root, "op");
        if (out.name.empty()) { err = "a rule needs a 'name'"; return false; }
        if (out.op.empty())   { err = "rule '" + out.name + "' names no 'op'"; return false; }
        const auto list = [&](auto n, const char* key, std::vector<std::string>& to) {
            if (!hasKey(n, key)) return true;
            auto l = n[c4::to_csubstr(key)];
            if (!l.is_seq()) { err = "rule '" + out.name + "': '" + key + "' must be a list"; return false; }
            for (auto e : l) {
                if (!e.has_val()) { err = "rule '" + out.name + "': '" + key + "' holds a non-scalar"; return false; }
                to.push_back(toStr(e.val()));
            }
            return true;
        };
        if (!list(root, "runs", out.runs) || !list(root, "select", out.select)) return false;
        if (hasKey(root, "order")) {
            auto o = root[c4::to_csubstr("order")];
            if (!o.is_map()) { err = "rule '" + out.name + "': 'order' must be a map ({ after: […], before: […] })"; return false; }
            if (!list(o, "after", out.after) || !list(o, "before", out.before)) return false;
        }
        if (out.runs.empty()) { err = "rule '" + out.name + "' runs nowhere (runs: [regen, converter])"; return false; }
        for (const auto& s : out.runs)
            if (s != "regen" && s != "converter") { err = "rule '" + out.name + "': unknown stage '" + s + "' (regen, converter)"; return false; }
        for (const auto& t : out.select)
            if (t != "boneweights" && t != "bonelist" && t != "boneref" && t != "user" && t != "charprop") {
                err = "rule '" + out.name + "': unknown select '" + t + "' (boneweights, bonelist, boneref, user, charprop)";
                return false;
            }
        return true;
    } catch (const std::exception& e) {
        err = std::string("parse: ") + e.what();
        return false;
    }
}

bool SchemaRegistry::LoadDir(const std::string& root, std::string& err) {
    std::error_code ec;
    /// @note Semantics are collected during the walk and applied after it: they name classes, so m_byName must be
    ///       complete first.
    std::vector<std::string> semanticsTexts;
    /// @note A descriptor's identity is its `name:` within its `kind:`; folders are decoration. A second file
    ///       claiming a loaded name fails the load, naming both files, instead of silently winning by walk order.
    std::map<std::string, fs::path> classFrom, enumFrom, shimFrom, embeddedFrom, ruleFrom;
    const auto claim = [&err](std::map<std::string, fs::path>& seen, const std::string& name, const fs::path& p,
                              const char* what) {
        const auto [it, fresh] = seen.emplace(name, p);
        if (!fresh) err = p.string() + ": " + what + " '" + name + "' is already defined by " + it->second.string();
        return fresh;
    };
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        const fs::path p = it->path();
        const std::string ext = codec::resource::PathKey(p.extension().string());
        if (ext != ".yaml" && ext != ".yml") continue;
        // The schema VERSION stamp (SCHEMA.yaml, generated by cmake/Schema.cmake) is NOT a class
        // descriptor — it carries the one `schema_version:` for the whole tree. Read it
        // into m_schemaVersion and move on; never ParseSchema it (it has no `name:`/`fields:`, which
        // would otherwise fail the whole load). Recognized by stem, case-insensitively.
        {
            if (codec::resource::PathKey(p.stem().string()) == "schema") {
                // The file EXISTS — record that, so a present-but-unreadable stamp (parse failure or
                // a missing schema_version key, both of which leave m_schemaVersion empty below) is
                // distinguishable from a truly unstamped tree and can be refused, not trusted.
                m_schemaStampPresent = true;
                std::string vtext = disk::ReadFile(p, { .stripBom = true }).value_or(std::string());
                try {
                    c4::yml::Tree vt = c4::yml::parse_in_place(c4::to_substr(vtext));
                    m_schemaVersion  = childStr(vt.rootref(), "schema_version");
                } catch (const std::exception&) { /* leave version empty; present flag still set */ }
                continue;
            }
        }
        const std::string text = disk::ReadFile(p, { .stripBom = true }).value_or(std::string());
        std::string kind, perr;
        if (!DescriptorKind(text, kind, perr)) { err = p.string() + ": " + perr; return false; }
        if (kind == "enum") {
            EnumDef ed;
            if (!ParseEnumDef(text, ed, perr)) { err = p.string() + ": " + perr; return false; }
            if (!claim(enumFrom, ed.name, p, "enum")) return false;
            m_enums[ed.name] = std::move(ed);
        } else if (kind == "semantics") {
            semanticsTexts.push_back(text);
        } else if (kind == "probe") {
            /// @note Compile-trace probes ride on the schema (they name classes and fields) but are not part of it:
            ///       CB::engine::compile::trace reads them itself.
            continue;
        } else if (kind == "compatshim") {
            /// @note A compatibility shim (schema/metadata/compatshim/<name>/) rides with the schema but is not part of
            ///       it: no class, no layout. Its text is kept by name for its own reader (CompatShim).
            std::string name;
            try {
                std::string copy = text;
                c4::yml::Tree st = c4::yml::parse_in_place(c4::to_substr(copy));
                name = childStr(st.rootref(), "name");
            } catch (const std::exception& e) { err = p.string() + ": parse: " + e.what(); return false; }
            if (name.empty()) { err = p.string() + ": compatshim without a 'name'"; return false; }
            if (!claim(shimFrom, name, p, "compatshim")) return false;
            m_compatShims[name] = text;
        } else if (kind == "embedded") {
            EmbeddedKind ek;
            try {
                std::string copy = text;
                c4::yml::Tree et = c4::yml::parse_in_place(c4::to_substr(copy));
                ek.name = childStr(et.rootref(), "name");
                ek.root = childStr(et.rootref(), "root");
                ek.cls  = childStr(et.rootref(), "class");
            } catch (const std::exception& e) { err = p.string() + ": parse: " + e.what(); return false; }
            if (ek.name.empty() || ek.root.empty() || ek.cls.empty()) {
                err = p.string() + ": an embedded kind needs 'name', 'root' and 'class'";
                return false;
            }
            if (!claim(embeddedFrom, ek.name, p, "embedded kind")) return false;
            m_embedded[ek.name] = std::move(ek);
        } else if (kind == "rule") {
            RuleDef rd;
            if (!ParseRuleDef(text, rd, perr)) { err = p.string() + ": " + perr; return false; }
            if (!claim(ruleFrom, rd.name, p, "rule")) return false;
            rd.file = p.string();
            m_rules[rd.name] = std::move(rd);
        } else if (kind.empty() || kind == "metadata") {
            /// @note `kind: metadata` = a Bethesda text-cache record (adsf/asdsf/speeddata) MetaText reads and writes:
            ///       no signature or size, so no packfile path ever meets it. It shares the class namespace.
            ClassSchema cs;
            if (!ParseSchema(text, cs, perr)) { err = p.string() + ": " + perr; return false; }
            if (!claim(classFrom, cs.name, p, "class")) return false;
            m_byName[cs.name] = std::move(cs);
        } else {
            err = p.string() + ": unknown kind '" + kind + "' (enum, semantics, probe, compatshim, embedded, rule, metadata, or none for a class)";
            return false;
        }
    }

    // Apply centralized merge semantics (metadata/semantics/*.yaml) onto the loaded classes — the
    // single auditable source for merge policy, resolved here into the per-field Field::merge the merge
    // engine already reads (so the runtime path is unchanged; this only relocates the SOURCE off the 200+
    // class files). Runs after the class walk so every class is present to assign to.
    for (const auto& stext : semanticsTexts)
        if (!ApplyMergeSemantics(stext, err)) return false;

    /// @note `role:` names resolve after the walk, when every enum is loaded: against the Role and RoleFlags enum
    ///       descriptors (hkbRoleAttribute's), matched exactly as Havok spells them. An unknown name fails the load.
    {
        const auto lookup = [this](const char* enumName, const std::string& item, long& out) {
            const EnumDef* e = FindEnum(enumName);
            if (!e) return false;
            for (const EnumItem& it : e->items)
                if (it.name == item) { out = it.value; return true; }
            return false;
        };
        for (auto& [name, cs] : m_byName) {
            for (Field& f : cs.fields) {
                long v = 0;
                if (!f.roleName.empty()) {
                    if (!lookup("Role", f.roleName, v)) { err = "class '" + name + "' field '" + f.name + "': unknown role '" + f.roleName + "' (an item of the Role enum)"; return false; }
                    f.role = static_cast<int>(v);
                }
                for (const std::string& flag : f.roleFlagNames) {
                    if (!lookup("RoleFlags", flag, v)) { err = "class '" + name + "' field '" + f.name + "': unknown role flag '" + flag + "' (an item of the RoleFlags enum)"; return false; }
                    f.roleFlags |= static_cast<int>(v);
                }
            }
        }
    }

    /// @note A `view:` is checked against the views editors know, each role naming one of the class's own fields, so a
    ///       misspelt role or field fails here instead of drawing nothing; an embedded kind must name a loaded class.
    {
        static const std::map<std::string, std::vector<std::string>> kViewRoles{
            { "curve", { "keys", "label" } },
            { "curvekey", { "time", "value", "interp" } },
        };
        for (const auto& [name, cs] : m_byName) {
            if (cs.view.kind.empty()) continue;
            const auto known = kViewRoles.find(cs.view.kind);
            if (known == kViewRoles.end()) { err = "class '" + name + "': unknown view '" + cs.view.kind + "' (curve, curvekey)"; return false; }
            for (const auto& role : known->second)
                if (!cs.view.roles.count(role)) { err = "class '" + name + "': view '" + cs.view.kind + "' has no '" + role + "'"; return false; }
            for (const auto& [role, field] : cs.view.roles) {
                if (std::find(known->second.begin(), known->second.end(), role) == known->second.end()) {
                    err = "class '" + name + "': view '" + cs.view.kind + "' has no role '" + role + "'";
                    return false;
                }
                const bool own = std::any_of(cs.fields.begin(), cs.fields.end(), [&](const Field& f) { return f.name == field; });
                if (!own) { err = "class '" + name + "': view role '" + role + "' names no field '" + field + "'"; return false; }
            }
        }
        for (const auto& [name, ek] : m_embedded)
            if (!m_byName.count(ek.cls)) { err = "embedded kind '" + name + "': no class '" + ek.cls + "'"; return false; }
        for (const auto& [name, rd] : m_rules)
            for (const auto* l : { &rd.after, &rd.before })
                for (const auto& other : *l)
                    if (!m_rules.count(other)) { err = rd.file + ": rule '" + name + "' orders against no rule '" + other + "'"; return false; }
    }

    /// @note A `factory:` name maps to exactly one class, or a node's class could not be told from its factory name.
    ///       Compared byte-exact: the engine's factories are keyed by exact name.
    {
        std::map<std::string, std::string> factoryClass;
        for (const auto& [name, cs] : m_byName) {
            if (cs.factory.empty()) continue;
            const auto [it, fresh] = factoryClass.emplace(cs.factory, name);
            if (!fresh) {
                err = "class '" + name + "': factory '" + cs.factory + "' is already claimed by class '" + it->second + "'";
                return false;
            }
        }
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

    /// @note A vanilla class CB extends (`cb: true` members) stores the exe's signature. Without CB's members its
    ///       descriptor must reproduce it, which proves it carries the exe's whole reflection; the class's signature
    ///       then becomes the one with them, which CB registers at runtime and writes into every packfile.
    {
        struct Extended { std::string name; std::uint32_t vanilla, full; };
        std::vector<Extended> extended;
        for (const auto& [name, cs] : m_byName) {
            if (cs.metadata || std::none_of(cs.fields.begin(), cs.fields.end(), [](const Field& f) { return f.cbAdded; })) continue;
            std::uint32_t vanilla = 0, full = 0;
            std::string   serr;
            if (!ComputeSignature(name, vanilla, &serr, true)) { err = "class '" + name + "': no vanilla signature: " + serr; return false; }
            if (vanilla != cs.signature) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "without its cb members computes 0x%08x, the exe's is 0x%08x", vanilla, cs.signature);
                err = "class '" + name + "' " + buf + ": its descriptor does not carry the exe's reflection";
                return false;
            }
            if (!ComputeSignature(name, full, &serr)) { err = "class '" + name + "': no signature: " + serr; return false; }
            extended.push_back({ name, vanilla, full });
        }
        for (const auto& e : extended) {
            m_byName[e.name].vanillaSignature = e.vanilla;
            m_byName[e.name].signature        = e.full;
        }
    }

    /// @note A CB class (CB prefix) stores no signature: it gets the one the engine derives. The behavior loader
    ///       recomputes every class's signature from the registered hkClass and refuses a file whose stored value
    ///       differs, and the project's remaining behaviors stop loading with it (2026-10-20: CBPoseSearchGenerator
    ///       written as 0 cut every attack behavior). So no CB class loads without one.
    std::vector<std::pair<std::string, std::uint32_t>> derived;
    for (const auto& [name, cs] : m_byName) {
        if (cs.metadata || cs.signature != 0 || name.rfind("CB", 0) != 0) continue;
        std::uint32_t sig = 0;
        std::string   serr;
        if (!ComputeSignature(name, sig, &serr)) { err = "class '" + name + "': no signature: " + serr; return false; }
        derived.emplace_back(name, sig);
    }
    for (const auto& [name, sig] : derived) m_byName[name].signature = sig;

    BuildLayouts();
    return true;
}

const ClassSchema* SchemaRegistry::Find(const std::string& name) const {
    auto it = m_byName.find(name);
    return it == m_byName.end() ? nullptr : &it->second;
}

bool SchemaRegistry::Derives(const std::string& cls, const std::string& base) const {
    // the depth bound only guards a cyclic `parent:` chain, which Flatten refuses
    int depth = 0;
    for (const ClassSchema* c = Find(cls); c && depth < 64; c = c->parent.empty() ? nullptr : Find(c->parent), ++depth)
        if (c->name == base) return true;
    return false;
}

namespace {
/// @note `depth` bounds the parent chain so a cyclic `parent:` (A->B->A) throws instead of overflowing.
void flattenInto(const SchemaRegistry& reg, const ClassSchema* cs, std::vector<const Field*>& out, int depth) {
    if (!cs) return;
    if (depth > 256) throw std::runtime_error("havok-io: schema parent chain too deep (cycle?) at '" + cs->name + "'");
    if (!cs->parent.empty()) {
        const ClassSchema* p = reg.Find(cs->parent);
        if (!p) throw std::runtime_error("havok-io: unresolved parent '" + cs->parent + "' of '" + cs->name + "'");
        flattenInto(reg, p, out, depth + 1);
    }
    for (const Field& f : cs->fields) out.push_back(&f);
}
}  // namespace

std::vector<const Field*> SchemaRegistry::Flatten(const ClassSchema* cs) const {
    std::vector<const Field*> out;
    flattenInto(*this, cs, out, 0);
    return out;
}

const FieldLayout* SchemaRegistry::Layout(const ClassSchema* cs) const {
    const auto it = m_layouts.find(cs);
    return it == m_layouts.end() ? nullptr : &it->second;
}

void SchemaRegistry::BuildLayouts() {
    m_layouts.clear();
    for (const auto& [name, cs] : m_byName) {
        FieldLayout l;
        try { l.fields = Flatten(&cs); }
        catch (const std::exception&) { continue; }   // left uncached: Flatten reports it at the use site
        l.index.reserve(l.fields.size());
        for (std::size_t i = 0; i < l.fields.size(); ++i) l.index.emplace(l.fields[i]->name, i);
        m_layouts.emplace(&cs, std::move(l));
    }
}

const EnumDef* SchemaRegistry::FindEnum(const std::string& name) const {
    auto it = m_enums.find(name);
    return it == m_enums.end() ? nullptr : &it->second;
}

const std::string* SchemaRegistry::CompatShim(const std::string& name) const {
    auto it = m_compatShims.find(name);
    return it == m_compatShims.end() ? nullptr : &it->second;
}

std::string SchemaRegistry::MergeTag(const std::string& className, const std::string& field) const {
    const ClassSchema* cs = Find(className);
    if (!cs) return {};
    for (const Field& f : cs->fields)
        if (f.name == field) return f.merge;
    return {};
}

bool SchemaRegistry::ApplyMergeSemantics(const std::string& yamlText, std::string& err) {
    std::string text = yamlText;   // parse_in_place mutates its buffer
    c4::yml::Tree tree;
    try { tree = c4::yml::parse_in_place(c4::to_substr(text)); }
    catch (const std::exception& e) { err = std::string("semantics parse: ") + e.what(); return false; }
    auto root = tree.rootref();
    if (!root.is_map()) return true;   // empty / commented-out file → nothing to apply

    // Collect, then apply class-level first and field-level second, so "<Class>.<field>" overrides the
    // whole-class "<Class>" default no matter the listing order.
    struct Assign { std::string cls, field, strategy; };
    std::vector<Assign> classLevel, fieldLevel;
    for (auto cat : root) {
        if (!cat.has_key() || !cat.is_seq()) continue;
        std::string strategy; c4::from_chars(cat.key(), &strategy);   // "compose" / "guarded" / "replace"
        for (auto e : cat) {
            if (!e.has_val()) continue;
            std::string entry; c4::from_chars(e.val(), &entry);
            const auto dot = entry.find('.');
            if (dot == std::string::npos) classLevel.push_back({ entry, {}, strategy });
            else                          fieldLevel.push_back({ entry.substr(0, dot), entry.substr(dot + 1), strategy });
        }
    }
    for (const auto& a : classLevel) {
        auto it = m_byName.find(a.cls);
        if (it == m_byName.end()) { err = "semantics: unknown class '" + a.cls + "'"; return false; }
        for (Field& f : it->second.fields) f.merge = a.strategy;
    }
    for (const auto& a : fieldLevel) {
        auto it = m_byName.find(a.cls);
        if (it == m_byName.end()) { err = "semantics: unknown class '" + a.cls + "'"; return false; }
        bool found = false;
        for (Field& f : it->second.fields) if (f.name == a.field) { f.merge = a.strategy; found = true; break; }
        if (!found) { err = "semantics: class '" + a.cls + "' has no field '" + a.field + "'"; return false; }
    }
    return true;
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
    for (const Field& f : cs->fields)
        if (!AdvanceField(f, cursor, err, depth)) return -1;
    return cursor;
}

std::vector<int> SchemaRegistry::OwnFieldOffsets(const std::string& className, std::string* err) const {
    const ClassSchema* cs = Find(className);
    if (!cs) { if (err) *err = "unresolved class '" + className + "'"; return {}; }
    int cursor = 0;
    if (!cs->parent.empty()) {
        cursor = ComputeSize(cs->parent, err);
        if (cursor < 0) return {};
    }
    std::vector<int> offsets;
    offsets.reserve(cs->fields.size());
    for (const Field& f : cs->fields) {
        if (f.kind == FieldKind::Pad) cursor = alignUp(cursor, f.count);
        offsets.push_back(cursor);
        if (f.kind != FieldKind::Pad && !AdvanceField(f, cursor, err, 0)) return {};
    }
    return offsets;
}

namespace {

/// @brief hkClassMember::Type ordinals (Havok 2010.2).
enum : std::uint8_t {
    kTypeBool = 1, kTypeInt8 = 3, kTypeUInt8 = 4, kTypeInt16 = 5, kTypeUInt16 = 6, kTypeInt32 = 7, kTypeUInt32 = 8,
    kTypeInt64 = 9, kTypeUInt64 = 10, kTypeReal = 11, kTypeVector4 = 12, kTypeQuaternion = 13, kTypeQsTransform = 16,
    kTypeZero = 19, kTypePointer = 20, kTypeArray = 22, kTypeEnum = 24, kTypeStruct = 25, kTypeCString = 29,
    kTypeULong = 30, kTypeFlags = 31, kTypeHalf = 32, kTypeStringPtr = 33,
};
constexpr std::uint16_t kSerializeIgnored = 0x400;

std::uint8_t ScalarType(Scalar s) {
    switch (s) {
        case Scalar::Int8:   return kTypeInt8;
        case Scalar::Byte:   return kTypeUInt8;
        case Scalar::Int16:  return kTypeInt16;
        case Scalar::UInt16: return kTypeUInt16;
        case Scalar::Int32:  return kTypeInt32;
        case Scalar::UInt32: return kTypeUInt32;
        case Scalar::Int64:  return kTypeInt64;
        case Scalar::UInt64: return kTypeUInt64;
        case Scalar::Float:  return kTypeReal;
        case Scalar::Half:   return kTypeHalf;
        case Scalar::Bool:   return kTypeBool;
    }
    return 0;
}

/// @brief One field as the hkClassMember the engine hashes.
struct ReflectedMember {
    std::uint8_t   type = 0, subtype = 0;
    std::int16_t   cArraySize = 0;
    std::uint16_t  flags = 0;
    std::string    structClass;    ///< an inline struct (or array of them): its class chain is hashed before the name
    std::string    enumName;       ///< the member's enum, hashed before the name
};

/// @return false when `f` is no member at all (vtable slot, padding, skip, metadata text).
bool Reflect(const Field& f, ReflectedMember& m) {
    m = ReflectedMember{};
    m.flags = f.ignored ? kSerializeIgnored : 0;
    if (f.count > 1) m.cArraySize = static_cast<std::int16_t>(f.count);
    switch (f.kind) {
        case FieldKind::Scalar:
            m.type = ScalarType(f.scalar);
            if (!f.enumName.empty() && !f.enumHkyOnly) { m.subtype = m.type; m.type = f.isFlags ? kTypeFlags : kTypeEnum; m.enumName = f.enumName; }
            break;
        case FieldKind::String:           m.type = kTypeStringPtr; break;
        case FieldKind::CString:          m.type = kTypeCString; break;
        case FieldKind::Quaternion:       m.type = kTypeQuaternion; break;
        case FieldKind::Ptr:              m.type = kTypePointer; m.subtype = kTypeStruct; break;
        case FieldKind::PtrArray:         m.type = kTypeArray; m.subtype = kTypePointer; break;
        case FieldKind::StructArray:      m.type = kTypeArray; m.subtype = kTypeStruct; m.structClass = f.ref; break;
        case FieldKind::Struct:           m.type = kTypeStruct; m.structClass = f.ref; break;
        case FieldKind::Vector4:          m.type = kTypeVector4; break;
        case FieldKind::QsTransform:      m.type = kTypeQsTransform; break;
        case FieldKind::Vec4Array:        m.type = kTypeArray; m.subtype = kTypeVector4; break;
        case FieldKind::QsTransformArray: m.type = kTypeArray; m.subtype = kTypeQsTransform; break;
        case FieldKind::StringArray:      m.type = kTypeArray; m.subtype = kTypeStringPtr; break;
        case FieldKind::ScalarArray:      m.type = kTypeArray; m.subtype = ScalarType(f.scalar); break;
        case FieldKind::BoolArray:        m.type = kTypeBool; m.cArraySize = static_cast<std::int16_t>(f.count); break;
        case FieldKind::EmptyArray:       m.type = kTypeArray; m.flags = kSerializeIgnored; break;
        case FieldKind::EmptyPtr:         m.type = f.hkType == "stringptr" ? kTypeStringPtr : kTypePointer; m.flags = kSerializeIgnored; break;
        default:                          return false;
    }
    if (f.hkType == "ulong") { m.type = kTypeULong; m.subtype = 0; }
    else if (f.hkType == "enum" || f.hkType == "flags") {
        if (m.type != kTypeEnum && m.type != kTypeFlags) m.subtype = m.type;
        m.type = f.hkType == "enum" ? kTypeEnum : kTypeFlags;
    }
    return true;
}

/// @brief hkClassMember's per-type storage size (the TypeProperties table), for the scalar storage an ENUM/FLAGS
///        member's subtype names.
std::uint16_t StorageSize(std::uint8_t type) {
    switch (type) {
        case kTypeBool: case 2: case kTypeInt8: case kTypeUInt8: return 1;
        case kTypeInt16: case kTypeUInt16: case kTypeHalf: case kTypeFlags: return 2;
        case kTypeInt32: case kTypeUInt32: case kTypeReal: return 4;
        case kTypeInt64: case kTypeUInt64: case kTypeULong: return 8;
        default: return 0xffff;
    }
}

void PutU16(std::string& out, std::uint16_t v) { out.push_back(static_cast<char>(v & 0xff)); out.push_back(static_cast<char>(v >> 8)); }
void PutU32(std::string& out, std::uint32_t v) { PutU16(out, static_cast<std::uint16_t>(v)); PutU16(out, static_cast<std::uint16_t>(v >> 16)); }

void EmitEnum(const EnumDef& en, std::string& out) {
    out += en.name;
    for (const EnumItem& it : en.items) { out += it.name; PutU32(out, static_cast<std::uint32_t>(it.value)); }
    PutU32(out, static_cast<std::uint32_t>(en.items.size()));
}

}  // namespace

bool SchemaRegistry::EmitSignatureOwn(const ClassSchema& cs, std::string& out, std::vector<const ClassSchema*>& touched,
                                      std::string* err, int depth, bool vanilla) const {
    PutU32(out, static_cast<std::uint32_t>(cs.interfaces));
    for (const std::string& name : cs.enums) {
        const EnumDef* en = FindEnum(name);
        if (!en) { if (err) *err = "'" + cs.name + "' declares unknown enum '" + name + "'"; return false; }
        EmitEnum(*en, out);
    }
    PutU32(out, static_cast<std::uint32_t>(cs.enums.size()));
    std::uint32_t members = 0;
    for (const Field& f : cs.fields) {
        ReflectedMember m;
        if ((vanilla && f.cbAdded) || !Reflect(f, m)) continue;
        if (f.name.empty()) { if (err) *err = "'" + cs.name + "' has an unnamed reflected member (give it a `name:`)"; return false; }
        ++members;
        if (!m.structClass.empty()) {
            const ClassSchema* sc = Find(m.structClass);
            if (!sc) { if (err) *err = "'" + cs.name + "." + f.name + "' references unknown class '" + m.structClass + "'"; return false; }
            if (!EmitSignatureChain(sc, out, touched, err, depth + 1, vanilla)) return false;
        }
        if (!m.enumName.empty()) {
            const EnumDef* en = FindEnum(m.enumName);
            if (!en) { if (err) *err = "'" + cs.name + "." + f.name + "' references unknown enum '" + m.enumName + "'"; return false; }
            EmitEnum(*en, out);
        }
        std::uint16_t typeWord = m.type, subWord = m.subtype, extra = 0, flagsWord = m.flags;
        if (m.type == kTypeEnum || m.type == kTypeFlags) {
            flagsWord = static_cast<std::uint16_t>(flagsWord ^ (StorageSize(m.subtype) << 3));
            subWord   = 0;
        }
        if (flagsWord & kSerializeIgnored) {
            flagsWord ^= kSerializeIgnored;
            extra    = subWord;
            typeWord = kTypeZero;
            subWord  = m.type;
        }
        out += f.name;
        PutU16(out, typeWord);
        PutU16(out, subWord);
        if (extra) PutU16(out, extra);
        PutU16(out, static_cast<std::uint16_t>(m.cArraySize));
        PutU16(out, flagsWord);
    }
    PutU32(out, members);
    return true;
}

bool SchemaRegistry::EmitSignatureChain(const ClassSchema* cs, std::string& out, std::vector<const ClassSchema*>& touched,
                                        std::string* err, int depth, bool vanilla) const {
    for (; cs; ++depth) {
        if (depth > 256) { if (err) *err = "signature recursion too deep (cyclic parent/struct ref?) at '" + cs->name + "'"; return false; }
        touched.push_back(cs);
        if (!EmitSignatureOwn(*cs, out, touched, err, depth, vanilla)) return false;
        if (cs->parent.empty()) break;
        const ClassSchema* parent = Find(cs->parent);
        if (!parent) { if (err) *err = "unresolved parent '" + cs->parent + "' of '" + cs->name + "'"; return false; }
        cs = parent;
    }
    return true;
}

bool SchemaRegistry::ComputeSignature(const std::string& className, std::uint32_t& out, std::string* err, bool vanilla) const {
    const ClassSchema* cs = Find(className);
    if (!cs) { if (err) *err = "unresolved class '" + className + "'"; return false; }
    std::string                     bytes;
    std::vector<const ClassSchema*> touched;
    if (!EmitSignatureChain(cs, bytes, touched, err, 0, vanilla)) return false;
    out = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
    for (const ClassSchema* t : touched) {
        /// @note A touched class is checked against what it stores for this computation: its vanilla signature when
        ///       CB's members are left out, else its full one.
        const std::uint32_t stored = (vanilla && t->vanillaSignature) ? t->vanillaSignature : t->signature;
        if (t == cs || stored == 0) continue;
        std::string                     tb;
        std::vector<const ClassSchema*> tt;
        if (!EmitSignatureChain(t, tb, tt, err, 0, vanilla)) return false;
        const auto ts = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(tb.data()), tb.size()));
        if (ts != stored) {
            if (err) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "computes 0x%08x, stores 0x%08x", ts, stored);
                *err = "'" + t->name + "' (in '" + className + "''s signature) " + buf + ": its descriptor does not carry its full reflection";
            }
            return false;
        }
    }
    return true;
}

bool SchemaRegistry::AdvanceField(const Field& f, int& cursor, std::string* err, int depth) const {
    {
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
                if (inner < 0) return false;
                cursor += inner;
                break;
            }
            case FieldKind::Pad:  cursor = alignUp(cursor, f.count); break;
            case FieldKind::Skip: cursor += f.count; break;
            default:              break;
        }
    }
    return true;
}

} // namespace CB::engine::schema
