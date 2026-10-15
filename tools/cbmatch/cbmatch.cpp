/// @file
/// @brief cbmatch — the byte-exact matching gate for CB's reimplementation of the engine.
/// @note Compiles each reimplemented function with the toolchains Skyrim shipped with, then diffs its bytes against the
///       function in the game exe, relocated fields masked. A function is EXACT (identical), REGALLOC (identical but for
///       register choice) or STRUCTURAL. Function bounds come from the exe's .pdata, so no disassembler database is
///       needed at match time.

#include <RymlInclude.h>
#include <disk/Disk.h>
#include <exec/Parallel.h>
#include <text/Text.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

#include <Zydis/Zydis.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<std::uint8_t>;

template <class T>
T rd(const Bytes& b, std::size_t off)
{
    if (off + sizeof(T) > b.size()) throw std::runtime_error("read past end of file");
    T v;
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

std::string hex(std::uint64_t v)
{
    char b[24];
    std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v));
    return b;
}

// ── manifest ─────────────────────────────────────────────────────────────────────────────────────────────────────

struct FunctionSpec {
    std::string                          name;   ///< undecorated (`Class::method`) or the mangled symbol
    std::map<std::string, std::uint64_t> va;     ///< runtime → absolute address in that runtime's exe
};

struct UnitSpec {
    fs::path                  source;
    std::vector<std::string>  flags;
    std::vector<std::string>  defines;
    std::vector<std::string>  toolchains;   ///< empty = every toolchain
    std::vector<FunctionSpec> functions;
};

struct RuntimeSpec {
    std::string name;
    fs::path    exe;
    fs::path    addressLibrary;   ///< the runtime's Address Library .bin; empty = no reference checking
    std::string pins;             ///< which engine.yaml pin column this runtime uses (se | ae | vr); defaults to `name`
};

struct Manifest {
    fs::path                                     dir;
    std::vector<RuntimeSpec>                     runtimes;     ///< in file order; the first is the default
    fs::path                                     registry;     ///< engine.yaml; empty = no reference checking
    std::vector<std::pair<std::string, fs::path>> toolchains;   ///< name → cl.exe, in file order
    std::vector<std::string>                     flags;
    std::vector<fs::path>                        includes;
    std::vector<UnitSpec>                        units;
};

[[noreturn]] void rymlThrowBasic(c4::csubstr msg, c4::yml::ErrorDataBasic const&, void*) { throw std::runtime_error(std::string(msg.str, msg.len)); }
[[noreturn]] void rymlThrowParse(c4::csubstr msg, c4::yml::ErrorDataParse const&, void*) { throw std::runtime_error(std::string(msg.str, msg.len)); }
[[noreturn]] void rymlThrowVisit(c4::csubstr msg, c4::yml::ErrorDataVisit const&, void*) { throw std::runtime_error(std::string(msg.str, msg.len)); }
[[noreturn]] void c4ThrowOnError(const char* msg, std::size_t sz) { throw std::runtime_error(std::string(msg, sz)); }

/// @brief ryml's default handler aborts the process; make a malformed manifest throw a reportable error instead.
void ensureRymlThrows()
{
    static const bool once = [] {
        c4::yml::Callbacks cb = c4::yml::get_callbacks();
        cb.m_error_basic = &rymlThrowBasic;
        cb.m_error_parse = &rymlThrowParse;
        cb.m_error_visit = &rymlThrowVisit;
        c4::yml::set_callbacks(cb);
        c4::set_error_flags(c4::ON_ERROR_CALLBACK);
        c4::set_error_callback(&c4ThrowOnError);
        return true;
    }();
    (void)once;
}

std::string str(c4::csubstr s) { return std::string(s.str, s.len); }

std::string val(const c4::yml::ConstNodeRef& n)
{
    if (!n.readable() || !n.has_val()) return {};
    return str(n.val());
}

bool has(const c4::yml::ConstNodeRef& n, const char* k) { return n.readable() && n.is_map() && n.has_child(c4::to_csubstr(k)); }

/// @brief `${NAME}` replaced by the environment variable, so a manifest can name machine-specific paths portably.
std::string expandEnv(std::string_view s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
            const auto close = s.find('}', i + 2);
            if (close == std::string_view::npos) throw std::runtime_error("unterminated ${ in '" + std::string(s) + "'");
            const std::string name(s.substr(i + 2, close - i - 2));
            const DWORD       n = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
            if (n == 0) throw std::runtime_error("environment variable " + name + " is not set");
            std::string v(n, '\0');
            v.resize(GetEnvironmentVariableA(name.c_str(), v.data(), n));
            out += v;
            i = close + 1;
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::vector<std::string> strSeq(const c4::yml::ConstNodeRef& parent, const char* key)
{
    std::vector<std::string> v;
    if (!has(parent, key)) return v;
    const auto n = parent[c4::to_csubstr(key)];
    if (n.is_seq()) for (const auto c : n.children()) v.push_back(expandEnv(val(c)));
    else if (n.has_val()) v.push_back(expandEnv(val(n)));
    return v;
}

std::vector<std::pair<std::string, fs::path>> namedPaths(const c4::yml::ConstNodeRef& parent, const char* key, const fs::path& base)
{
    std::vector<std::pair<std::string, fs::path>> v;
    if (!has(parent, key)) return v;
    for (const auto c : parent[c4::to_csubstr(key)].children()) {
        const fs::path p = expandEnv(val(c));
        v.emplace_back(str(c.key()), p.is_absolute() ? p : base / p);
    }
    return v;
}

std::uint64_t parseAddress(const std::string& s)
{
    std::size_t used = 0;
    const auto  v = std::stoull(s, &used, 0);
    if (used != s.size()) throw std::runtime_error("bad address '" + s + "'");
    return v;
}

Manifest loadManifest(const fs::path& path)
{
    auto text = CB::engine::disk::ReadFile(path, { .stripBom = true });
    if (!text) throw std::runtime_error("cannot read manifest " + path.string());
    ensureRymlThrows();
    const c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(*text));
    const auto          root = tree.crootref();

    Manifest m;
    m.dir = fs::absolute(path).parent_path();
    auto resolve = [&](const std::string& p) { const fs::path q = expandEnv(p); return q.is_absolute() ? q : m.dir / q; };
    if (has(root, "runtimes")) {
        for (const auto r : root["runtimes"].children()) {
            RuntimeSpec rt;
            rt.name = str(r.key());
            rt.pins = rt.name;
            if (r.is_map()) {
                rt.exe = resolve(val(r["exe"]));
                if (has(r, "addresslib")) rt.addressLibrary = resolve(val(r["addresslib"]));
                if (has(r, "pins")) rt.pins = val(r["pins"]);
            } else {
                rt.exe = resolve(val(r));
            }
            m.runtimes.push_back(std::move(rt));
        }
    }
    if (has(root, "registry")) m.registry = resolve(val(root["registry"]));
    m.toolchains = namedPaths(root, "toolchains", m.dir);
    m.flags      = strSeq(root, "flags");
    for (const auto& inc : strSeq(root, "includes")) m.includes.push_back(fs::path(inc).is_absolute() ? fs::path(inc) : m.dir / inc);

    if (!has(root, "units")) throw std::runtime_error("manifest has no units");
    for (const auto u : root["units"].children()) {
        UnitSpec unit;
        unit.source     = m.dir / expandEnv(val(u["source"]));
        unit.flags      = strSeq(u, "flags");
        unit.defines    = strSeq(u, "defines");
        unit.toolchains = strSeq(u, "toolchains");
        if (!has(u, "functions")) throw std::runtime_error("unit " + unit.source.string() + " lists no functions");
        for (const auto f : u["functions"].children()) {
            FunctionSpec fn;
            fn.name = val(f["name"]);
            if (fn.name.empty()) throw std::runtime_error("a function in " + unit.source.string() + " has no name");
            const auto va = f["va"];
            if (va.is_map()) {
                for (const auto r : va.children()) fn.va[str(r.key())] = parseAddress(val(r));
            } else {
                if (m.runtimes.empty()) throw std::runtime_error("a scalar va needs at least one runtime");
                fn.va[m.runtimes.front().name] = parseAddress(val(va));
            }
            unit.functions.push_back(std::move(fn));
        }
        m.units.push_back(std::move(unit));
    }
    if (m.toolchains.empty()) throw std::runtime_error("manifest names no toolchains");
    return m;
}

// ── the game exe ─────────────────────────────────────────────────────────────────────────────────────────────────

struct PeImage {
    struct Section { std::uint32_t va, vsize, raw, rawSize; };

    Bytes                                   bytes;
    std::uint64_t                           imageBase = 0;
    std::vector<Section>                    sections;
    std::uint32_t                           pdataRva = 0, pdataSize = 0;
    std::map<std::uint64_t, std::string>    importsBySlot;   ///< IAT slot address → imported function name

    std::optional<std::size_t> fileOffset(std::uint32_t rva, std::size_t n) const
    {
        for (const auto& s : sections) {
            const std::uint32_t span = std::max(s.vsize, s.rawSize);
            if (rva >= s.va && rva < s.va + span) {
                const std::size_t off = std::size_t(rva - s.va) + s.raw;
                if (rva - s.va + n > s.rawSize || off + n > bytes.size()) return std::nullopt;
                return off;
            }
        }
        return std::nullopt;
    }

    /// @brief The end RVA .pdata records for the function starting at `rva`, following chained unwind entries that
    ///        continue it; nullopt for a leaf function without an entry.
    std::optional<std::uint32_t> functionEnd(std::uint32_t rva) const
    {
        const std::size_t count = pdataSize / 12;
        const auto        base  = fileOffset(pdataRva, pdataSize);
        if (!base || count == 0) return std::nullopt;
        auto entry = [&](std::size_t i) {
            const std::size_t o = *base + i * 12;
            return std::array<std::uint32_t, 3>{ rd<std::uint32_t>(bytes, o), rd<std::uint32_t>(bytes, o + 4), rd<std::uint32_t>(bytes, o + 8) };
        };
        std::size_t lo = 0, hi = count;
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (entry(mid)[0] < rva) lo = mid + 1; else hi = mid;
        }
        if (lo >= count || entry(lo)[0] != rva) return std::nullopt;
        std::uint32_t end = entry(lo)[1];
        for (std::size_t i = lo + 1; i < count; ++i) {
            const auto e = entry(i);
            if (e[0] != end) break;
            const auto info = fileOffset(e[2] & ~1u, 1);
            if (!info || ((bytes[*info] >> 3) & UNW_FLAG_CHAININFO) == 0) break;
            end = e[1];
        }
        return end;
    }
};

PeImage loadPe(const fs::path& path)
{
    PeImage pe;
    auto    bytes = CB::engine::disk::ReadBytes(path);
    if (!bytes) throw std::runtime_error("cannot read exe " + path.string());
    pe.bytes = std::move(*bytes);
    const auto& b  = pe.bytes;
    const auto  nt = rd<std::uint32_t>(b, 0x3C);
    if (rd<std::uint32_t>(b, nt) != 0x00004550) throw std::runtime_error(path.string() + " is not a PE image");
    const auto nSections = rd<std::uint16_t>(b, nt + 6);
    const auto optSize   = rd<std::uint16_t>(b, nt + 20);
    const auto opt       = nt + 24;
    if (rd<std::uint16_t>(b, opt) != 0x20B) throw std::runtime_error(path.string() + " is not a PE32+ (x64) image");
    pe.imageBase   = rd<std::uint64_t>(b, opt + 24);
    const auto nDirs = rd<std::uint32_t>(b, opt + 108);
    if (nDirs > IMAGE_DIRECTORY_ENTRY_EXCEPTION) {
        pe.pdataRva  = rd<std::uint32_t>(b, opt + 112 + IMAGE_DIRECTORY_ENTRY_EXCEPTION * 8);
        pe.pdataSize = rd<std::uint32_t>(b, opt + 116 + IMAGE_DIRECTORY_ENTRY_EXCEPTION * 8);
    }
    for (std::size_t i = 0, sh = opt + optSize; i < nSections; ++i, sh += 40)
        pe.sections.push_back({ rd<std::uint32_t>(b, sh + 12), rd<std::uint32_t>(b, sh + 8), rd<std::uint32_t>(b, sh + 20), rd<std::uint32_t>(b, sh + 16) });

    // Import table: every IAT slot named by its import, so `call [__imp_X]` can be checked by name.
    if (nDirs > IMAGE_DIRECTORY_ENTRY_IMPORT) {
        const auto impRva = rd<std::uint32_t>(b, opt + 112 + IMAGE_DIRECTORY_ENTRY_IMPORT * 8);
        auto cstr = [&](std::uint32_t rva) -> std::string {
            const auto o = pe.fileOffset(rva, 1);
            if (!o) return {};
            const char* p = reinterpret_cast<const char*>(b.data() + *o);
            return std::string(p, strnlen(p, b.size() - *o));
        };
        for (std::uint32_t d = impRva; impRva != 0; d += 20) {
            const auto o = pe.fileOffset(d, 20);
            if (!o) break;
            const auto names = rd<std::uint32_t>(b, *o);
            const auto iat   = rd<std::uint32_t>(b, *o + 16);
            if (names == 0 && iat == 0) break;
            for (std::uint32_t k = 0;; ++k) {
                const auto t = pe.fileOffset((names ? names : iat) + k * 8, 8);
                if (!t) break;
                const auto thunk = rd<std::uint64_t>(b, *t);
                if (thunk == 0) break;
                if (!(thunk >> 63)) pe.importsBySlot[pe.imageBase + iat + k * 8] = cstr(static_cast<std::uint32_t>(thunk) + 2);
            }
        }
    }
    return pe;
}

// ── engine symbols: the Address Library + src/core/hooks/registry/engine.yaml ────────────────────────────────────

/// @brief Address Library id → image offset, decoded as CommonLibSSE-NG's REL::IDDB does (formats 1, 2 and the dense 5).
std::map<std::uint64_t, std::uint64_t> loadAddressLibrary(const fs::path& path)
{
    auto bytes = CB::engine::disk::ReadBytes(path);
    if (!bytes) throw std::runtime_error("cannot read Address Library " + path.string());
    const Bytes& b = *bytes;
    std::size_t  p = 0;
    auto take = [&](auto v) { using T = decltype(v); T out = rd<T>(b, p); p += sizeof(T); return out; };

    std::map<std::uint64_t, std::uint64_t> out;
    const auto format = take(std::int32_t{});
    if (format == 5) {
        p += 4 * 4 + 64;
        take(std::int32_t{});
        take(std::int32_t{});
        const auto count = take(std::int32_t{});
        for (std::int32_t i = 0; i < count; ++i)
            if (const auto o = take(std::uint32_t{})) out[std::uint64_t(i)] = o;
        return out;
    }
    if (format != 1 && format != 2) throw std::runtime_error(path.string() + ": Address Library format " + std::to_string(format) + " unsupported");
    p += 4 * 4;
    p += std::size_t(take(std::int32_t{}));   // module name
    const auto pointerSize = std::uint64_t(take(std::int32_t{}));
    const auto count       = take(std::int32_t{});
    std::uint64_t prevId = 0, prevOffset = 0;
    for (std::int32_t n = 0; n < count; ++n) {
        const auto type = take(std::uint8_t{});
        const int  lo = type & 0xF, hi = type >> 4;
        std::uint64_t id = 0;
        switch (lo) {
            case 0: id = take(std::uint64_t{}); break;
            case 1: id = prevId + 1; break;
            case 2: id = prevId + take(std::uint8_t{}); break;
            case 3: id = prevId - take(std::uint8_t{}); break;
            case 4: id = prevId + take(std::uint16_t{}); break;
            case 5: id = prevId - take(std::uint16_t{}); break;
            case 6: id = take(std::uint16_t{}); break;
            case 7: id = take(std::uint32_t{}); break;
            default: throw std::runtime_error(path.string() + ": bad id encoding");
        }
        const std::uint64_t base = (hi & 8) ? prevOffset / pointerSize : prevOffset;
        std::uint64_t offset = 0;
        switch (hi & 7) {
            case 0: offset = take(std::uint64_t{}); break;
            case 1: offset = base + 1; break;
            case 2: offset = base + take(std::uint8_t{}); break;
            case 3: offset = base - take(std::uint8_t{}); break;
            case 4: offset = base + take(std::uint16_t{}); break;
            case 5: offset = base - take(std::uint16_t{}); break;
            case 6: offset = take(std::uint16_t{}); break;
            case 7: offset = take(std::uint32_t{}); break;
        }
        if (hi & 8) offset *= pointerSize;
        out[id] = offset;
        prevId = id;
        prevOffset = offset;
    }
    return out;
}

/// @brief Where each engine symbol that reimplemented code references lives in this runtime's exe.
struct EngineSymbols {
    std::map<std::string, std::uint64_t>    byCpp;        ///< engine.yaml `cpp` name → absolute address
    std::map<std::string, std::string>      registryName; ///< `cpp` name → engine.yaml name
    std::map<std::uint64_t, std::uint64_t>  idByOffset;   ///< image offset → Address Library id (for --suggest)
};

EngineSymbols loadEngineSymbols(const fs::path& registry, const fs::path& addressLibrary, const std::string& pins, std::uint64_t imageBase)
{
    EngineSymbols out;
    const auto library = loadAddressLibrary(addressLibrary);
    for (const auto& [id, offset] : library) out.idByOffset.emplace(offset, id);

    auto text = CB::engine::disk::ReadFile(registry, { .stripBom = true });
    if (!text) throw std::runtime_error("cannot read registry " + registry.string());
    ensureRymlThrows();
    const c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(*text));
    const auto          root = tree.crootref();
    if (!has(root, "symbols")) throw std::runtime_error(registry.string() + " has no symbols:");
    for (const auto s : root["symbols"].children()) {
        if (!has(s, "cpp") || !has(s, pins.c_str())) continue;
        const std::string cpp = val(s["cpp"]);
        const std::string pin = val(s[c4::to_csubstr(pins.c_str())]);
        const auto        plus = pin.find('+');
        const auto        id = std::stoull(pin.substr(0, plus));
        const auto        off = plus == std::string::npos ? 0ull : std::stoull(pin.substr(plus + 1), nullptr, 16);
        if (id == 0) continue;
        const auto it = library.find(id);
        if (it == library.end()) throw std::runtime_error("engine.yaml " + val(s["name"]) + ": id " + std::to_string(id) + " is not in " + addressLibrary.string());
        out.byCpp[cpp]        = imageBase + it->second + off;
        out.registryName[cpp] = val(s["name"]);
    }
    return out;
}

// ── compiled objects ─────────────────────────────────────────────────────────────────────────────────────────────

struct CoffFunction {
    std::string                            mangled, name;
    Bytes                                  bytes;
    std::vector<bool>                      mask;     ///< bytes a relocation fills in at link time
    struct Target { std::string mangled, name; };
    std::map<std::uint32_t, Target>        targets;  ///< reloc offset → the symbol it refers to
};

std::mutex g_dbghelp;   // dbghelp is single-threaded

std::string undecorate(const std::string& mangled)
{
    if (mangled.empty() || mangled[0] != '?') return mangled;
    std::lock_guard lk(g_dbghelp);
    char buf[2048];
    return UnDecorateSymbolName(mangled.c_str(), buf, sizeof buf, UNDNAME_NAME_ONLY) ? std::string(buf) : mangled;
}

/// @brief Byte width a relocation patches, per the AMD64 COFF relocation types.
std::uint32_t relocWidth(std::uint16_t type)
{
    switch (type) {
        case IMAGE_REL_AMD64_ABSOLUTE: return 0;
        case IMAGE_REL_AMD64_ADDR64:   return 8;
        case IMAGE_REL_AMD64_SECTION:  return 2;
        default:                       return 4;
    }
}

std::vector<CoffFunction> loadCoffFunctions(const fs::path& path)
{
    auto bytes = CB::engine::disk::ReadBytes(path);
    if (!bytes) throw std::runtime_error("cannot read " + path.string());
    const Bytes& b = *bytes;
    if (rd<std::uint16_t>(b, 0) == 0 && rd<std::uint16_t>(b, 2) == 0xFFFF) throw std::runtime_error(path.string() + ": /bigobj objects are not supported");
    if (rd<std::uint16_t>(b, 0) != IMAGE_FILE_MACHINE_AMD64) throw std::runtime_error(path.string() + " is not an x64 object");

    const auto nSections = rd<std::uint16_t>(b, 2);
    const auto symTab    = rd<std::uint32_t>(b, 8);
    const auto nSymbols  = rd<std::uint32_t>(b, 12);
    const auto secTab    = 20u + rd<std::uint16_t>(b, 16);
    const auto strTab    = symTab + nSymbols * 18u;

    auto symName = [&](std::size_t s) {
        if (rd<std::uint32_t>(b, s) == 0) {
            const std::size_t o = strTab + rd<std::uint32_t>(b, s + 4);
            if (o >= b.size()) throw std::runtime_error("bad string-table offset");
            return std::string(reinterpret_cast<const char*>(b.data() + o));
        }
        const char* p = reinterpret_cast<const char*>(b.data() + s);
        return std::string(p, strnlen(p, 8));
    };

    std::vector<std::string> names(nSymbols);
    struct Sym { std::uint32_t index, value; };
    std::map<int, std::vector<Sym>> funcsBySection;
    for (std::uint32_t i = 0; i < nSymbols; ++i) {
        const std::size_t s = symTab + i * 18u;
        names[i]            = symName(s);
        const auto value    = rd<std::uint32_t>(b, s + 8);
        const auto section  = rd<std::int16_t>(b, s + 12);
        const auto type     = rd<std::uint16_t>(b, s + 14);
        const auto storage  = rd<std::uint8_t>(b, s + 16);
        const auto aux      = rd<std::uint8_t>(b, s + 17);
        if (section > 0 && section <= nSections && (type >> 4) == IMAGE_SYM_DTYPE_FUNCTION &&
            (storage == IMAGE_SYM_CLASS_EXTERNAL || storage == IMAGE_SYM_CLASS_STATIC))
            funcsBySection[section].push_back({ i, value });
        i += aux;
    }

    std::vector<CoffFunction> out;
    for (auto& [section, syms] : funcsBySection) {
        const std::size_t sh       = secTab + std::size_t(section - 1) * 40u;
        const auto        rawSize  = rd<std::uint32_t>(b, sh + 16);
        const auto        rawPtr   = rd<std::uint32_t>(b, sh + 20);
        const auto        relPtr   = rd<std::uint32_t>(b, sh + 24);
        const auto        nRelocs  = rd<std::uint16_t>(b, sh + 32);
        std::sort(syms.begin(), syms.end(), [](const Sym& a, const Sym& c) { return a.value < c.value; });
        for (std::size_t k = 0; k < syms.size(); ++k) {
            const std::uint32_t begin = syms[k].value;
            const std::uint32_t end   = k + 1 < syms.size() ? syms[k + 1].value : rawSize;
            if (end <= begin || rawPtr + std::size_t(end) > b.size()) continue;
            CoffFunction f;
            f.mangled = names[syms[k].index];
            f.name    = undecorate(f.mangled);
            f.bytes.assign(b.begin() + rawPtr + begin, b.begin() + rawPtr + end);
            f.mask.assign(f.bytes.size(), false);
            for (std::uint32_t r = 0; r < nRelocs; ++r) {
                const std::size_t ro    = relPtr + r * 10u;
                const auto        at    = rd<std::uint32_t>(b, ro);
                const auto        sym   = rd<std::uint32_t>(b, ro + 4);
                const auto        width = relocWidth(rd<std::uint16_t>(b, ro + 8));
                if (at < begin || at >= end) continue;
                for (std::uint32_t w = 0; w < width && at - begin + w < f.mask.size(); ++w) f.mask[at - begin + w] = true;
                if (sym < names.size()) f.targets[at - begin] = { names[sym], undecorate(names[sym]) };
            }
            out.push_back(std::move(f));
        }
    }
    return out;
}

// ── compiling ────────────────────────────────────────────────────────────────────────────────────────────────────

std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    const int    n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::wstring quoteArg(const std::wstring& a)
{
    if (a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (wchar_t c : a) { if (c == L'"') q += L'\\'; q += c; }
    return q + L"\"";
}

/// @brief The current environment with `dir` prepended to PATH, so cl.exe finds the DLLs of its own toolset.
/// @note Windows matches environment names case-insensitively; this follows the OS's own rule.
std::vector<wchar_t> environmentWithPath(const fs::path& dir)
{
    std::vector<std::wstring> vars;
    wchar_t*                  env  = GetEnvironmentStringsW();
    bool                      seen = false;
    for (const wchar_t* p = env; *p; p += wcslen(p) + 1) {
        std::wstring v = p;
        if (_wcsnicmp(v.c_str(), L"PATH=", 5) == 0) { v = L"PATH=" + dir.wstring() + L";" + v.substr(5); seen = true; }
        vars.push_back(std::move(v));
    }
    FreeEnvironmentStringsW(env);
    if (!seen) vars.push_back(L"PATH=" + dir.wstring());
    std::vector<wchar_t> block;
    for (const auto& v : vars) { block.insert(block.end(), v.begin(), v.end()); block.push_back(L'\0'); }
    block.push_back(L'\0');
    return block;
}

struct CompileOutput {
    bool                      ok = false;
    std::string               log;
    std::vector<CoffFunction> functions;
};

CompileOutput compileUnit(const Manifest& m, const UnitSpec& unit, const fs::path& cl, const fs::path& outDir)
{
    CompileOutput res;
    fs::create_directories(outDir);
    const fs::path obj = outDir / "unit.obj";
    const fs::path cod = outDir / "unit.cod";
    const fs::path log = outDir / "cl.log";
    fs::remove(obj);

    std::vector<std::wstring> args = { cl.wstring(), L"/nologo", L"/c", L"/Gy" };
    for (const auto& f : m.flags) args.push_back(widen(f));
    for (const auto& f : unit.flags) args.push_back(widen(f));
    for (const auto& d : unit.defines) args.push_back(L"/D" + widen(d));
    for (const auto& i : m.includes) args.push_back(L"/I" + i.wstring());
    args.push_back(L"/Fo" + obj.wstring());
    args.push_back(L"/Fa" + cod.wstring());
    args.push_back(L"/FAc");
    args.push_back(unit.source.wstring());

    std::wstring cmd;
    for (const auto& a : args) { if (!cmd.empty()) cmd += L' '; cmd += quoteArg(a); }

    SECURITY_ATTRIBUTES sa{ sizeof sa, nullptr, TRUE };
    HANDLE logH = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logH == INVALID_HANDLE_VALUE) { res.log = "cannot create " + log.string(); return res; }

    STARTUPINFOW si{};
    si.cb         = sizeof si;
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = logH;
    si.hStdError  = logH;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    auto env = environmentWithPath(cl.parent_path());
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                                        env.data(), m.dir.c_str(), &si, &pi);
    if (!started) {
        CloseHandle(logH);
        res.log = "cannot start " + cl.string() + " (error " + std::to_string(GetLastError()) + ")";
        return res;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(logH);

    res.log = CB::engine::disk::ReadFile(log).value_or("");
    if (code != 0 || !fs::exists(obj)) return res;
    res.functions = loadCoffFunctions(obj);
    res.ok        = true;
    return res;
}

// ── comparing ────────────────────────────────────────────────────────────────────────────────────────────────────

struct Insn {
    std::uint32_t           off = 0;
    ZydisDecodedInstruction ins{};
    ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT]{};
    std::string             text;
};

std::optional<std::vector<Insn>> disassemble(const Bytes& bytes)
{
    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisFormatter fmt;
    ZydisFormatterInit(&fmt, ZYDIS_FORMATTER_STYLE_INTEL);
    std::vector<Insn> out;
    for (std::uint32_t off = 0; off < bytes.size();) {
        Insn i;
        i.off = off;
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, bytes.data() + off, bytes.size() - off, &i.ins, i.ops))) return std::nullopt;
        char buf[256];
        // Function-relative addresses, so branches inside matching functions print identically on both sides.
        if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&fmt, &i.ins, i.ops, i.ins.operand_count_visible, buf, sizeof buf, off, ZYAN_NULL)))
            i.text = buf;
        off += i.ins.length;
        out.push_back(std::move(i));
    }
    return out;
}

bool sameRegClass(ZydisRegister a, ZydisRegister b) { return ZydisRegisterGetClass(a) == ZydisRegisterGetClass(b); }

/// @brief Operands equal up to register choice. `masked`: the instruction holds a relocated field, so its
///        displacement/immediate is the linker's business and not compared.
bool operandsMatch(const ZydisDecodedOperand& a, const ZydisDecodedOperand& b, bool masked)
{
    if (a.type != b.type || a.size != b.size) return false;
    switch (a.type) {
        case ZYDIS_OPERAND_TYPE_REGISTER:
            return sameRegClass(a.reg.value, b.reg.value);
        case ZYDIS_OPERAND_TYPE_MEMORY:
            if (a.mem.type != b.mem.type || a.mem.scale != b.mem.scale || a.mem.segment != b.mem.segment) return false;
            if ((a.mem.base == ZYDIS_REGISTER_NONE) != (b.mem.base == ZYDIS_REGISTER_NONE)) return false;
            if ((a.mem.index == ZYDIS_REGISTER_NONE) != (b.mem.index == ZYDIS_REGISTER_NONE)) return false;
            if ((a.mem.base == ZYDIS_REGISTER_RIP) != (b.mem.base == ZYDIS_REGISTER_RIP)) return false;
            if (a.mem.base != ZYDIS_REGISTER_NONE && !sameRegClass(a.mem.base, b.mem.base)) return false;
            if (a.mem.index != ZYDIS_REGISTER_NONE && !sameRegClass(a.mem.index, b.mem.index)) return false;
            return masked || a.mem.disp.value == b.mem.disp.value;
        case ZYDIS_OPERAND_TYPE_IMMEDIATE:
            return masked || (a.imm.is_relative == b.imm.is_relative && a.imm.value.u == b.imm.value.u);
        case ZYDIS_OPERAND_TYPE_POINTER:
            return a.ptr.segment == b.ptr.segment && a.ptr.offset == b.ptr.offset;
        default:
            return true;
    }
}

enum class Status { Exact, Regalloc, Variant, Structural, Missing, CompileError };

const char* statusName(Status s)
{
    switch (s) {
        case Status::Exact:        return "EXACT";
        case Status::Regalloc:     return "REGALLOC";
        case Status::Variant:      return "VARIANT";
        case Status::Structural:   return "STRUCTURAL";
        case Status::Missing:      return "MISSING";
        case Status::CompileError: return "COMPILE-ERROR";
    }
    return "?";
}

int statusRank(Status s)
{
    switch (s) {
        case Status::Exact:      return 0;
        case Status::Regalloc:   return 1;
        case Status::Variant:    return 2;
        case Status::Structural: return 3;
        default:                 return 4;
    }
}

/// @brief A reference our code makes that engine.yaml can't place yet.
struct UnmappedRef {
    std::string   name, mangled;
    std::uint64_t vanillaTarget = 0;   ///< where vanilla's matching instruction points
    bool          code = false;        ///< reached by call/jmp (a function), else data
};

struct Match {
    Status      status = Status::Missing;
    std::string toolchain;
    double      pct = 0;
    std::size_t oursLen = 0, vanillaLen = 0;
    long long   firstDiff = -1;
    std::string detail;
    std::vector<std::string> listing;

    bool                     refsChecked = false;
    std::size_t              refsVerified = 0, refsUnverifiable = 0;
    std::vector<std::string> refsWrong;
    std::vector<UnmappedRef> refsUnmapped;
};

std::string targetNote(const CoffFunction& f, const Insn& i)
{
    for (std::uint32_t o = i.off; o < i.off + i.ins.length; ++o)
        if (auto it = f.targets.find(o); it != f.targets.end()) return "  -> " + it->second.name;
    return {};
}

/// @brief The absolute target of a branch or RIP-relative operand that leaves the function [va, va + len).
std::string vanillaTargetNote(const Insn& i, std::uint64_t va, std::size_t len)
{
    for (std::uint8_t k = 0; k < i.ins.operand_count_visible; ++k) {
        const auto& op = i.ops[k];
        const bool  rel = (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative) ||
                         (op.type == ZYDIS_OPERAND_TYPE_MEMORY && op.mem.base == ZYDIS_REGISTER_RIP);
        if (!rel) continue;
        ZyanU64 abs = 0;
        if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&i.ins, &op, va + i.off, &abs)) && (abs < va || abs >= va + len))
            return "  -> 0x" + hex(abs);
    }
    return {};
}

/// @brief What an instruction IS, ignoring which registers and displacements it uses: aligning on this pairs
///        instructions that differ only in register choice.
bool sameShape(const Insn& x, const Insn& y)
{
    if (x.ins.mnemonic != y.ins.mnemonic || x.ins.operand_count_visible != y.ins.operand_count_visible) return false;
    for (std::uint8_t o = 0; o < x.ins.operand_count_visible; ++o)
        if (x.ops[o].type != y.ops[o].type || x.ops[o].size != y.ops[o].size) return false;
    return true;
}

/// @brief 0 = different instructions; 1 = same shape; 2 = same shape and the same immediates and displacements too.
/// @note Weighting exact pairs higher keeps two independent instructions that a build emitted in the other order
///       (shufps 0xc9 / 0xd2) paired with their own counterparts instead of each other.
unsigned matchScore(const Insn& x, const Insn& y)
{
    if (!sameShape(x, y)) return 0;
    for (std::uint8_t o = 0; o < x.ins.operand_count_visible; ++o) {
        const auto& ox = x.ops[o];
        const auto& oy = y.ops[o];
        if (ox.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && !ox.imm.is_relative && ox.imm.value.u != oy.imm.value.u) return 1;
        if (ox.type == ZYDIS_OPERAND_TYPE_MEMORY && ox.mem.disp.value != oy.mem.disp.value) return 1;
    }
    return 2;
}

/// @brief Pairs of (ours, vanilla) instruction indices; -1 on one side marks an instruction the other side lacks.
/// @note Weighted LCS over instruction shapes (matchScore), so one inserted instruction doesn't misalign the rest of
///       the listing. Falls back to index pairing when the table would be too large.
std::vector<std::pair<long, long>> alignInstructions(const std::vector<Insn>& a, const std::vector<Insn>& b)
{
    std::vector<std::pair<long, long>> rows;
    const std::size_t n = a.size(), m = b.size();
    if (n * m > 40'000'000) {
        for (std::size_t k = 0; k < std::max(n, m); ++k) rows.emplace_back(k < n ? long(k) : -1, k < m ? long(k) : -1);
        return rows;
    }
    std::vector<std::uint32_t> lcs((n + 1) * (m + 1), 0);
    auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return lcs[i * (m + 1) + j]; };
    for (std::size_t i = n; i-- > 0;)
        for (std::size_t j = m; j-- > 0;) {
            const unsigned s    = matchScore(a[i], b[j]);
            const auto     skip = std::max(at(i + 1, j), at(i, j + 1));
            at(i, j)            = s ? std::max<std::uint32_t>(at(i + 1, j + 1) + s, skip) : skip;
        }
    std::size_t i = 0, j = 0;
    while (i < n || j < m) {
        const unsigned s = (i < n && j < m) ? matchScore(a[i], b[j]) : 0;
        if (s && at(i, j) == at(i + 1, j + 1) + s) rows.emplace_back(long(i++), long(j++));
        else if (j < m && (i == n || at(i, j + 1) >= at(i + 1, j))) rows.emplace_back(-1, long(j++));
        else rows.emplace_back(long(i++), -1);
    }
    return rows;
}

/// @brief Side-by-side listing. Markers: ' ' identical bytes, '~' same instruction with other registers or
///        displacements, '<' only in ours, '>' only in vanilla.
void buildListing(Match& m, const CoffFunction& ours, const std::vector<Insn>& a, const Bytes& vanilla, const std::vector<Insn>& b, std::uint64_t va)
{
    for (const auto& [ia, ib] : alignInstructions(a, b)) {
        std::string left, right;
        char        mark = ' ';
        if (ia >= 0) left = a[std::size_t(ia)].text + targetNote(ours, a[std::size_t(ia)]);
        if (ib >= 0) right = b[std::size_t(ib)].text + vanillaTargetNote(b[std::size_t(ib)], va, vanilla.size());
        if (ia < 0) mark = '>';
        else if (ib < 0) mark = '<';
        else {
            const auto& x = a[std::size_t(ia)];
            const auto& y = b[std::size_t(ib)];
            bool same = x.ins.length == y.ins.length;
            for (std::uint32_t k = 0; same && k < x.ins.length; ++k) same = ours.mask[x.off + k] || ours.bytes[x.off + k] == vanilla[y.off + k];
            if (!same) mark = '~';
        }
        char offs[24];
        if (ia >= 0 && ib >= 0) std::snprintf(offs, sizeof offs, "%04x %04x", a[std::size_t(ia)].off, b[std::size_t(ib)].off);
        else if (ia >= 0)       std::snprintf(offs, sizeof offs, "%04x     ", a[std::size_t(ia)].off);
        else                    std::snprintf(offs, sizeof offs, "     %04x", b[std::size_t(ib)].off);
        char line[600];
        std::snprintf(line, sizeof line, "%c %s  %-52s | %s", mark, offs, left.c_str(), right.c_str());
        m.listing.push_back(line);
    }
}

/// @brief The condition a jcc/cmovcc/setcc tests, folded with its inverse (jz and jnz → "z"); nullptr for anything else.
/// @note Branch layout only inverts conditions; changing which flag is tested (jb → jbe) changes the family.
const char* conditionFamily(ZydisMnemonic m)
{
    switch (m) {
        case ZYDIS_MNEMONIC_JB:  case ZYDIS_MNEMONIC_JNB:  case ZYDIS_MNEMONIC_CMOVB:  case ZYDIS_MNEMONIC_CMOVNB:  case ZYDIS_MNEMONIC_SETB:  case ZYDIS_MNEMONIC_SETNB:  return "b";
        case ZYDIS_MNEMONIC_JBE: case ZYDIS_MNEMONIC_JNBE: case ZYDIS_MNEMONIC_CMOVBE: case ZYDIS_MNEMONIC_CMOVNBE: case ZYDIS_MNEMONIC_SETBE: case ZYDIS_MNEMONIC_SETNBE: return "be";
        case ZYDIS_MNEMONIC_JZ:  case ZYDIS_MNEMONIC_JNZ:  case ZYDIS_MNEMONIC_CMOVZ:  case ZYDIS_MNEMONIC_CMOVNZ:  case ZYDIS_MNEMONIC_SETZ:  case ZYDIS_MNEMONIC_SETNZ:  return "z";
        case ZYDIS_MNEMONIC_JS:  case ZYDIS_MNEMONIC_JNS:  case ZYDIS_MNEMONIC_CMOVS:  case ZYDIS_MNEMONIC_CMOVNS:  case ZYDIS_MNEMONIC_SETS:  case ZYDIS_MNEMONIC_SETNS:  return "s";
        case ZYDIS_MNEMONIC_JL:  case ZYDIS_MNEMONIC_JNL:  case ZYDIS_MNEMONIC_CMOVL:  case ZYDIS_MNEMONIC_CMOVNL:  case ZYDIS_MNEMONIC_SETL:  case ZYDIS_MNEMONIC_SETNL:  return "l";
        case ZYDIS_MNEMONIC_JLE: case ZYDIS_MNEMONIC_JNLE: case ZYDIS_MNEMONIC_CMOVLE: case ZYDIS_MNEMONIC_CMOVNLE: case ZYDIS_MNEMONIC_SETLE: case ZYDIS_MNEMONIC_SETNLE: return "le";
        case ZYDIS_MNEMONIC_JO:  case ZYDIS_MNEMONIC_JNO:  case ZYDIS_MNEMONIC_CMOVO:  case ZYDIS_MNEMONIC_CMOVNO:  case ZYDIS_MNEMONIC_SETO:  case ZYDIS_MNEMONIC_SETNO:  return "o";
        case ZYDIS_MNEMONIC_JP:  case ZYDIS_MNEMONIC_JNP:  case ZYDIS_MNEMONIC_CMOVP:  case ZYDIS_MNEMONIC_CMOVNP:  case ZYDIS_MNEMONIC_SETP:  case ZYDIS_MNEMONIC_SETNP:  return "p";
        default: return nullptr;
    }
}

/// @brief Instructions that can never execute, marked true: a forward `jge`/`jns` right after `and r32, <mask with the
///        sign bit clear>` is always taken (the and leaves SF = OF = 0), so the block it jumps over is dead — unless
///        another branch lands inside it. The guarding and/jcc are marked too: together they are an unconditional jump.
/// @note  One compiler build deletes such a block (hkArray::reserve(0) behind setSize(0)); another keeps it.
ZydisRegister enclosing(ZydisRegister r);

std::vector<bool> provablyDead(const std::vector<Insn>& insns)
{
    std::vector<bool> dead(insns.size(), false);
    auto branchTarget = [](const Insn& i) -> long long {
        for (std::uint8_t k = 0; k < i.ins.operand_count_visible; ++k)
            if (i.ops[k].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && i.ops[k].imm.is_relative)
                return static_cast<long long>(i.off) + i.ins.length + i.ops[k].imm.value.s;
        return -1;
    };
    for (std::size_t k = 0; k + 1 < insns.size(); ++k) {
        const Insn& a = insns[k];
        const Insn& j = insns[k + 1];
        if (a.ins.mnemonic != ZYDIS_MNEMONIC_AND || a.ins.operand_count_visible != 2 || a.ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER ||
            a.ops[0].size != 32 || a.ops[1].type != ZYDIS_OPERAND_TYPE_IMMEDIATE || (a.ops[1].imm.value.u & 0x80000000u) != 0)
            continue;
        if (j.ins.mnemonic != ZYDIS_MNEMONIC_JNL && j.ins.mnemonic != ZYDIS_MNEMONIC_JNS) continue;
        const long long target = branchTarget(j);
        if (target <= static_cast<long long>(j.off)) continue;
        std::size_t end = k + 2;
        while (end < insns.size() && insns[end].off < target) ++end;
        bool enteredElsewhere = false;
        for (std::size_t m = 0; m < insns.size() && !enteredElsewhere; ++m) {
            if (m == k + 1) continue;
            const long long t = branchTarget(insns[m]);
            enteredElsewhere = t > static_cast<long long>(j.off) && t < target && insns[m].ins.mnemonic != ZYDIS_MNEMONIC_CALL;
        }
        if (enteredElsewhere) continue;
        for (std::size_t m = k; m < end; ++m) dead[m] = true;
    }

    // A repeated guard: `test X, X; jcc` falling through into straight-line code that re-tests the unchanged X with the
    // same jcc. The repeat can never branch (one build keeps a rotated loop's entry guard behind an earlier identical
    // test, another folds it). Straight-line = no call, branch or return in between, nothing jumps into the stretch.
    std::set<long long> targets;
    for (const auto& i : insns)
        if (i.ins.mnemonic != ZYDIS_MNEMONIC_CALL)
            if (const long long t = branchTarget(i); t >= 0) targets.insert(t);
    auto isSelfTest = [](const Insn& i) {
        return i.ins.mnemonic == ZYDIS_MNEMONIC_TEST && i.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
               i.ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER && i.ops[0].reg.value == i.ops[1].reg.value;
    };
    auto isCondJump = [](const Insn& i) { return i.ins.meta.category == ZYDIS_CATEGORY_COND_BR; };
    for (std::size_t k = 0; k + 1 < insns.size(); ++k) {
        if (!isSelfTest(insns[k]) || !isCondJump(insns[k + 1])) continue;
        const ZydisRegister x = enclosing(insns[k].ops[0].reg.value);
        for (std::size_t m = k + 2; m + 1 < insns.size(); ++m) {
            const Insn& i = insns[m];
            if (targets.count(static_cast<long long>(i.off))) break;
            if (isSelfTest(i) && insns[m].ops[0].reg.value == insns[k].ops[0].reg.value &&
                insns[m + 1].ins.mnemonic == insns[k + 1].ins.mnemonic && !targets.count(static_cast<long long>(insns[m + 1].off))) {
                dead[m] = dead[m + 1] = true;
                break;
            }
            const auto cat = i.ins.meta.category;
            if (cat == ZYDIS_CATEGORY_CALL || cat == ZYDIS_CATEGORY_RET || cat == ZYDIS_CATEGORY_COND_BR || cat == ZYDIS_CATEGORY_UNCOND_BR)
                break;
            bool writesX = false;
            for (std::uint8_t o = 0; o < i.ins.operand_count && !writesX; ++o)
                writesX = i.ops[o].type == ZYDIS_OPERAND_TYPE_REGISTER && enclosing(i.ops[o].reg.value) == x &&
                          (i.ops[o].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE);
            if (writesX) break;
        }
    }
    return dead;
}

/// @brief Floating-point/SIMD arithmetic. Scheduling and branch layout move these but never change how many a function
///        performs, so unpaired ones must balance per mnemonic (add → sub is a real change).
bool isFloatArithmetic(ZydisMnemonic m)
{
    switch (m) {
        case ZYDIS_MNEMONIC_ADDPS: case ZYDIS_MNEMONIC_ADDSS: case ZYDIS_MNEMONIC_SUBPS: case ZYDIS_MNEMONIC_SUBSS:
        case ZYDIS_MNEMONIC_MULPS: case ZYDIS_MNEMONIC_MULSS: case ZYDIS_MNEMONIC_DIVPS: case ZYDIS_MNEMONIC_DIVSS:
        case ZYDIS_MNEMONIC_SQRTPS: case ZYDIS_MNEMONIC_SQRTSS: case ZYDIS_MNEMONIC_RSQRTPS: case ZYDIS_MNEMONIC_RSQRTSS:
        case ZYDIS_MNEMONIC_RCPPS: case ZYDIS_MNEMONIC_RCPSS: case ZYDIS_MNEMONIC_MINPS: case ZYDIS_MNEMONIC_MINSS:
        case ZYDIS_MNEMONIC_MAXPS: case ZYDIS_MNEMONIC_MAXSS: case ZYDIS_MNEMONIC_ADDSD: case ZYDIS_MNEMONIC_SUBSD:
        case ZYDIS_MNEMONIC_MULSD: case ZYDIS_MNEMONIC_DIVSD:
            return true;
        default:
            return false;
    }
}

/// @brief Where a function saves each callee-saved register it uses: register → stack slot (rsp displacement).
/// @note A save is a plain move of a callee-saved register (rbx rbp rsi rdi r12-r15 xmm6-xmm15) to [rsp+d] at the
///       register's first appearance, so it still holds the caller's value. Shrink-wrapped saves sit after the frame is
///       allocated, which the prologue rule does not cover.
using SaveSlots = std::map<ZydisRegister, std::int64_t>;

ZydisRegister enclosing(ZydisRegister r) { return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, r); }

bool isCalleeSaved(ZydisRegister r)
{
    switch (r) {
        case ZYDIS_REGISTER_RBX: case ZYDIS_REGISTER_RBP: case ZYDIS_REGISTER_RSI: case ZYDIS_REGISTER_RDI:
        case ZYDIS_REGISTER_R12: case ZYDIS_REGISTER_R13: case ZYDIS_REGISTER_R14: case ZYDIS_REGISTER_R15:
            return true;
        default:
            return r >= ZYDIS_REGISTER_XMM6 && r <= ZYDIS_REGISTER_XMM15;
    }
}

bool isPlainMove(ZydisMnemonic m)
{
    return m == ZYDIS_MNEMONIC_MOV || m == ZYDIS_MNEMONIC_MOVAPS || m == ZYDIS_MNEMONIC_MOVUPS || m == ZYDIS_MNEMONIC_MOVDQA ||
           m == ZYDIS_MNEMONIC_MOVDQU;
}

/// @brief How far rsp sits below its entry value at each instruction, so stack slots compare across address forms.
/// @note r11 is a frame base only when the function opens with `mov r11, rsp` and never writes r11 again. After an
///       unconditional exit (ret / tail jmp) the depth resets to the body's: the following code still has the frame.
struct Frame {
    std::map<std::uint32_t, std::int64_t> depth;
    bool                                  r11IsEntry = false;
};

Frame frameOf(const std::vector<Insn>& all)
{
    Frame        f;
    std::int64_t depth = 0, body = 0;
    bool         inPrologue = true;
    if (!all.empty() && all[0].ins.mnemonic == ZYDIS_MNEMONIC_MOV && all[0].ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
        all[0].ops[0].reg.value == ZYDIS_REGISTER_R11 && all[0].ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
        all[0].ops[1].reg.value == ZYDIS_REGISTER_RSP) {
        f.r11IsEntry = true;
        for (std::size_t k = 1; k < all.size() && f.r11IsEntry; ++k)
            for (std::uint8_t o = 0; o < all[k].ins.operand_count; ++o)
                if (all[k].ops[o].type == ZYDIS_OPERAND_TYPE_REGISTER && enclosing(all[k].ops[o].reg.value) == ZYDIS_REGISTER_R11 &&
                    (all[k].ops[o].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
                    f.r11IsEntry = false;
    }
    for (const auto& i : all) {
        f.depth[i.off] = depth;
        const auto& op0  = i.ops[0];
        const bool  rsp0 = op0.type == ZYDIS_OPERAND_TYPE_REGISTER && op0.reg.value == ZYDIS_REGISTER_RSP;
        const auto  imm  = i.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE ? std::int64_t(i.ops[1].imm.value.s) : 0;
        switch (i.ins.mnemonic) {
            case ZYDIS_MNEMONIC_PUSH: depth += 8; break;
            case ZYDIS_MNEMONIC_POP:  depth -= 8; break;
            case ZYDIS_MNEMONIC_SUB:  if (rsp0) { depth += imm; if (inPrologue) { body = depth; inPrologue = false; } } break;
            case ZYDIS_MNEMONIC_ADD:  if (rsp0) depth -= imm; break;
            case ZYDIS_MNEMONIC_RET:  depth = body; break;
            case ZYDIS_MNEMONIC_JMP:  if (!inPrologue && depth != body) depth = body; break;
            default: break;
        }
    }
    return f;
}

/// @brief A stack slot as an offset from the entry rsp (nullopt: not a plain rsp/frame-relative slot).
std::optional<std::int64_t> slotOf(const ZydisDecodedOperand& op, const Insn& i, const Frame& f)
{
    if (op.type != ZYDIS_OPERAND_TYPE_MEMORY || op.mem.type != ZYDIS_MEMOP_TYPE_MEM || op.mem.index != ZYDIS_REGISTER_NONE)
        return std::nullopt;
    if (op.mem.base == ZYDIS_REGISTER_RSP) return op.mem.disp.value - f.depth.at(i.off);
    if (op.mem.base == ZYDIS_REGISTER_R11 && f.r11IsEntry) return op.mem.disp.value;
    return std::nullopt;
}

SaveSlots findSaves(const std::vector<Insn>& all, const Frame& f)
{
    SaveSlots                saves;
    std::set<ZydisRegister> seen;
    for (const auto& i : all) {
        const auto slot = slotOf(i.ops[0], i, f);
        if (isPlainMove(i.ins.mnemonic) && i.ins.operand_count_visible == 2 && slot && i.ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER) {
            const ZydisRegister r = i.ops[1].reg.value;
            if (isCalleeSaved(r) && !seen.count(enclosing(r)) && !saves.count(r)) saves[r] = *slot;
        }
        for (std::uint8_t k = 0; k < i.ins.operand_count; ++k) {
            const auto& op = i.ops[k];
            if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) seen.insert(enclosing(op.reg.value));
            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY) {
                if (op.mem.base != ZYDIS_REGISTER_NONE) seen.insert(enclosing(op.mem.base));
                if (op.mem.index != ZYDIS_REGISTER_NONE) seen.insert(enclosing(op.mem.index));
            }
        }
    }
    return saves;
}

/// @brief The save of a callee-saved register, or its restore from that same slot: register allocation, not an effect.
bool isSaveOrRestore(const Insn& i, const SaveSlots& saves, const Frame& f)
{
    if (!isPlainMove(i.ins.mnemonic) || i.ins.operand_count_visible != 2) return false;
    const auto& d = i.ops[0];
    const auto& s = i.ops[1];
    if (const auto slot = slotOf(d, i, f); slot && s.type == ZYDIS_OPERAND_TYPE_REGISTER) {
        const auto it = saves.find(s.reg.value);
        return it != saves.end() && it->second == *slot;
    }
    if (const auto slot = slotOf(s, i, f); slot && d.type == ZYDIS_OPERAND_TYPE_REGISTER) {
        const auto it = saves.find(d.reg.value);
        return it != saves.end() && it->second == *slot;
    }
    return false;
}

bool isMasked(const CoffFunction& f, const Insn& i)
{
    for (std::uint32_t o = i.off; o < i.off + i.ins.length; ++o)
        if (f.mask[o]) return true;
    return false;
}

/// @brief The memory operand an instruction writes (nullptr when it writes none).
const ZydisDecodedOperand* writtenMemory(const Insn& i)
{
    for (std::uint8_t k = 0; k < i.ins.operand_count_visible; ++k) {
        const auto& op = i.ops[k];
        if (op.type == ZYDIS_OPERAND_TYPE_MEMORY && op.mem.type == ZYDIS_MEMOP_TYPE_MEM && (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
            return &op;
    }
    return nullptr;
}

/// @brief An instruction whose effect is visible outside the function's registers: a call, a return, a stack
///        push/pop, or a memory write.
bool hasEffect(const Insn& i)
{
    switch (i.ins.mnemonic) {
        case ZYDIS_MNEMONIC_CALL:
        case ZYDIS_MNEMONIC_RET:
        case ZYDIS_MNEMONIC_PUSH:
        case ZYDIS_MNEMONIC_POP:
            return true;
        default:
            return writtenMemory(i) != nullptr;
    }
}

/// @brief Two unaligned effects that are the same effect: the same operation on the same memory operand. Only the
///        source may differ (an immediate 0 against a zeroed register).
bool sameEffect(const Insn& x, const Insn& y, bool masked)
{
    if (x.ins.mnemonic != y.ins.mnemonic) return false;
    const auto* mx = writtenMemory(x);
    const auto* my = writtenMemory(y);
    if (mx && my) return operandsMatch(*mx, *my, masked);
    if (mx || my || x.ins.operand_count_visible != y.ins.operand_count_visible) return false;
    if (x.ins.operand_count_visible == 0) return true;
    const bool relative = x.ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && x.ops[0].imm.is_relative;
    return operandsMatch(x.ops[0], y.ops[0], masked || relative);
}

/// @brief VARIANT: the same calls, returns, stack traffic and memory writes in the same order, at the same data
///        offsets; only register choice, branch layout (tail merging/duplication, cmov vs branch) and side-effect-free
///        instructions (loads, compares, register moves) may differ. The signature of a different build of the same
///        compiler compiling the same source.
/// @warning Weaker than EXACT/REGALLOC: it checks effects (calls, stores) and tested conditions, not register dataflow.
///          NOT caught: a changed or dropped register computation (e.g. a loop's `i--` removed, two register arguments
///          swapped — both verified to pass), a different value loaded into a register, a condition change hidden by a
///          duplicated branch of the same family, an inverted test (`if (p)` → `if (!p)`: jz/jnz share a family because
///          branch layout inverts them — verified to pass).
///          Caught (deliberately broken copies, tools/cbmatch/tests/mutants.sh): wrong field read or written, dropped or
///          extra call, dropped or extra store, wrong argument, wrong constant, >= → >. A VARIANT function still needs
///          behavioural verification.
std::optional<std::uint64_t> absoluteTarget(const Insn& i, std::uint64_t runtimeAddress);

/// @brief Rewrites `mulss/mulps x, [2.0]` to `addss/addps x, x` — bit-identical in IEEE arithmetic; one compiler build
///        strength-reduces the multiply, another keeps it. `isTwo` says whether the instruction's constant operand is 2.0
///        in every lane (checked by value, never assumed).
void canonicalizeDoubling(std::vector<Insn>& insns, const std::function<bool(const Insn&, std::size_t lanes)>& isTwo)
{
    for (auto& i : insns) {
        const bool scalar = i.ins.mnemonic == ZYDIS_MNEMONIC_MULSS;
        if (!scalar && i.ins.mnemonic != ZYDIS_MNEMONIC_MULPS) continue;
        if (i.ins.operand_count_visible != 2 || i.ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER ||
            i.ops[1].type != ZYDIS_OPERAND_TYPE_MEMORY || i.ops[1].mem.base != ZYDIS_REGISTER_RIP || !isTwo(i, scalar ? 1 : 4))
            continue;
        i.ins.mnemonic = scalar ? ZYDIS_MNEMONIC_ADDSS : ZYDIS_MNEMONIC_ADDPS;
        i.ops[1]         = i.ops[0];
        i.ops[1].actions = ZYDIS_OPERAND_ACTION_READ;
    }
}

bool variantEquivalent(const CoffFunction& ours, const std::vector<Insn>& oursInsns, const std::vector<Insn>& vanillaInsns, std::uint64_t va,
                       const PeImage& pe, std::string& why)
{
    std::vector<Insn> a = oursInsns;
    std::vector<Insn> b = vanillaInsns;
    canonicalizeDoubling(a, [&](const Insn& i, std::size_t lanes) {
        std::string want = lanes == 1 ? "__real@40000000" : "__xmm@40000000400000004000000040000000";
        for (std::uint32_t o = i.off; o < i.off + i.ins.length; ++o)
            if (const auto t = ours.targets.find(o); t != ours.targets.end()) return t->second.mangled == want;
        return false;
    });
    canonicalizeDoubling(b, [&](const Insn& i, std::size_t lanes) {
        const auto target = absoluteTarget(i, va + i.off);
        if (!target) return false;
        const auto off = pe.fileOffset(static_cast<std::uint32_t>(*target - pe.imageBase), 4 * lanes);
        if (!off) return false;
        for (std::size_t k = 0; k < lanes; ++k)
            if (std::memcmp(&pe.bytes[*off + 4 * k], "\x00\x00\x00\x40", 4) != 0) return false;
        return true;
    });

    // Unpaired effects are matched as a set within each stretch between two aligned calls: a compiler may reorder
    // independent stores, never across a call.
    struct Effect { const Insn* insn; std::size_t segment; bool used = false; };
    std::vector<Effect>        effectsA, effectsB;
    /// Unpaired condition family → (count in ours, count in vanilla). Counts may differ (tail duplication copies a
    /// branch); a family unpaired on one side only means a tested condition changed.
    std::map<std::string, std::pair<int, int>> conditions;
    std::map<int, int>                         floatOps;   ///< unpaired FP mnemonic → ours minus vanilla
    std::size_t                                aligned = 0, glue = 0, segment = 0, deadCount = 0;

    // Provably dead code is left out on both sides before aligning: one compiler build deletes it, another keeps it.
    auto live = [&](const std::vector<Insn>& all) {
        const auto        dead = provablyDead(all);
        std::vector<Insn> out;
        for (std::size_t k = 0; k < all.size(); ++k) {
            if (dead[k]) ++deadCount;
            else out.push_back(all[k]);
        }
        return out;
    };
    // Callee-saved register spills happen before the frame is allocated (`sub rsp, N`); how many registers a build
    // needs is register allocation, not an effect.
    auto prologueEnd = [](const std::vector<Insn>& all) {
        for (std::size_t k = 0; k < all.size(); ++k)
            if (all[k].ins.mnemonic == ZYDIS_MNEMONIC_SUB && all[k].ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                all[k].ops[0].reg.value == ZYDIS_REGISTER_RSP)
                return all[k].off;
        return 0u;
    };
    const std::vector<Insn> liveA = live(a);
    const std::vector<Insn> liveB = live(b);
    const std::uint32_t     prologueA = prologueEnd(a);
    const std::uint32_t     prologueB = prologueEnd(b);
    const Frame             frameA    = frameOf(a);
    const Frame             frameB    = frameOf(b);
    const SaveSlots         savesA    = findSaves(a, frameA);
    const SaveSlots         savesB    = findSaves(b, frameB);
    // Two independent loads a build emitted in the other order get aligned crosswise when keeping them straight would
    // cost a whole reordered block. Such a pair is accepted only with evidence: the other side has an unpaired read of
    // each slot (same mnemonic and width), so the reads balance; a wrong field read finds no partner.
    struct Read { ZydisMnemonic mnemonic; std::uint16_t size; std::int64_t slot; bool stack; bool used = false; };
    std::vector<Read>                  spareA, spareB;
    std::vector<std::pair<Read, Read>> crossed;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> crossedAt;   ///< (ours, vanilla) offsets of each crossed pair
    auto readOf = [](const Insn& i, const Frame& f) -> std::optional<Read> {
        if (hasEffect(i)) return std::nullopt;
        for (std::uint8_t k = 0; k < i.ins.operand_count_visible; ++k) {
            const auto& op = i.ops[k];
            if (op.type != ZYDIS_OPERAND_TYPE_MEMORY || op.mem.type != ZYDIS_MEMOP_TYPE_MEM || op.mem.base == ZYDIS_REGISTER_RIP) continue;
            const auto slot = slotOf(op, i, f);
            return Read{ i.ins.mnemonic, op.size, slot ? *slot : op.mem.disp.value, slot.has_value() };
        }
        return std::nullopt;
    };
    auto sameRead = [](const Read& p, const Read& q) {
        return p.mnemonic == q.mnemonic && p.size == q.size && p.slot == q.slot && p.stack == q.stack;
    };
    // The 32-byte home area above the return address belongs to the callee (x64 ABI): one build spills a temporary
    // there, another keeps it in a register. A store into it is private scratch unless the area's address escapes —
    // a `lea` of it into an argument register.
    auto homeSlot = [](const ZydisDecodedOperand& op, const Insn& i, const Frame& f) {
        const auto slot = slotOf(op, i, f);
        return slot && *slot >= 8 && *slot < 0x28;
    };
    auto homeEscapes = [&](const std::vector<Insn>& all, const Frame& f) {
        for (const auto& i : all) {
            if (i.ins.mnemonic != ZYDIS_MNEMONIC_LEA || !homeSlot(i.ops[1], i, f)) continue;
            switch (enclosing(i.ops[0].reg.value)) {
                case ZYDIS_REGISTER_RCX: case ZYDIS_REGISTER_RDX: case ZYDIS_REGISTER_R8: case ZYDIS_REGISTER_R9: return true;
                default: break;
            }
        }
        return false;
    };
    const bool homeEscapesA = homeEscapes(a, frameA);
    const bool homeEscapesB = homeEscapes(b, frameB);
    auto       privateScratch = [&](const Insn& i, const Frame& f, bool escapes) {
        const auto* w = writtenMemory(i);
        return !escapes && w && i.ins.mnemonic != ZYDIS_MNEMONIC_CALL && homeSlot(*w, i, f);
    };
    // A materialized null test: `test R, R; setz/setnz D` whose D is later self-tested and branched on. One build keeps
    // a loop-invariant pointer test as a bool in a register, another re-tests the pointer; the branch on D is what
    // pairs with the other side's test, so the setcc itself adds no condition. Narrow on purpose: zero tests only.
    auto selfTest = [](const Insn& i) {
        return i.ins.mnemonic == ZYDIS_MNEMONIC_TEST && i.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
               i.ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER && i.ops[0].reg.value == i.ops[1].reg.value;
    };
    auto materializedNullTest = [&](const std::vector<Insn>& side, std::size_t k) {
        const Insn& s = side[k];
        if ((s.ins.mnemonic != ZYDIS_MNEMONIC_SETZ && s.ins.mnemonic != ZYDIS_MNEMONIC_SETNZ) || s.ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER ||
            k == 0 || !selfTest(side[k - 1]))
            return false;
        for (std::size_t j = k + 1; j + 1 < side.size(); ++j)
            if (selfTest(side[j]) && side[j].ops[0].reg.value == s.ops[0].reg.value && side[j + 1].ins.meta.category == ZYDIS_CATEGORY_COND_BR)
                return true;
        return false;
    };

    for (const auto& [ia, ib] : alignInstructions(liveA, liveB)) {
        if (ia >= 0 && ib >= 0) {
            const auto& x = liveA[std::size_t(ia)];
            const auto& y = liveB[std::size_t(ib)];
            const bool  masked = isMasked(ours, x);
            if (x.ins.mnemonic == ZYDIS_MNEMONIC_NOP) { ++aligned; continue; }   // alignment padding: its encoding is filler
            // Which callee-saved register a value lives in picks the slot it is saved to and restored from.
            if (isSaveOrRestore(x, savesA, frameA) && isSaveOrRestore(y, savesB, frameB)) { ++aligned; continue; }
            // The frame size follows from how many callee-saved registers and spill slots a build needed.
            auto frameAdjust = [](const Insn& i) {
                return (i.ins.mnemonic == ZYDIS_MNEMONIC_SUB || i.ins.mnemonic == ZYDIS_MNEMONIC_ADD) && i.ins.operand_count_visible == 2 &&
                       i.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && i.ops[0].reg.value == ZYDIS_REGISTER_RSP &&
                       i.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
            };
            if (frameAdjust(x) && frameAdjust(y) && x.ins.mnemonic == y.ins.mnemonic) { ++aligned; continue; }
            // Two stores aligned crosswise (a build wrote the fields in the other order): match them as a set within
            // the stretch between calls, like any unpaired store, instead of failing on the pair.
            if (const auto* wx = writtenMemory(x), *wy = writtenMemory(y);
                wx && wy && x.ins.mnemonic == y.ins.mnemonic && x.ins.mnemonic != ZYDIS_MNEMONIC_CALL && !operandsMatch(*wx, *wy, masked)) {
                effectsA.push_back({ &x, segment });
                effectsB.push_back({ &y, segment });
                continue;
            }
            bool readsDiffer = false;
            for (std::uint8_t o = 0; o < x.ins.operand_count_visible; ++o) {
                const auto& ox = x.ops[o];
                const auto& oy = y.ops[o];
                // A read-only memory operand may take another address form after strength reduction ([r9+rcx] for
                // [rcx]); its displacement still has to match, which is what pins the field being read.
                const bool readOnly = ox.type == ZYDIS_OPERAND_TYPE_MEMORY && oy.type == ZYDIS_OPERAND_TYPE_MEMORY &&
                                      ox.mem.type == ZYDIS_MEMOP_TYPE_MEM && !(ox.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) &&
                                      !(oy.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) && ox.size == oy.size &&
                                      (ox.mem.base == ZYDIS_REGISTER_RIP) == (oy.mem.base == ZYDIS_REGISTER_RIP);
                if (readOnly && (masked || ox.mem.disp.value == oy.mem.disp.value)) continue;
                if (readOnly && ox.mem.base != ZYDIS_REGISTER_RIP && !hasEffect(x) && !hasEffect(y)) { readsDiffer = true; continue; }
                const bool relative = ox.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ox.imm.is_relative;
                if (!operandsMatch(ox, oy, masked || relative)) {
                    why = "operands differ beyond registers at ours +0x" + hex(x.off) + " / vanilla +0x" + hex(y.off);
                    return false;
                }
            }
            if (readsDiffer) {
                crossed.emplace_back(*readOf(x, frameA), *readOf(y, frameB));
                crossedAt.emplace_back(x.off, y.off);
                ++glue;
                continue;
            }
            ++aligned;
            if (x.ins.mnemonic == ZYDIS_MNEMONIC_CALL) ++segment;
        } else if (ia >= 0) {
            const Insn& x = liveA[std::size_t(ia)];
            if (const char* fam = conditionFamily(x.ins.mnemonic); fam && !materializedNullTest(liveA, std::size_t(ia))) ++conditions[fam].first;
            if (isFloatArithmetic(x.ins.mnemonic)) ++floatOps[int(x.ins.mnemonic)];
            if (const auto r = readOf(x, frameA)) spareA.push_back(*r);
            if (hasEffect(x) && x.off >= prologueA && !isSaveOrRestore(x, savesA, frameA) && !privateScratch(x, frameA, homeEscapesA))
                effectsA.push_back({ &x, segment });
            else
                ++glue;
        } else {
            const Insn& y = liveB[std::size_t(ib)];
            if (const char* fam = conditionFamily(y.ins.mnemonic); fam && !materializedNullTest(liveB, std::size_t(ib))) ++conditions[fam].second;
            if (isFloatArithmetic(y.ins.mnemonic)) --floatOps[int(y.ins.mnemonic)];
            if (const auto r = readOf(y, frameB)) spareB.push_back(*r);
            if (hasEffect(y) && y.off >= prologueB && !isSaveOrRestore(y, savesB, frameB) && !privateScratch(y, frameB, homeEscapesB))
                effectsB.push_back({ &y, segment });
            else
                ++glue;
        }
    }
    // Crossed pairs vouch for each other: a pair of loads swapped twice is two crossed pairs whose reads balance.
    for (const auto& [rx, ry] : crossed) {
        spareA.push_back(rx);
        spareB.push_back(ry);
    }
    for (std::size_t k = 0; k < crossed.size(); ++k) {
        const auto& [rx, ry] = crossed[k];
        auto take = [&](std::vector<Read>& spare, const Read& want) {
            const auto it = std::find_if(spare.begin(), spare.end(), [&](const Read& s) { return !s.used && sameRead(s, want); });
            if (it == spare.end()) return false;
            it->used = true;
            return true;
        };
        if (!take(spareB, rx) || !take(spareA, ry)) {
            why = "ours +0x" + hex(crossedAt[k].first) + " reads another field than vanilla +0x" + hex(crossedAt[k].second) +
                  ", and no unpaired read on the other side makes it a reordering";
            return false;
        }
    }
    for (const auto& [mnemonic, balance] : floatOps) {
        if (balance != 0) {
            why = std::string("unpaired ") + ZydisMnemonicGetString(ZydisMnemonic(mnemonic)) + " don't balance (ours " +
                  (balance > 0 ? "+" : "") + std::to_string(balance) + ") - the arithmetic changed, not just its schedule";
            return false;
        }
    }
    for (const auto& [fam, counts] : conditions) {
        if ((counts.first > 0) != (counts.second > 0)) {
            why = "an unpaired '" + fam + "' condition appears only in " + (counts.first > 0 ? "ours" : "vanilla") +
                  " - a tested condition changed, not just branch layout";
            return false;
        }
    }
    if (effectsA.size() != effectsB.size()) {
        why = std::to_string(effectsA.size()) + " vs " + std::to_string(effectsB.size()) + " unpaired calls/stores";
        return false;
    }
    for (auto& ea : effectsA) {
        const bool masked = isMasked(ours, *ea.insn);
        auto       hit    = std::find_if(effectsB.begin(), effectsB.end(), [&](const Effect& eb) {
            return !eb.used && eb.segment == ea.segment && sameEffect(*ea.insn, *eb.insn, masked);
        });
        if (hit == effectsB.end()) {
            why = "no matching call/store in vanilla for ours +0x" + hex(ea.insn->off);
            return false;
        }
        hit->used = true;
    }
    why = std::to_string(aligned) + " aligned, " + std::to_string(glue) + " unpaired side-effect-free, " +
          std::to_string(effectsA.size()) + " store(s) with a different source";
    if (deadCount) why += ", " + std::to_string(deadCount) + " provably dead";
    return true;
}

/// @brief The absolute address an instruction's relative branch or RIP-relative operand points at.
std::optional<std::uint64_t> absoluteTarget(const Insn& i, std::uint64_t runtimeAddress)
{
    for (std::uint8_t k = 0; k < i.ins.operand_count_visible; ++k) {
        const auto& op = i.ops[k];
        const bool  rel = (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative) ||
                         (op.type == ZYDIS_OPERAND_TYPE_MEMORY && op.mem.base == ZYDIS_REGISTER_RIP);
        ZyanU64 abs = 0;
        if (rel && ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&i.ins, &op, runtimeAddress, &abs))) return abs;
    }
    return std::nullopt;
}

/// @brief Checks every relocation in our function against where vanilla's matching instruction points, using
///        engine.yaml's `cpp` names: masked bytes say nothing about WHAT is referenced, so without this a call to the
///        wrong engine function (or a load of the wrong constant) would still match.
/// @brief Checks a reference that carries its own meaning: a compiler constant (`__real@3f800000`, `__xmm@<32 hex>`,
///        named for its bytes) against vanilla's bytes at the target, an import (`__imp_X`) against the exe's import
///        table. nullopt = not such a reference.
std::optional<std::string> selfCheck(const std::string& name, std::uint64_t target, const PeImage& pe)
{
    for (const char* prefix : { "__real@", "__xmm@" }) {
        if (name.rfind(prefix, 0) != 0) continue;
        const std::string hexBytes = name.substr(std::strlen(prefix));
        const std::size_t n        = hexBytes.size() / 2;
        const auto        off      = pe.fileOffset(static_cast<std::uint32_t>(target - pe.imageBase), n);
        if (!off) return "constant " + name + ": vanilla target 0x" + hex(target) + " is not in the file image";
        for (std::size_t k = 0; k < n; ++k) {   // the name spells the value most-significant byte first
            const auto expect = static_cast<std::uint8_t>(std::stoul(hexBytes.substr((n - 1 - k) * 2, 2), nullptr, 16));
            if (pe.bytes[*off + k] != expect) return "constant " + name + " differs from vanilla's bytes at 0x" + hex(target);
        }
        return std::string();
    }
    if (name.rfind("__imp_", 0) == 0) {
        const auto it = pe.importsBySlot.find(target);
        if (it == pe.importsBySlot.end()) return "import " + name + ": vanilla 0x" + hex(target) + " is not an import slot";
        if (it->second != name.substr(6)) return "import " + name + ": vanilla calls " + it->second;
        return std::string();
    }
    return std::nullopt;
}

void verifyReferences(Match& m, const CoffFunction& ours, const std::vector<Insn>& a, const std::vector<Insn>& b, std::uint64_t va,
                      const EngineSymbols& syms, const PeImage& pe)
{
    m.refsChecked = true;
    for (const auto& [ia, ib] : alignInstructions(a, b)) {
        if (ia < 0) continue;
        const Insn& x = a[std::size_t(ia)];
        for (std::uint32_t o = x.off; o < x.off + x.ins.length; ++o) {
            const auto t = ours.targets.find(o);
            if (t == ours.targets.end()) continue;
            if (t->second.mangled.rfind("??_C@", 0) == 0 || t->second.name == "`string'") continue;   // string literals
            if (ib < 0) { ++m.refsUnverifiable; continue; }
            const Insn& y      = b[std::size_t(ib)];
            const auto  actual = absoluteTarget(y, va + y.off);
            if (actual) {
                if (const auto self = selfCheck(t->second.mangled, *actual, pe)) {
                    if (self->empty()) ++m.refsVerified;
                    else m.refsWrong.push_back(*self);
                    continue;
                }
            }
            auto it = syms.byCpp.find(t->second.name);
            if (it == syms.byCpp.end()) it = syms.byCpp.find(t->second.mangled);
            if (it != syms.byCpp.end()) {
                if (actual && *actual == it->second) {
                    ++m.refsVerified;
                } else {
                    m.refsWrong.push_back(t->second.name + " should be 0x" + hex(it->second) + " (" + syms.registryName.at(it->first) +
                                          ") but vanilla +0x" + hex(y.off) + " references " + (actual ? "0x" + hex(*actual) : std::string("nothing")));
                }
            } else if (actual) {
                const bool code = x.ins.mnemonic == ZYDIS_MNEMONIC_CALL || x.ins.mnemonic == ZYDIS_MNEMONIC_JMP;
                m.refsUnmapped.push_back({ t->second.name, t->second.mangled, *actual, code });
            } else {
                ++m.refsUnverifiable;
            }
        }
    }
    if (!m.refsWrong.empty()) {
        std::string why = "wrong reference(s): ";
        for (std::size_t k = 0; k < m.refsWrong.size(); ++k) why += (k ? "; " : "") + m.refsWrong[k];
        m.detail = why + (m.detail.empty() ? "" : "\n" + m.detail);
        m.status = Status::Structural;
    }
}

Match classify(const CoffFunction& ours, const Bytes& vanilla, std::uint64_t va, bool wantListing, const PeImage& pe);

Match compare(const CoffFunction& ours, const Bytes& vanilla, std::uint64_t va, bool wantListing, const EngineSymbols* syms, const PeImage& pe)
{
    Match m = classify(ours, vanilla, va, wantListing, pe);
    if (syms) {
        const auto a = disassemble(ours.bytes);
        const auto b = disassemble(vanilla);
        if (a && b) verifyReferences(m, ours, *a, *b, va, *syms, pe);
    }
    return m;
}

Match classify(const CoffFunction& ours, const Bytes& vanilla, std::uint64_t va, bool wantListing, const PeImage& pe)
{
    Match m;
    m.oursLen    = ours.bytes.size();
    m.vanillaLen = vanilla.size();
    const std::size_t common = std::min(m.oursLen, m.vanillaLen);
    std::size_t       same   = 0;
    for (std::size_t k = 0; k < common; ++k) {
        if (ours.mask[k] || ours.bytes[k] == vanilla[k]) ++same;
        else if (m.firstDiff < 0) m.firstDiff = static_cast<long long>(k);
    }
    if (m.firstDiff < 0 && m.oursLen != m.vanillaLen) m.firstDiff = static_cast<long long>(common);
    m.pct = 100.0 * double(same) / double(std::max<std::size_t>(std::max(m.oursLen, m.vanillaLen), 1));

    const auto a = disassemble(ours.bytes);
    const auto b = disassemble(vanilla);
    if (wantListing && a && b) buildListing(m, ours, *a, vanilla, *b, va);

    if (m.firstDiff < 0) { m.status = Status::Exact; return m; }
    m.status = Status::Structural;
    if (!a || !b) { m.detail = "undecodable bytes"; return m; }
    std::string regallocWhy;
    if (a->size() != b->size()) {
        regallocWhy = std::to_string(a->size()) + " vs " + std::to_string(b->size()) + " instructions";
    } else {
        for (std::size_t k = 0; k < a->size() && regallocWhy.empty(); ++k) {
            const auto& x  = (*a)[k];
            const auto& y  = (*b)[k];
            bool        ok = x.ins.mnemonic == y.ins.mnemonic && x.ins.length == y.ins.length &&
                      x.ins.operand_count_visible == y.ins.operand_count_visible;
            for (std::uint8_t o = 0; ok && o < x.ins.operand_count_visible; ++o) ok = operandsMatch(x.ops[o], y.ops[o], isMasked(ours, x));
            if (!ok) regallocWhy = "first structural difference at +0x" + hex(x.off);
        }
        if (regallocWhy.empty()) { m.status = Status::Regalloc; return m; }
    }
    std::string variantWhy;
    if (variantEquivalent(ours, *a, *b, va, pe, variantWhy)) {
        m.status = Status::Variant;
        m.detail = regallocWhy + "; " + variantWhy;
    } else {
        m.detail = regallocWhy + "; not VARIANT: " + variantWhy;
    }
    return m;
}

// ── driver ───────────────────────────────────────────────────────────────────────────────────────────────────────

void usage()
{
    std::puts(
        "cbmatch - compile reimplemented functions with the original toolchains and diff them against the game exe\n"
        "\n"
        "usage: cbmatch <manifest.yaml> [options]\n"
        "  --runtime <name>     which runtime's exe and addresses to use (default: the manifest's first)\n"
        "  --exe <path>         override that runtime's exe path\n"
        "  --only <text>        only functions whose name contains <text>\n"
        "  --show               print a side-by-side disassembly of every non-exact function\n"
        "  --show-all           print it for every function\n"
        "  --changes            like --show, but only the lines that differ\n"
        "  --report <file>      write the results as JSON\n"
        "  --work <dir>         where objects and listings go (default: %TEMP%/cbmatch/<manifest name>)\n"
        "  --jobs <n>           parallel compiles (default: hardware threads)\n"
        "  --allow-regalloc     exit 0 when every function is EXACT or REGALLOC\n"
        "  --allow-variant      exit 0 when every function is EXACT, REGALLOC or VARIANT\n"
        "  --suggest            print engine.yaml lines (with Address Library ids) for unmapped references\n"
        "\n"
        "manifest:\n"
        "  runtimes:   { ae: { exe: <exe>, addresslib: <versionlib-*.bin>, pins: ae } }   # or name: <exe>; ${VAR} = env\n"
        "  registry:   <engine.yaml>   # then every relocation is checked against the cpp: names there\n"
        "  toolchains: { vs2015: <cl.exe>, vs2017: <cl.exe> }   # every toolchain is tried; the best result wins\n"
        "  flags:      [/O2, /GS-, /fp:fast]\n"
        "  includes:   [dir, ...]                       # relative to the manifest\n"
        "  units:\n"
        "    - source: hkbTimerModifier.cpp             # relative to the manifest\n"
        "      defines: [NAME=1]                        # optional; also flags:, toolchains: [vs2015]\n"
        "      functions:\n"
        "        - { name: \"hkbTimerModifier::update\", va: { ae: 0x140afebd0 } }\n"
        "\n"
        "Statuses: EXACT (identical with relocated fields masked), REGALLOC (identical but for register choice),\n"
        "VARIANT (the same calls and memory writes in the same order at the same offsets and the same tested conditions;\n"
        "register choice, branch layout and side-effect-free instructions differ - what a different build of the same\n"
        "compiler produces; it does NOT check register dataflow, so a dropped or changed register computation such as a\n"
        "loop counter step passes), STRUCTURAL, MISSING (symbol or address not found), COMPILE-ERROR.\n"
        "With a registry, a relocation that points somewhere other than its engine.yaml address makes the function\n"
        "STRUCTURAL whatever its bytes say. Exit code 0 only when every function is EXACT.");
}

struct Options {
    fs::path    manifest, exe, work, report;
    std::string runtime, only;
    bool        show = false, showAll = false, changesOnly = false, allowRegalloc = false, allowVariant = false, suggest = false;
    unsigned    jobs = 0;
};

int run(const Options& opt)
{
    const Manifest m = loadManifest(opt.manifest);
    if (m.runtimes.empty() && opt.exe.empty()) throw std::runtime_error("no runtime: add runtimes: to the manifest or pass --exe");

    std::string        runtime = opt.runtime.empty() ? (m.runtimes.empty() ? std::string("default") : m.runtimes.front().name) : opt.runtime;
    const RuntimeSpec* rt      = nullptr;
    for (const auto& r : m.runtimes) if (r.name == runtime) rt = &r;
    fs::path exePath = opt.exe;
    if (exePath.empty()) {
        if (!rt) throw std::runtime_error("manifest has no runtime '" + runtime + "'");
        exePath = rt->exe;
    }
    const PeImage pe = loadPe(exePath);

    std::optional<EngineSymbols> syms;
    if (!m.registry.empty() && rt && !rt->addressLibrary.empty())
        syms = loadEngineSymbols(m.registry, rt->addressLibrary, rt->pins, pe.imageBase);

    const fs::path work = opt.work.empty() ? fs::temp_directory_path() / "cbmatch" / m.dir.filename() / opt.manifest.stem() : opt.work;

    struct Job { std::size_t unit; std::size_t toolchain; };
    std::vector<Job> jobs;
    for (std::size_t u = 0; u < m.units.size(); ++u)
        for (std::size_t t = 0; t < m.toolchains.size(); ++t) {
            const auto& allowed = m.units[u].toolchains;
            if (allowed.empty() || std::find(allowed.begin(), allowed.end(), m.toolchains[t].first) != allowed.end())
                jobs.push_back({ u, t });
        }

    std::vector<CompileOutput> compiled(jobs.size());
    CB::engine::exec::ParallelFor(jobs.size(), [&](std::size_t j) {
        const auto& job  = jobs[j];
        const auto& unit = m.units[job.unit];
        const auto  dir  = work / (std::to_string(job.unit) + "-" + unit.source.stem().string()) / m.toolchains[job.toolchain].first;
        compiled[j]      = compileUnit(m, unit, m.toolchains[job.toolchain].second, dir);
    }, { .threads = opt.jobs });

    struct Row { std::string name; std::uint64_t va = 0; Match best; std::vector<std::pair<std::string, Match>> all; };
    std::vector<Row> rows;
    bool             gatePassed = true;

    for (std::size_t u = 0; u < m.units.size(); ++u) {
        for (const auto& fn : m.units[u].functions) {
            if (!opt.only.empty() && fn.name.find(opt.only) == std::string::npos) continue;
            Row row;
            row.name = fn.name;
            const auto vaIt = fn.va.find(runtime);
            if (vaIt == fn.va.end()) { row.best.detail = "no address for runtime " + runtime; rows.push_back(std::move(row)); gatePassed = false; continue; }
            row.va = vaIt->second;
            const auto rva = static_cast<std::uint32_t>(row.va - pe.imageBase);
            const auto end = pe.functionEnd(rva);

            for (std::size_t j = 0; j < jobs.size(); ++j) {
                if (jobs[j].unit != u) continue;
                const std::string& tc = m.toolchains[jobs[j].toolchain].first;
                Match              mt;
                mt.toolchain = tc;
                if (!compiled[j].ok) {
                    mt.status = Status::CompileError;
                    mt.detail = std::string(CB::engine::text::Trim(compiled[j].log));
                } else {
                    const auto& fs_ = compiled[j].functions;
                    std::vector<const CoffFunction*> hits;
                    for (const auto& f : fs_) if (f.name == fn.name || f.mangled == fn.name) hits.push_back(&f);
                    if (hits.size() != 1) {
                        mt.detail = hits.empty() ? "symbol not found in the object" : "ambiguous name; give the mangled symbol";
                    } else {
                        const CoffFunction& ours = *hits.front();
                        const std::size_t   len  = end ? std::size_t(*end - rva) : ours.bytes.size();
                        const auto          off  = pe.fileOffset(rva, len);
                        if (!off) {
                            mt.detail = "address 0x" + hex(row.va) + " is not in the exe's file image";
                        } else {
                            Bytes vanilla(pe.bytes.begin() + *off, pe.bytes.begin() + *off + len);
                            mt = compare(ours, vanilla, row.va, opt.show || opt.showAll, syms ? &*syms : nullptr, pe);
                            mt.toolchain = tc;
                            if (!end) mt.detail += (mt.detail.empty() ? "" : "; ") + std::string("no .pdata entry, length assumed from ours");
                        }
                    }
                }
                row.all.emplace_back(tc, mt);
            }
            for (const auto& [tc, mt] : row.all) {
                const bool better = statusRank(mt.status) < statusRank(row.best.status) ||
                                    (statusRank(mt.status) == statusRank(row.best.status) && mt.pct > row.best.pct) ||
                                    row.best.toolchain.empty();
                if (better) row.best = mt;
            }
            const bool pass = row.best.status == Status::Exact ||
                              ((opt.allowRegalloc || opt.allowVariant) && row.best.status == Status::Regalloc) ||
                              (opt.allowVariant && row.best.status == Status::Variant);
            gatePassed      = gatePassed && pass;
            rows.push_back(std::move(row));
        }
    }

    std::printf("cbmatch: runtime %s, exe %s, work %s\n\n", runtime.c_str(), exePath.string().c_str(), work.string().c_str());
    std::printf("%-48s %-12s %-8s %-13s %6s  %s\n", "function", "va", "tc", "status", "match", "bytes ours/vanilla");
    for (const auto& r : rows) {
        const auto& b = r.best;
        std::printf("%-48s %-12s %-8s %-13s %5.1f%%  %zu/%zu", r.name.c_str(), hex(r.va).c_str(), b.toolchain.c_str(), statusName(b.status), b.pct, b.oursLen, b.vanillaLen);
        if (r.all.size() > 1) {
            std::printf("   [");
            for (std::size_t k = 0; k < r.all.size(); ++k)
                std::printf("%s%s %s %.1f%%", k ? ", " : "", r.all[k].first.c_str(), statusName(r.all[k].second.status), r.all[k].second.pct);
            std::printf("]");
        }
        std::printf("\n");
        if (!b.detail.empty() && b.status != Status::Exact) {
            for (const auto line : CB::engine::text::SplitLines(b.detail)) std::printf("    %.*s\n", int(line.size()), line.data());
        }
        if (b.refsChecked) {
            std::printf("    references: %zu verified, %zu unmapped, %zu unverifiable", b.refsVerified, b.refsUnmapped.size(), b.refsUnverifiable);
            if (!b.refsUnmapped.empty()) {
                std::printf(" (");
                std::size_t shown = 0;
                for (const auto& u : b.refsUnmapped) {
                    if (shown++ == 4) { std::printf(", ..."); break; }
                    std::printf("%s%s", shown > 1 ? ", " : "", u.name.c_str());
                }
                std::printf(")");
            }
            std::printf("\n");
        }
        if ((opt.showAll || (opt.show && b.status != Status::Exact)) && !b.listing.empty()) {
            std::printf("    %-63s | vanilla 0x%s\n", ("ours (" + b.toolchain + ")").c_str(), hex(r.va).c_str());
            for (const auto& line : b.listing)
                if (!opt.changesOnly || line[0] != ' ') std::printf("    %s\n", line.c_str());
            std::printf("\n");
        }
    }

    if (opt.suggest && syms) {
        std::map<std::string, const UnmappedRef*> unique;
        for (const auto& r : rows)
            for (const auto& u : r.best.refsUnmapped) unique.emplace(u.name, &u);
        std::printf("\n# engine.yaml lines for the unmapped references (%s pins; other runtimes 0 until verified):\n", rt->pins.c_str());
        for (const auto& [name, u] : unique) {
            const auto        id    = syms->idByOffset.find(u->vanillaTarget - pe.imageBase);
            const std::string ident = [&] {
                std::string s;
                for (char c : name) s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
                while (s.find("__") != std::string::npos) s.erase(s.find("__"), 1);
                return s;
            }();
            const std::string pin = id == syms->idByOffset.end() ? std::string("?") : std::to_string(id->second);
            std::printf("  - { name: %s, kind: %s, se: 0, ae: %s, vr: 0, cpp: \"%s\" }  # vanilla 0x%s%s\n", ident.c_str(),
                        u->code ? "func" : "data", rt->pins == "ae" ? pin.c_str() : "0", name.c_str(), hex(u->vanillaTarget).c_str(),
                        id == syms->idByOffset.end() ? " - no exact Address Library id (mid-object?)" : "");
        }
    }

    if (!opt.report.empty()) {
        nlohmann::json j;
        j["runtime"] = runtime;
        j["exe"]     = exePath.string();
        for (const auto& r : rows) {
            nlohmann::json f;
            f["name"]      = r.name;
            f["va"]        = "0x" + hex(r.va);
            f["status"]    = statusName(r.best.status);
            f["toolchain"] = r.best.toolchain;
            f["match"]     = r.best.pct;
            f["oursLen"]   = r.best.oursLen;
            f["vanillaLen"] = r.best.vanillaLen;
            f["detail"]    = r.best.detail;
            for (const auto& [tc, mt] : r.all) f["toolchains"][tc] = { { "status", statusName(mt.status) }, { "match", mt.pct } };
            j["functions"].push_back(std::move(f));
        }
        std::string err;
        if (!CB::engine::disk::WriteFile(opt.report, j.dump(2), {}, &err)) throw std::runtime_error("cannot write report: " + err);
    }
    return gatePassed ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "cbmatch: %s needs a value\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--runtime") opt.runtime = next();
        else if (a == "--exe") opt.exe = next();
        else if (a == "--only") opt.only = next();
        else if (a == "--show") opt.show = true;
        else if (a == "--show-all") opt.showAll = true;
        else if (a == "--changes") { opt.show = true; opt.changesOnly = true; }
        else if (a == "--report") opt.report = next();
        else if (a == "--work") opt.work = next();
        else if (a == "--jobs") opt.jobs = static_cast<unsigned>(std::stoul(next()));
        else if (a == "--allow-regalloc") opt.allowRegalloc = true;
        else if (a == "--allow-variant") opt.allowVariant = true;
        else if (a == "--suggest") opt.suggest = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "cbmatch: unknown option %s\n", a.c_str()); return 2; }
        else if (opt.manifest.empty()) opt.manifest = a;
        else { std::fprintf(stderr, "cbmatch: more than one manifest given\n"); return 2; }
    }
    if (opt.manifest.empty()) { usage(); return 2; }
    try {
        return run(opt);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cbmatch: %s\n", e.what());
        return 2;
    }
}
