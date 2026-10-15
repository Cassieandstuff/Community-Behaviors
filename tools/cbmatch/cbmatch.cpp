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
#include <unicorn/unicorn.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
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

/// @brief Which of the exe's classes the inventory covers, and where it and the parked ledger live.
struct InventorySpec {
    fs::path                 file;      ///< the generated inventory; empty = no inventory
    fs::path                 parked;    ///< hand-kept functions set aside, with why; optional
    fs::path                 schema;    ///< every class with a YAML descriptor under it is in scope; optional
    std::vector<std::string> include;   ///< RTTI class-name globs in scope
    std::vector<std::string> exclude;   ///< globs taken out again (deferred families)
};

struct Manifest {
    fs::path                                     dir;
    std::vector<RuntimeSpec>                     runtimes;     ///< in file order; the first is the default
    fs::path                                     registry;     ///< engine.yaml; empty = no reference checking
    std::vector<std::pair<std::string, fs::path>> toolchains;   ///< name → cl.exe, in file order
    std::vector<std::string>                     flags;
    std::vector<fs::path>                        includes;
    std::vector<UnitSpec>                        units;
    InventorySpec                                inventory;
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
    if (has(root, "inventory")) {
        const auto inv = root["inventory"];
        if (has(inv, "file")) m.inventory.file = resolve(val(inv["file"]));
        if (has(inv, "parked")) m.inventory.parked = resolve(val(inv["parked"]));
        if (has(inv, "schema")) m.inventory.schema = resolve(val(inv["schema"]));
        m.inventory.include = strSeq(inv, "include");
        m.inventory.exclude = strSeq(inv, "exclude");
    }

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
    struct Section {
        std::uint32_t va, vsize, raw, rawSize, characteristics;
        bool          executable() const { return (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0; }
    };

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
        pe.sections.push_back({ rd<std::uint32_t>(b, sh + 12), rd<std::uint32_t>(b, sh + 8), rd<std::uint32_t>(b, sh + 20), rd<std::uint32_t>(b, sh + 16),
                                rd<std::uint32_t>(b, sh + 36) });

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
    struct Target {
        std::string                 mangled, name;
        std::uint16_t               type = 0;
        std::optional<std::int64_t> local;   ///< the referenced offset within this function, when it points into it
        /// @brief The offset into the symbol the reference addresses (COFF keeps it in the patched field): a member of
        ///        a constant object, `symbol+0x10`.
        std::int64_t                addend = 0;
    };
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

    std::vector<std::string>  names(nSymbols);
    std::vector<std::int16_t> symSection(nSymbols, 0);
    std::vector<std::uint32_t> symValue(nSymbols, 0);
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
        symSection[i]       = section;
        symValue[i]         = value;
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
                if (sym >= names.size()) continue;
                CoffFunction::Target t{ names[sym], undecorate(names[sym]), rd<std::uint16_t>(b, ro + 8), std::nullopt };
                if (width == 4) t.addend = rd<std::int32_t>(b, rawPtr + at);
                else if (width == 8) t.addend = rd<std::int64_t>(b, rawPtr + at);
                // A reference into this same section (a jump table, its entries): resolve it to a function offset. COFF
                // keeps the addend in the field itself.
                if (symSection[sym] == section && width == 4) {
                    const std::int64_t target = std::int64_t(symValue[sym]) + rd<std::int32_t>(b, rawPtr + at);
                    if (target >= begin && target < end) t.local = target - begin;
                }
                f.targets[at - begin] = std::move(t);
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
        case ZYDIS_OPERAND_TYPE_MEMORY: {
            // In 64-bit mode SS and DS are both flat; which one an address implies follows from its base register
            // (rbp/rsp → SS), so it is register choice. fs/gs (thread data) stay distinct.
            auto flat = [](ZydisRegister s) { return s == ZYDIS_REGISTER_SS ? ZYDIS_REGISTER_DS : s; };
            if (a.mem.type != b.mem.type || a.mem.scale != b.mem.scale || flat(a.mem.segment) != flat(b.mem.segment)) return false;
            if ((a.mem.base == ZYDIS_REGISTER_NONE) != (b.mem.base == ZYDIS_REGISTER_NONE)) return false;
            if ((a.mem.index == ZYDIS_REGISTER_NONE) != (b.mem.index == ZYDIS_REGISTER_NONE)) return false;
            if ((a.mem.base == ZYDIS_REGISTER_RIP) != (b.mem.base == ZYDIS_REGISTER_RIP)) return false;
            if (a.mem.base != ZYDIS_REGISTER_NONE && !sameRegClass(a.mem.base, b.mem.base)) return false;
            if (a.mem.index != ZYDIS_REGISTER_NONE && !sameRegClass(a.mem.index, b.mem.index)) return false;
            return masked || a.mem.disp.value == b.mem.disp.value;
        }
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

    const CoffFunction*      ours = nullptr;   ///< the compiled function this result is for (outlives the Match)
    bool                     needsDiff = false;   ///< VARIANT only through rule A or E: --diff must confirm it
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
/// @note r11 is a frame base wherever its value is known relative to the entry rsp: set by `mov r11, rsp` (the
///       prologue) or `lea r11, [rsp+k]` (an epilogue re-deriving it); any other write ends that until the next such
///       set. After an unconditional exit (ret / tail jmp) depth and r11 reset to the body's: the following code still
///       has the frame.
struct Frame {
    std::map<std::uint32_t, std::int64_t> depth;
    std::map<std::uint32_t, std::int64_t> r11;   ///< r11's value relative to the entry rsp, where known
    std::map<std::uint32_t, std::int64_t> rax;   ///< likewise rax (`mov rax, rsp` opens many prologues; saves go through it)
};

/// @brief Whether `i` writes `reg` (or a register it encloses).
bool writesRegister(const Insn& i, ZydisRegister reg)
{
    for (std::uint8_t o = 0; o < i.ins.operand_count; ++o)
        if (i.ops[o].type == ZYDIS_OPERAND_TYPE_REGISTER && enclosing(i.ops[o].reg.value) == enclosing(reg) &&
            (i.ops[o].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
            return true;
    return false;
}

Frame frameOf(const std::vector<Insn>& all)
{
    Frame                       f;
    std::int64_t                depth = 0, body = 0;
    std::optional<std::int64_t> r11, bodyR11, rax;
    bool                        inPrologue = true;
    // A register set to (rsp + k) holds a known entry-relative value until anything else writes it.
    auto track = [&](const Insn& i, ZydisRegister reg, std::optional<std::int64_t>& value) {
        if (!writesRegister(i, reg)) return;
        const auto& d  = i.ops[0];
        const auto& s  = i.ops[1];
        const bool  to = d.type == ZYDIS_OPERAND_TYPE_REGISTER && d.reg.value == reg;
        if (to && i.ins.mnemonic == ZYDIS_MNEMONIC_MOV && s.type == ZYDIS_OPERAND_TYPE_REGISTER && s.reg.value == ZYDIS_REGISTER_RSP)
            value = -depth;
        else if (to && i.ins.mnemonic == ZYDIS_MNEMONIC_LEA && s.mem.base == ZYDIS_REGISTER_RSP && s.mem.index == ZYDIS_REGISTER_NONE)
            value = s.mem.disp.value - depth;
        else
            value.reset();
    };
    for (const auto& i : all) {
        f.depth[i.off] = depth;
        if (r11) f.r11[i.off] = *r11;
        if (rax) f.rax[i.off] = *rax;
        track(i, ZYDIS_REGISTER_R11, r11);
        track(i, ZYDIS_REGISTER_RAX, rax);
        const auto& op0  = i.ops[0];
        const bool  rsp0 = op0.type == ZYDIS_OPERAND_TYPE_REGISTER && op0.reg.value == ZYDIS_REGISTER_RSP;
        const auto  imm  = i.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE ? std::int64_t(i.ops[1].imm.value.s) : 0;
        switch (i.ins.mnemonic) {
            case ZYDIS_MNEMONIC_PUSH: depth += 8; break;
            case ZYDIS_MNEMONIC_POP:  depth -= 8; break;
            case ZYDIS_MNEMONIC_SUB:  if (rsp0) { depth += imm; if (inPrologue) { body = depth; bodyR11 = r11; inPrologue = false; } } break;
            case ZYDIS_MNEMONIC_ADD:  if (rsp0) depth -= imm; break;
            case ZYDIS_MNEMONIC_RET:  depth = body; r11 = bodyR11; rax.reset(); break;
            case ZYDIS_MNEMONIC_JMP:  if (!inPrologue && depth != body) { depth = body; r11 = bodyR11; rax.reset(); } break;
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
    if (op.mem.base == ZYDIS_REGISTER_R11) {
        const auto it = f.r11.find(i.off);
        if (it != f.r11.end()) return it->second + op.mem.disp.value;
    }
    if (op.mem.base == ZYDIS_REGISTER_RAX) {
        const auto it = f.rax.find(i.off);
        if (it != f.rax.end()) return it->second + op.mem.disp.value;
    }
    return std::nullopt;
}

/// @brief Rewrites a frame-pointer function's `[rbp+d]` operands as the `[rsp+d']` they address, so a build that keeps
///        rbp as a frame base compares with one that addresses the same slots from rsp.
/// @note Only when the prologue sets rbp from the stack once (`mov rbp, rsp`, `lea rbp, [rsp+k]`, or `lea rbp, [rax+k]`
///       after an opening `mov rax, rsp`) and every later write of rbp is a `pop rbp` (the epilogue's restore): then
///       rbp is purely an addressing base. Any other write leaves the function untouched.
void canonicalizeFrameBase(std::vector<Insn>& insns)
{
    const Frame                 f = frameOf(insns);
    std::optional<std::size_t>  at;
    std::int64_t                base = 0;   ///< rbp relative to the entry rsp
    const bool raxIsEntry = !insns.empty() && insns[0].ins.mnemonic == ZYDIS_MNEMONIC_MOV && insns[0].ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                            insns[0].ops[0].reg.value == ZYDIS_REGISTER_RAX && insns[0].ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                            insns[0].ops[1].reg.value == ZYDIS_REGISTER_RSP;
    bool raxStillEntry = raxIsEntry;
    for (std::size_t k = 0; k < insns.size(); ++k) {
        const Insn& i = insns[k];
        if (k > 0 && writesRegister(i, ZYDIS_REGISTER_RAX)) raxStillEntry = false;
        if (!writesRegister(i, ZYDIS_REGISTER_RBP)) continue;
        if (i.ins.mnemonic == ZYDIS_MNEMONIC_PUSH) continue;   // push rbp reads it
        if (at) {
            if (i.ins.mnemonic != ZYDIS_MNEMONIC_POP) return;   // rbp rewritten: not a plain frame base
            continue;
        }
        const auto& d = i.ops[0];
        const auto& s = i.ops[1];
        if (d.type != ZYDIS_OPERAND_TYPE_REGISTER || d.reg.value != ZYDIS_REGISTER_RBP) return;
        if (i.ins.mnemonic == ZYDIS_MNEMONIC_MOV && s.type == ZYDIS_OPERAND_TYPE_REGISTER && s.reg.value == ZYDIS_REGISTER_RSP)
            base = -f.depth.at(i.off);
        else if (i.ins.mnemonic == ZYDIS_MNEMONIC_LEA && s.mem.index == ZYDIS_REGISTER_NONE && s.mem.base == ZYDIS_REGISTER_RSP)
            base = s.mem.disp.value - f.depth.at(i.off);
        else if (i.ins.mnemonic == ZYDIS_MNEMONIC_LEA && s.mem.index == ZYDIS_REGISTER_NONE && s.mem.base == ZYDIS_REGISTER_RAX && raxStillEntry)
            base = s.mem.disp.value;
        else
            return;
        at = k;
    }
    if (!at) return;
    for (std::size_t k = *at + 1; k < insns.size(); ++k) {
        Insn& i = insns[k];
        for (std::uint8_t o = 0; o < i.ins.operand_count; ++o) {
            auto& op = i.ops[o];
            if (op.type != ZYDIS_OPERAND_TYPE_MEMORY || op.mem.base != ZYDIS_REGISTER_RBP) continue;
            op.mem.base       = ZYDIS_REGISTER_RSP;
            op.mem.disp.value = base + op.mem.disp.value + f.depth.at(i.off);
            op.mem.disp.has_displacement = ZYAN_TRUE;
        }
    }
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
/// @note A push/pop of a callee-saved register counts too: how many registers a build needs (so which it pushes, and
///       where the rest of its saves land) is register allocation. Restoring the caller's values is checked by --diff.
bool isSaveOrRestore(const Insn& i, const SaveSlots& saves, const Frame& f)
{
    if ((i.ins.mnemonic == ZYDIS_MNEMONIC_PUSH || i.ins.mnemonic == ZYDIS_MNEMONIC_POP) && i.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER)
        return isCalleeSaved(i.ops[0].reg.value);
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

/// @brief An upper bound on a register's value at instruction `k` when it is provably non-negative there, found by
///        walking back to its definition in straight-line code: an `and` with a sign-clear mask, `movzx`, a non-negative
///        `mov` immediate, `xor r, r`, or the doubling (`add r, r` / `lea r, [a+a]`) of such a value. nullopt: unknown.
std::optional<std::uint64_t> nonNegativeBound(const std::vector<Insn>& side, std::size_t k, ZydisRegister reg,
                                              const std::set<std::uint64_t>& targets, int depth = 0)
{
    if (depth > 4) return std::nullopt;
    for (std::size_t j = k; j-- > 0;) {
        const Insn& i = side[j];
        const auto  cat = i.ins.meta.category;
        if (targets.count(side[j + 1].off) || cat == ZYDIS_CATEGORY_UNCOND_BR || cat == ZYDIS_CATEGORY_CALL || cat == ZYDIS_CATEGORY_RET)
            return std::nullopt;   // another path may join, or a call clobbers: only straight-line (fall-through) code counts
        if (!writesRegister(i, reg)) continue;
        const auto& d = i.ops[0];
        const auto& s = i.ops[1];
        if (d.type != ZYDIS_OPERAND_TYPE_REGISTER || enclosing(d.reg.value) != enclosing(reg)) return std::nullopt;
        const std::uint64_t signBit = d.size ? (1ull << (d.size - 1)) : 0;
        switch (i.ins.mnemonic) {
            case ZYDIS_MNEMONIC_AND:
                if (s.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && !(s.imm.value.u & signBit)) return s.imm.value.u & (signBit - 1);
                return std::nullopt;
            case ZYDIS_MNEMONIC_MOVZX:
                return (1ull << s.size) - 1;
            case ZYDIS_MNEMONIC_MOV:
                if (s.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && !(s.imm.value.u & signBit)) return s.imm.value.u;
                return std::nullopt;
            case ZYDIS_MNEMONIC_XOR:
                if (s.type == ZYDIS_OPERAND_TYPE_REGISTER && s.reg.value == d.reg.value) return 0;
                return std::nullopt;
            case ZYDIS_MNEMONIC_ADD:
                if (s.type == ZYDIS_OPERAND_TYPE_REGISTER && s.reg.value == d.reg.value)
                    if (const auto b = nonNegativeBound(side, j, reg, targets, depth + 1); b && 2 * *b < signBit) return 2 * *b;
                return std::nullopt;
            case ZYDIS_MNEMONIC_LEA:
                if (s.mem.base != ZYDIS_REGISTER_NONE && s.mem.base == s.mem.index && s.mem.scale == 1 && s.mem.disp.value == 0)
                    if (const auto b = nonNegativeBound(side, j, s.mem.base, targets, depth + 1); b && 2 * *b < signBit) return 2 * *b;
                return std::nullopt;
            default:
                return std::nullopt;
        }
    }
    return std::nullopt;
}

/// @brief The family of the condition at `k`, with a signed relation (l, le) folded onto its unsigned counterpart
///        (b, be) when the compare it reads has two provably non-negative operands within the signed range: then both
///        orderings agree, and which one a compiler emits is its own choice (VS2015 24247 picks unsigned where vanilla's
///        build kept Havok's signed compare, e.g. hkArray::setSize's capacity test).
const char* foldedConditionFamily(const std::vector<Insn>& side, std::size_t k, const std::set<std::uint64_t>& targets)
{
    const char* fam = conditionFamily(side[k].ins.mnemonic);
    if (!fam || (std::strcmp(fam, "l") != 0 && std::strcmp(fam, "le") != 0)) return fam;
    for (std::size_t j = k; j-- > 0;) {
        const Insn& c   = side[j];
        const auto* fl  = c.ins.cpu_flags;
        const bool  wrf = fl && (fl->modified | fl->set_0 | fl->set_1 | fl->undefined) != 0;
        const auto  cat = c.ins.meta.category;
        if (targets.count(side[j + 1].off) || cat == ZYDIS_CATEGORY_UNCOND_BR || cat == ZYDIS_CATEGORY_CALL || cat == ZYDIS_CATEGORY_RET)
            return fam;
        if (!wrf) continue;
        if (c.ins.mnemonic != ZYDIS_MNEMONIC_CMP || c.ins.operand_count_visible != 2) return fam;
        auto bounded = [&](const ZydisDecodedOperand& op) -> bool {
            const std::uint64_t signBit = op.size ? (1ull << (op.size - 1)) : 0;
            if (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) return !(op.imm.value.u & signBit);
            if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) return nonNegativeBound(side, j, op.reg.value, targets).has_value();
            return false;
        };
        if (bounded(c.ops[0]) && bounded(c.ops[1])) return std::strcmp(fam, "l") == 0 ? "b" : "be";
        return fam;
    }
    return fam;
}

/// @brief VARIANT: the same calls, returns, stack traffic and memory writes in the same order, at the same data
///        offsets; only register choice, branch layout (tail merging/duplication, cmov vs branch) and side-effect-free
///        instructions (loads, compares, register moves) may differ. The signature of a different build of the same
///        compiler compiling the same source.
/// @warning Weaker than EXACT/REGALLOC: it checks effects (calls, stores) and tested conditions, not register dataflow.
///          NOT caught: a changed or dropped register computation (e.g. a loop's `i--` removed, two register arguments
///          swapped — both verified to pass), a different value loaded into a register (a changed constant the build
///          already holds in another register needs no load — verified to pass), a condition change hidden by a
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
                       const PeImage& pe, std::string& why, bool& needsDiff)
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
    canonicalizeFrameBase(a);
    canonicalizeFrameBase(b);

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
    // A materialized zero test: `test R, R` or `cmp X, 0`, then `setz/setnz D`, whose D is later tested and branched on
    // (directly, or after a spill to a stack slot and a reload). One build keeps a loop-invariant zero/null test as a
    // bool, another re-tests the value; the branch on D is what pairs with the other side's test, so the setcc itself
    // adds no condition. Between the test and the setcc only instructions that write no flags, not the tested register
    // and no memory may sit (setcc reads the flags of the last writer), and none of them may be a branch target.
    // Narrow on purpose: zero tests only.
    auto selfTest = [](const Insn& i) {
        return i.ins.mnemonic == ZYDIS_MNEMONIC_TEST && i.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
               i.ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER && i.ops[0].reg.value == i.ops[1].reg.value;
    };
    auto zeroCompare = [](const Insn& i) {
        return i.ins.mnemonic == ZYDIS_MNEMONIC_CMP && i.ins.operand_count_visible == 2 && i.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
               i.ops[1].imm.value.u == 0 &&
               (i.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER || (i.ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY && i.ops[0].mem.type == ZYDIS_MEMOP_TYPE_MEM));
    };
    auto writesFlags = [](const Insn& i) {
        const auto* fl = i.ins.cpu_flags;
        return fl && (fl->modified | fl->set_0 | fl->set_1 | fl->undefined) != 0;
    };
    auto branchTargets = [](const std::vector<Insn>& side) {
        std::set<std::uint64_t> targets;
        for (const auto& i : side)
            if (i.ins.meta.category == ZYDIS_CATEGORY_COND_BR || i.ins.meta.category == ZYDIS_CATEGORY_UNCOND_BR)
                if (const auto t = absoluteTarget(i, i.off)) targets.insert(*t);
        return targets;
    };
    const std::set<std::uint64_t> targetsA = branchTargets(liveA);
    const std::set<std::uint64_t> targetsB = branchTargets(liveB);
    /// The setcc's zero test sits at k - gap with only neutral instructions between.
    auto zeroTestBefore = [&](const std::vector<Insn>& side, std::size_t k, const std::set<std::uint64_t>& targets) {
        for (std::size_t j = k; j-- > 0 && k - j <= 8;) {
            const Insn& t = side[j];
            if (selfTest(t) || zeroCompare(t)) {
                const bool          reg    = t.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER;
                const ZydisRegister tested = reg ? t.ops[0].reg.value : ZYDIS_REGISTER_NONE;
                for (std::size_t m = j + 1; m <= k; ++m) {
                    if (targets.count(side[m].off)) return false;
                    if (m == k) break;
                    if ((reg && writesRegister(side[m], tested)) || (!reg && writtenMemory(side[m]))) return false;
                }
                return true;
            }
            if (writesFlags(t) || t.ins.meta.category == ZYDIS_CATEGORY_COND_BR || t.ins.meta.category == ZYDIS_CATEGORY_UNCOND_BR ||
                t.ins.meta.category == ZYDIS_CATEGORY_CALL || t.ins.meta.category == ZYDIS_CATEGORY_RET || writtenMemory(t))
                return false;
        }
        return false;
    };
    /// Where D goes after the setcc: the stack slot it is spilled to (with that store's offset), if it is spilled
    /// before being overwritten.
    struct Spill { std::uint32_t storeOff; std::int64_t slot; };
    auto spillOf = [&](const std::vector<Insn>& side, std::size_t k, const Frame& f) -> std::optional<Spill> {
        const ZydisRegister d = side[k].ops[0].reg.value;
        for (std::size_t j = k + 1; j < side.size(); ++j) {
            const Insn& i = side[j];
            if (const auto* w = writtenMemory(i);
                w && isPlainMove(i.ins.mnemonic) && i.ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER && i.ops[1].reg.value == d)
                if (const auto slot = slotOf(*w, i, f)) return Spill{ i.off, *slot };
            if (writesRegister(i, d) || i.ins.meta.category == ZYDIS_CATEGORY_COND_BR || i.ins.meta.category == ZYDIS_CATEGORY_UNCOND_BR)
                return std::nullopt;
        }
        return std::nullopt;
    };
    auto slotEscapes = [&](const std::vector<Insn>& side, std::int64_t slot, const Frame& f) {
        for (const auto& i : side)
            if (i.ins.mnemonic == ZYDIS_MNEMONIC_LEA)
                if (const auto s = slotOf(i.ops[1], i, f); s && *s <= slot && slot < *s + 16) return true;
        return false;
    };
    /// A later branch on D: `test D, D; jcc`, or (spilled) a read of D's slot followed within two instructions by a
    /// zero test and a jcc.
    auto branchedOn = [&](const std::vector<Insn>& side, std::size_t k, const Frame& f, std::optional<Spill> spill) {
        const ZydisRegister d = side[k].ops[0].reg.value;
        for (std::size_t j = k + 1; j + 1 < side.size(); ++j) {
            const Insn& i = side[j];
            if (selfTest(i) && i.ops[0].reg.value == d && side[j + 1].ins.meta.category == ZYDIS_CATEGORY_COND_BR) return true;
            if (!spill) continue;
            const bool readsSlot = std::any_of(i.ops, i.ops + i.ins.operand_count_visible, [&](const ZydisDecodedOperand& op) {
                const auto s = slotOf(op, i, f);
                return s && *s == spill->slot && !(op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE);
            });
            if (!readsSlot) continue;
            if (zeroCompare(i) && side[j + 1].ins.meta.category == ZYDIS_CATEGORY_COND_BR) return true;
            if (j + 2 < side.size() && selfTest(side[j + 1]) && side[j + 2].ins.meta.category == ZYDIS_CATEGORY_COND_BR) return true;
        }
        return false;
    };
    auto isMaterialized = [&](const std::vector<Insn>& side, std::size_t k, const Frame& f, const std::set<std::uint64_t>& targets) {
        const Insn& s = side[k];
        if ((s.ins.mnemonic != ZYDIS_MNEMONIC_SETZ && s.ins.mnemonic != ZYDIS_MNEMONIC_SETNZ) || s.ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER ||
            !zeroTestBefore(side, k, targets))
            return false;
        auto spill = spillOf(side, k, f);
        if (spill && slotEscapes(side, spill->slot, f)) spill.reset();
        return branchedOn(side, k, f, spill);
    };
    /// The materialized setcc's and their non-escaping flag spills (the spill store is glue: a bool parked on the stack).
    auto materializedOf = [&](const std::vector<Insn>& side, const Frame& f, const std::set<std::uint64_t>& targets,
                              std::set<std::uint32_t>& setccs, std::set<std::uint32_t>& spills) {
        for (std::size_t k = 0; k < side.size(); ++k) {
            if (!isMaterialized(side, k, f, targets)) continue;
            setccs.insert(side[k].off);
            if (const auto spill = spillOf(side, k, f); spill && !slotEscapes(side, spill->slot, f)) spills.insert(spill->storeOff);
        }
    };
    std::set<std::uint32_t> setccA, setccB, flagSpillA, flagSpillB;
    materializedOf(liveA, frameA, targetsA, setccA, flagSpillA);
    materializedOf(liveB, frameB, targetsB, setccB, flagSpillB);
    auto materializedNullTest = [&](const std::vector<Insn>& side, std::size_t k) {
        return (&side == &liveA ? setccA : setccB).count(side[k].off) != 0;
    };

    // Rule A (needs --diff): a function's own stack slots — its locals below the return address and its home area
    // above it, never `lea`'d (so no callee can see them), and not an outgoing argument — are register allocation:
    // which slot a build spills a value to, or whether it spills at all. What flows through them is the dataflow --diff
    // checks.
    auto takenSlots = [](const std::vector<Insn>& all, const Frame& f) {
        std::vector<std::int64_t> taken;
        for (const auto& i : all)
            if (i.ins.mnemonic == ZYDIS_MNEMONIC_LEA)
                if (const auto s = slotOf(i.ops[1], i, f)) taken.push_back(*s);
        return taken;
    };
    /// Slots the function reads back itself. An outgoing argument is written for the callee and never read by the
    /// caller, so a store into the argument area ([rsp+0x20..]) counts as a local only when its slot is read back.
    auto readBackSlots = [](const std::vector<Insn>& all, const Frame& f) {
        std::set<std::int64_t> read;
        for (const auto& i : all) {
            if (i.ins.mnemonic == ZYDIS_MNEMONIC_LEA) continue;
            for (std::uint8_t o = 0; o < i.ins.operand_count_visible; ++o)
                if (i.ops[o].type == ZYDIS_OPERAND_TYPE_MEMORY && (i.ops[o].actions & ZYDIS_OPERAND_ACTION_MASK_READ))
                    if (const auto s = slotOf(i.ops[o], i, f)) read.insert(*s);
        }
        return read;
    };
    const std::vector<std::int64_t> takenA = takenSlots(liveA, frameA), takenB = takenSlots(liveB, frameB);
    const std::set<std::int64_t>    readBackA = readBackSlots(liveA, frameA), readBackB = readBackSlots(liveB, frameB);
    auto privateLocal = [&](const std::vector<Insn>& side, std::size_t k, const ZydisDecodedOperand& op, const Frame& f,
                            const std::vector<std::int64_t>& taken, const std::set<std::uint64_t>&) {
        if (op.type != ZYDIS_OPERAND_TYPE_MEMORY) return false;
        const auto s = slotOf(op, side[k], f);
        if (!s) return false;
        const bool ours = &side == &liveA;
        if (*s >= 0) return homeSlot(op, side[k], f) && !(ours ? homeEscapesA : homeEscapesB);
        for (const auto t : taken)
            if (*s >= t && *s < t + 0x40) return false;
        const bool argumentArea = op.mem.base == ZYDIS_REGISTER_RSP && op.mem.disp.value >= 0x20 && op.mem.disp.value < 0x60;
        return !((op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) && argumentArea && !(ours ? readBackA : readBackB).count(*s));
    };
    auto privateLocalRead = [&](const std::vector<Insn>& side, std::size_t k, const Frame& f, const std::vector<std::int64_t>& taken,
                                const std::set<std::uint64_t>& targets) {
        const Insn& i = side[k];
        for (std::uint8_t o = 0; o < i.ins.operand_count_visible; ++o)
            if (i.ops[o].type == ZYDIS_OPERAND_TYPE_MEMORY && !(i.ops[o].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
                return privateLocal(side, k, i.ops[o], f, taken, targets);
        return false;
    };
    // Rule E (needs --diff): two aligned SIMD instructions differing only in an immediate (a shuffle's selector) are the
    // same instructions scheduled differently: set aside, they must balance as a multiset per stretch between calls,
    // together with the unpaired SIMD instructions of that mnemonic (the alignment pairs some crosswise, leaves others over).
    auto simdOnly = [](const Insn& i) {
        if (writtenMemory(i) || i.ins.operand_count_visible == 0) return false;
        bool anyXmm = false;
        for (std::uint8_t o = 0; o < i.ins.operand_count_visible; ++o) {
            const auto& op = i.ops[o];
            if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) {
                const auto cls = ZydisRegisterGetClass(op.reg.value);
                if (cls != ZYDIS_REGCLASS_XMM) return false;
                anyXmm = true;
            } else if (op.type != ZYDIS_OPERAND_TYPE_IMMEDIATE && op.type != ZYDIS_OPERAND_TYPE_MEMORY) {
                return false;
            }
        }
        return anyXmm;
    };
    auto simdKey = [](const Insn& i) {
        std::string key = ZydisMnemonicGetString(i.ins.mnemonic);
        for (std::uint8_t o = 0; o < i.ins.operand_count_visible; ++o)
            if (i.ops[o].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) key += ":" + hex(i.ops[o].imm.value.u);
        return key;
    };
    std::map<std::pair<std::size_t, std::string>, int> simdBalance;   ///< (segment, mnemonic:immediates) → ours minus vanilla
    std::set<std::string> simdChecked;   ///< mnemonics whose balance is checked

    // Rule H: `setcc r8; or r8, 1` leaves r8 = 1 whatever the condition (setcc writes 0 or 1), so the setcc tests
    // nothing. One build keeps an unfolded `flag = flag | true`, another stores the constant; the stored value then
    // pairs as any store does.
    auto orOne = [](const std::vector<Insn>& side, std::size_t k) {
        if (k + 1 >= side.size() || side[k].ins.meta.category != ZYDIS_CATEGORY_SETCC || side[k].ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER)
            return false;
        const Insn& o = side[k + 1];
        return o.ins.mnemonic == ZYDIS_MNEMONIC_OR && o.ins.operand_count_visible == 2 && o.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
               o.ops[0].reg.value == side[k].ops[0].reg.value && o.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && o.ops[1].imm.value.u == 1;
    };

    // Rule I (needs --diff): a tail ending in `ret` that one build duplicates into a path and another reaches by a jump.
    // Within a side, the last n instructions before two `ret`s (no branch, call or RIP-relative operand among them) that
    // are the same instructions as a set, registers aside, are twin copies (a duplicated tail may be scheduled in another
    // order). Of each twin pair the less-paired copy is the duplicate: its unpaired instructions are layout, since running
    // a copy of the tail is running the tail; the other copy is counted as usual.
    auto duplicateTails = [](const std::vector<Insn>& side, const std::vector<bool>& paired) {
        std::vector<bool> excused(side.size(), false);
        auto plain = [](const Insn& i) {
            const auto cat = i.ins.meta.category;
            if (cat == ZYDIS_CATEGORY_COND_BR || cat == ZYDIS_CATEGORY_UNCOND_BR || cat == ZYDIS_CATEGORY_CALL || cat == ZYDIS_CATEGORY_RET) return false;
            for (std::uint8_t o = 0; o < i.ins.operand_count_visible; ++o)
                if ((i.ops[o].type == ZYDIS_OPERAND_TYPE_MEMORY && i.ops[o].mem.base == ZYDIS_REGISTER_RIP) ||
                    (i.ops[o].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && i.ops[o].imm.is_relative))
                    return false;
            return true;
        };
        /// The run of plain instructions before the ret at `r`, ret included (at most 16).
        auto tail = [&](std::size_t r) {
            std::vector<std::size_t> run{ r };
            while (run.size() < 16 && run.back() > 0 && plain(side[run.back() - 1])) run.push_back(run.back() - 1);
            return run;   // ret first, then backwards
        };
        /// Whether the first n of `p` and of `q` are the same instructions as a set (registers aside).
        auto sameSet = [&](const std::vector<std::size_t>& p, const std::vector<std::size_t>& q, std::size_t n) {
            std::vector<bool> used(n, false);
            for (std::size_t i = 0; i < n; ++i) {
                bool hit = false;
                for (std::size_t j = 0; j < n && !hit; ++j)
                    if (!used[j] && matchScore(side[p[i]], side[q[j]]) == 2) used[j] = hit = true;
                if (!hit) return false;
            }
            return true;
        };
        std::vector<std::size_t> rets;
        for (std::size_t k = 0; k < side.size(); ++k)
            if (side[k].ins.mnemonic == ZYDIS_MNEMONIC_RET) rets.push_back(k);
        for (std::size_t a = 0; a < rets.size(); ++a)
            for (std::size_t b = a + 1; b < rets.size(); ++b) {
                const auto p = tail(rets[a]), q = tail(rets[b]);
                for (std::size_t n = std::min(p.size(), q.size()); n > 1; --n) {
                    if (!sameSet(p, q, n)) continue;
                    std::size_t pp = 0, pq = 0;
                    for (std::size_t i = 0; i < n; ++i) pp += paired[p[i]], pq += paired[q[i]];
                    for (std::size_t i = 0; i < n; ++i) excused[(pp < pq ? p : q)[i]] = true;
                    break;
                }
            }
        return excused;
    };
    const auto rows = alignInstructions(liveA, liveB);
    std::vector<bool> pairedA(liveA.size(), false), pairedB(liveB.size(), false);
    for (const auto& [ia, ib] : rows)
        if (ia >= 0 && ib >= 0) pairedA[std::size_t(ia)] = pairedB[std::size_t(ib)] = true;
    const auto duplicateA = duplicateTails(liveA, pairedA), duplicateB = duplicateTails(liveB, pairedB);

    // Rule J (needs --diff): register setup a build scheduled in another order within a block — the vtable `lea` and an
    // argument `lea` crossing a constant store, two argument `lea`s swapped. An aligned pair of register-only definitions
    // (`lea r, [address]`, `mov r, imm`: no memory touched) whose sources differ is set aside; the set-aside and unpaired
    // ones of that mnemonic must balance per stretch between calls, by what they compute (registers aside).
    auto registerSetup = [](const Insn& i) {
        if (i.ins.operand_count_visible != 2 || i.ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER) return false;
        const auto cls = ZydisRegisterGetClass(i.ops[0].reg.value);
        if (cls != ZYDIS_REGCLASS_GPR64 && cls != ZYDIS_REGCLASS_GPR32) return false;
        if (i.ins.mnemonic == ZYDIS_MNEMONIC_LEA) return i.ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY;
        return i.ins.mnemonic == ZYDIS_MNEMONIC_MOV && i.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
    };
    auto setupKey = [](const Insn& i, const Frame& f) {
        std::string key = ZydisMnemonicGetString(i.ins.mnemonic);
        const auto& s   = i.ops[1];
        if (s.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) return key + ":i" + hex(s.imm.value.u);
        if (s.mem.base == ZYDIS_REGISTER_RIP) return key + ":rip";   // the reference check places the address
        if (s.mem.base == ZYDIS_REGISTER_RSP && s.mem.index == ZYDIS_REGISTER_NONE)
            if (const auto d = f.depth.find(i.off); d != f.depth.end()) return key + ":s" + std::to_string(s.mem.disp.value - d->second);
        return key + ":d" + hex(std::uint64_t(s.mem.disp.value)) + (s.mem.index != ZYDIS_REGISTER_NONE ? "x" + std::to_string(s.mem.scale) : "");
    };
    std::map<std::pair<std::size_t, std::string>, int> setupBalance;   ///< (segment, what it computes) → ours minus vanilla
    std::set<std::string> setupChecked;
    bool ruleA = false, ruleE = false, ruleI = false, ruleJ = false;

    for (const auto& [ia, ib] : rows) {
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
            if (const auto* wx = writtenMemory(x), *wy = writtenMemory(y);
                wx && wy && x.ins.mnemonic == y.ins.mnemonic && privateLocal(liveA, std::size_t(ia), *wx, frameA, takenA, targetsA) &&
                privateLocal(liveB, std::size_t(ib), *wy, frameB, takenB, targetsB)) {
                if (!operandsMatch(*wx, *wy, masked)) ruleA = true;
                ++glue;
                continue;
            }
            // Two stores aligned crosswise (a build wrote the fields in the other order): match them as a set within
            // the stretch between calls, like any unpaired store, instead of failing on the pair.
            if (const auto* wx = writtenMemory(x), *wy = writtenMemory(y);
                wx && wy && x.ins.mnemonic == y.ins.mnemonic && x.ins.mnemonic != ZYDIS_MNEMONIC_CALL && !operandsMatch(*wx, *wy, masked)) {
                effectsA.push_back({ &x, segment });
                effectsB.push_back({ &y, segment });
                continue;
            }
            bool readsDiffer = false, simdDeferred = false, setupDeferred = false;
            for (std::uint8_t o = 0; o < x.ins.operand_count_visible && !simdDeferred && !setupDeferred; ++o) {
                const auto& ox = x.ops[o];
                const auto& oy = y.ops[o];
                if (privateLocal(liveA, std::size_t(ia), ox, frameA, takenA, targetsA) && privateLocal(liveB, std::size_t(ib), oy, frameB, takenB, targetsB) &&
                    !(ox.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) && !(oy.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)) {
                    if (ox.mem.disp.value != oy.mem.disp.value || ox.size != oy.size) ruleA = true;
                    continue;
                }
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
                    if (x.ins.mnemonic == y.ins.mnemonic && simdOnly(x) && simdOnly(y)) {
                        simdDeferred = true;
                        break;
                    }
                    if (x.ins.mnemonic == y.ins.mnemonic && registerSetup(x) && registerSetup(y)) {
                        setupDeferred = true;
                        break;
                    }
                    why = "operands differ beyond registers at ours +0x" + hex(x.off) + " / vanilla +0x" + hex(y.off);
                    return false;
                }
            }
            if (simdDeferred) {
                simdChecked.insert(ZydisMnemonicGetString(x.ins.mnemonic));
                ++simdBalance[{ segment, simdKey(x) }];
                --simdBalance[{ segment, simdKey(y) }];
                ruleE = true;
                ++glue;
                continue;
            }
            if (setupDeferred) {
                setupChecked.insert(ZydisMnemonicGetString(x.ins.mnemonic));
                ++setupBalance[{ segment, setupKey(x, frameA) }];
                --setupBalance[{ segment, setupKey(y, frameB) }];
                ruleJ = true;
                ++glue;
                continue;
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
            if (duplicateA[std::size_t(ia)] && !pairedA[std::size_t(ia)]) { ++glue, ruleI = true; continue; }
            if (const char* fam = foldedConditionFamily(liveA, std::size_t(ia), targetsA);
                fam && !materializedNullTest(liveA, std::size_t(ia)) && !orOne(liveA, std::size_t(ia)))
                ++conditions[fam].first;
            if (isFloatArithmetic(x.ins.mnemonic)) ++floatOps[int(x.ins.mnemonic)];
            if (simdOnly(x)) ++simdBalance[{ segment, simdKey(x) }];
            if (registerSetup(x)) ++setupBalance[{ segment, setupKey(x, frameA) }];
            const auto* wx    = writtenMemory(x);
            const bool  spill = wx && privateLocal(liveA, std::size_t(ia), *wx, frameA, takenA, targetsA);
            const auto  rx    = readOf(x, frameA);
            if (rx && !(rx->stack && privateLocalRead(liveA, std::size_t(ia), frameA, takenA, targetsA))) spareA.push_back(*rx);
            if (hasEffect(x) && x.off >= prologueA && !isSaveOrRestore(x, savesA, frameA) && !privateScratch(x, frameA, homeEscapesA) &&
                !flagSpillA.count(x.off) && !spill)
                effectsA.push_back({ &x, segment });
            else
                ++glue, ruleA = ruleA || (spill && x.off >= prologueA && !isSaveOrRestore(x, savesA, frameA));
        } else {
            const Insn& y = liveB[std::size_t(ib)];
            if (duplicateB[std::size_t(ib)] && !pairedB[std::size_t(ib)]) { ++glue, ruleI = true; continue; }
            if (const char* fam = foldedConditionFamily(liveB, std::size_t(ib), targetsB);
                fam && !materializedNullTest(liveB, std::size_t(ib)) && !orOne(liveB, std::size_t(ib)))
                ++conditions[fam].second;
            if (isFloatArithmetic(y.ins.mnemonic)) --floatOps[int(y.ins.mnemonic)];
            if (simdOnly(y)) --simdBalance[{ segment, simdKey(y) }];
            if (registerSetup(y)) --setupBalance[{ segment, setupKey(y, frameB) }];
            const auto* wy    = writtenMemory(y);
            const bool  spill = wy && privateLocal(liveB, std::size_t(ib), *wy, frameB, takenB, targetsB);
            const auto  ry    = readOf(y, frameB);
            if (ry && !(ry->stack && privateLocalRead(liveB, std::size_t(ib), frameB, takenB, targetsB))) spareB.push_back(*ry);
            if (hasEffect(y) && y.off >= prologueB && !isSaveOrRestore(y, savesB, frameB) && !privateScratch(y, frameB, homeEscapesB) &&
                !flagSpillB.count(y.off) && !spill)
                effectsB.push_back({ &y, segment });
            else
                ++glue, ruleA = ruleA || (spill && y.off >= prologueB && !isSaveOrRestore(y, savesB, frameB));
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
    for (const auto& [key, balance] : simdBalance) {
        if (balance != 0 && simdChecked.count(key.second.substr(0, key.second.find(':')))) {
            why = "SIMD instructions set aside as rescheduled don't balance (" + key.second + ", ours " + (balance > 0 ? "+" : "") +
                  std::to_string(balance) + ")";
            return false;
        }
    }
    for (const auto& [key, balance] : setupBalance) {
        if (balance != 0 && setupChecked.count(key.second.substr(0, key.second.find(':')))) {
            why = "register setup set aside as rescheduled doesn't balance (" + key.second + ", ours " + (balance > 0 ? "+" : "") +
                  std::to_string(balance) + ")";
            return false;
        }
    }
    needsDiff = ruleA || ruleE || ruleI || ruleJ;
    why = std::to_string(aligned) + " aligned, " + std::to_string(glue) + " unpaired side-effect-free, " +
          std::to_string(effectsA.size()) + " store(s) with a different source";
    if (deadCount) why += ", " + std::to_string(deadCount) + " provably dead";
    if (ruleA) why += ", local stack slots rearranged (rule A)";
    if (ruleE) why += ", SIMD rescheduled (rule E)";
    if (ruleI) why += ", a duplicated return tail (rule I)";
    if (ruleJ) why += ", register setup rescheduled (rule J)";
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
    if (name == "__ImageBase") return target == pe.imageBase ? std::string() : "__ImageBase: vanilla 0x" + hex(target) + " is not the image base";
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
    // Compiler constants name their own bytes, so one the alignment paired with another load (the two builds scheduled
    // the loads differently) is still placed if some other vanilla load of the same mnemonic reads exactly those bytes.
    // Each vanilla load vouches once, and only after every aligned check has claimed its own, so a changed constant
    // (no vanilla load of its bytes) still fails.
    struct Deferred { const Insn* x; std::string mangled, error; };   ///< error empty: no aligned partner at all
    std::vector<Deferred>   deferred;
    std::set<std::uint32_t> claimed;   ///< vanilla offsets whose constant load already placed one of ours
    for (const auto& [ia, ib] : alignInstructions(a, b)) {
        if (ia < 0) continue;
        const Insn& x = a[std::size_t(ia)];
        for (std::uint32_t o = x.off; o < x.off + x.ins.length; ++o) {
            const auto t = ours.targets.find(o);
            if (t == ours.targets.end()) continue;
            if (t->second.mangled.rfind("??_C@", 0) == 0 || t->second.name == "`string'") continue;   // string literals
            if (t->second.local) { ++m.refsVerified; continue; }   // into this function (a jump table): checkTables compares it
            const bool constant = t->second.mangled.rfind("__real@", 0) == 0 || t->second.mangled.rfind("__xmm@", 0) == 0;
            if (ib < 0) {
                if (constant) deferred.push_back({ &x, t->second.mangled, std::string() });   // unverifiable unless found
                else ++m.refsUnverifiable;
                continue;
            }
            const Insn& y      = b[std::size_t(ib)];
            const auto  actual = absoluteTarget(y, va + y.off);
            if (actual) {
                if (const auto self = selfCheck(t->second.mangled, *actual, pe)) {
                    if (self->empty()) {
                        ++m.refsVerified;
                        claimed.insert(y.off);
                    } else if (constant) {
                        deferred.push_back({ &x, t->second.mangled, *self });
                    } else {
                        m.refsWrong.push_back(*self);
                    }
                    continue;
                }
            }
            auto it = syms.byCpp.find(t->second.name);
            if (it == syms.byCpp.end()) it = syms.byCpp.find(t->second.mangled);
            if (it != syms.byCpp.end()) {
                const std::uint64_t expect = it->second + std::uint64_t(t->second.addend);
                if (actual && *actual == expect) {
                    ++m.refsVerified;
                } else {
                    m.refsWrong.push_back(t->second.name + " should be 0x" + hex(expect) + " (" + syms.registryName.at(it->first) +
                                          (t->second.addend ? (t->second.addend > 0 ? "+0x" : "-0x") + hex(std::uint64_t(std::llabs(t->second.addend))) : std::string()) +
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
    for (const auto& d : deferred) {
        const auto hit = std::find_if(b.begin(), b.end(), [&](const Insn& y) {
            if (y.ins.mnemonic != d.x->ins.mnemonic || claimed.count(y.off)) return false;
            const auto actual = absoluteTarget(y, va + y.off);
            if (!actual) return false;
            const auto self = selfCheck(d.mangled, *actual, pe);
            return self && self->empty();
        });
        if (hit == b.end()) {
            if (d.error.empty()) ++m.refsUnverifiable;
            else m.refsWrong.push_back(d.error);
        } else {
            claimed.insert(hit->off);
            ++m.refsVerified;
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
    if (variantEquivalent(ours, *a, *b, va, pe, variantWhy, m.needsDiff)) {
        m.status = Status::Variant;
        m.detail = regallocWhy + "; " + variantWhy;
    } else {
        m.detail = regallocWhy + "; not VARIANT: " + variantWhy;
    }
    return m;
}

// ── switch jump tables ─────────────────────────────────────────────────────────────────────────────────────────────

/// @brief A switch's table, laid out after the function's code. 4-byte entries are case targets (function offsets);
///        1-byte entries index a 4-byte table (a two-level switch).
struct JumpTable {
    std::uint32_t             start = 0;
    std::uint8_t              entrySize = 4;
    std::vector<std::int64_t> entries;
};

/// @brief Where a function's code ends and its jump tables (which follow it) begin.
struct CodeLayout {
    std::uint32_t          codeEnd = 0;
    std::vector<JumpTable> tables;
};

/// @brief Decodes `bytes` linearly, asking `tableAt` of each instruction whether it reads a jump table (and where), and
///        stops at the first table: what follows is data. MSVC x64 places a function's tables after its code.
CodeLayout layoutOf(const Bytes& bytes, const std::function<std::optional<std::pair<std::uint32_t, std::uint8_t>>(const Insn&)>& tableAt)
{
    CodeLayout   layout;
    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    std::uint32_t firstTable = std::uint32_t(bytes.size());
    std::uint32_t off        = 0;
    while (off < bytes.size() && off < firstTable) {
        Insn i;
        i.off = off;
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, bytes.data() + off, bytes.size() - off, &i.ins, i.ops))) break;
        if (const auto t = tableAt(i); t && t->first > off && t->first < bytes.size()) {
            if (std::none_of(layout.tables.begin(), layout.tables.end(), [&](const JumpTable& j) { return j.start == t->first; }))
                layout.tables.push_back({ t->first, t->second, {} });
            firstTable = std::min(firstTable, t->first);
        }
        off += i.ins.length;
    }
    layout.codeEnd = layout.tables.empty() ? std::uint32_t(bytes.size()) : std::min(off, firstTable);
    std::sort(layout.tables.begin(), layout.tables.end(), [](const JumpTable& a, const JumpTable& b) { return a.start < b.start; });
    return layout;
}

/// @brief The memory operand reading a jump table: `[base + index*4 + disp]` (case targets) or `[base + index + disp]`
///        (the byte index of a two-level switch).
const ZydisDecodedOperand* tableOperand(const Insn& i)
{
    for (std::uint8_t k = 0; k < i.ins.operand_count_visible; ++k) {
        const auto& op = i.ops[k];
        if (op.type == ZYDIS_OPERAND_TYPE_MEMORY && op.mem.type == ZYDIS_MEMOP_TYPE_MEM && op.mem.base != ZYDIS_REGISTER_NONE &&
            op.mem.base != ZYDIS_REGISTER_RIP && op.mem.index != ZYDIS_REGISTER_NONE && (op.mem.scale == 4 || op.mem.scale == 1) &&
            !(op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
            return &op;
    }
    return nullptr;
}

/// @brief Fills each table's entries from 4-byte `entryAt(offset)` (nullopt ends the table) or raw bytes, each table
///        running at most to the next one. A byte table stops at the first index past the case table it selects into.
void fillTables(CodeLayout& layout, const Bytes& bytes, const std::function<std::optional<std::int64_t>(std::uint32_t)>& entryAt)
{
    std::size_t cases = 0;
    for (std::size_t k = 0; k < layout.tables.size(); ++k) {
        auto&               t   = layout.tables[k];
        const std::uint32_t end = k + 1 < layout.tables.size() ? layout.tables[k + 1].start : std::uint32_t(bytes.size());
        if (t.entrySize != 4) continue;
        for (std::uint32_t at = t.start; at + 4 <= end; at += 4) {
            const auto e = entryAt(at);
            if (!e || *e < 0 || *e >= layout.codeEnd) break;
            t.entries.push_back(*e);
        }
        cases = std::max(cases, t.entries.size());
    }
    for (std::size_t k = 0; k < layout.tables.size(); ++k) {
        auto&               t   = layout.tables[k];
        const std::uint32_t end = k + 1 < layout.tables.size() ? layout.tables[k + 1].start : std::uint32_t(bytes.size());
        if (t.entrySize != 1) continue;
        for (std::uint32_t at = t.start; at < end; ++at) {
            if (cases && bytes[at] >= cases) break;
            t.entries.push_back(bytes[at]);
        }
    }
}

/// @brief Vanilla's layout: a table is read through a register holding the image base (`lea r, [__ImageBase]`), its
///        displacement the table's RVA.
CodeLayout vanillaLayout(const Bytes& bytes, std::uint64_t va, std::uint64_t imageBase)
{
    std::set<ZydisRegister> baseRegs;
    CodeLayout              layout = layoutOf(bytes, [&](const Insn& i) -> std::optional<std::pair<std::uint32_t, std::uint8_t>> {
        if (i.ins.mnemonic == ZYDIS_MNEMONIC_LEA && i.ops[1].mem.base == ZYDIS_REGISTER_RIP) {
            ZyanU64 abs = 0;
            if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&i.ins, &i.ops[1], va + i.off, &abs)) && abs == imageBase) {
                baseRegs.insert(enclosing(i.ops[0].reg.value));
                return std::nullopt;
            }
        }
        const auto* op = tableOperand(i);
        if (!op || !baseRegs.count(enclosing(op->mem.base))) return std::nullopt;
        const std::int64_t at = std::int64_t(imageBase) + op->mem.disp.value - std::int64_t(va);
        if (at <= 0) return std::nullopt;
        return std::make_pair(std::uint32_t(at), std::uint8_t(op->mem.scale));
    });
    fillTables(layout, bytes, [&](std::uint32_t at) -> std::optional<std::int64_t> {
        return std::int64_t(imageBase) + rd<std::uint32_t>(bytes, at) - std::int64_t(va);
    });
    return layout;
}

/// @brief Our layout: a table is read through an image-relative relocation into this function (`$LN` / section
///        symbol), its entries image-relative relocations to case labels.
CodeLayout oursLayout(const CoffFunction& f)
{
    auto localAt = [&](std::uint32_t at) -> std::optional<std::int64_t> {
        const auto t = f.targets.find(at);
        if (t == f.targets.end() || t->second.type != IMAGE_REL_AMD64_ADDR32NB || !t->second.local) return std::nullopt;
        return *t->second.local;
    };
    CodeLayout layout = layoutOf(f.bytes, [&](const Insn& i) -> std::optional<std::pair<std::uint32_t, std::uint8_t>> {
        const auto* op = tableOperand(i);
        if (!op) return std::nullopt;
        for (std::uint32_t o = i.off; o < i.off + i.ins.length; ++o)
            if (const auto l = localAt(o)) return std::make_pair(std::uint32_t(*l), std::uint8_t(op->mem.scale));
        return std::nullopt;
    });
    fillTables(layout, f.bytes, localAt);
    return layout;
}

/// @brief The function without its jump tables: what the instruction comparison sees.
CoffFunction codeOnly(const CoffFunction& f, std::uint32_t codeEnd)
{
    CoffFunction c = f;
    c.bytes.resize(codeEnd);
    c.mask.resize(codeEnd);
    for (auto it = c.targets.begin(); it != c.targets.end();) it = it->first >= codeEnd ? c.targets.erase(it) : std::next(it);
    return c;
}

/// @brief Compares the two functions' jump tables after their code has been matched: the same tables, byte index
///        tables identical, and every case target landing on corresponding code — the same offset when the code has the
///        same layout (EXACT/REGALLOC), an instruction the alignment pairs with vanilla's otherwise (VARIANT).
void checkTables(Match& m, const CoffFunction& code, const Bytes& vanillaCode, const CodeLayout& ours, const CodeLayout& vanilla)
{
    auto fail = [&](const std::string& why) {
        m.status = Status::Structural;
        m.detail = "jump tables: " + why + (m.detail.empty() ? "" : "\n" + m.detail);
    };
    if (m.status != Status::Exact && m.status != Status::Regalloc && m.status != Status::Variant) return;
    if (ours.tables.size() != vanilla.tables.size()) return fail(std::to_string(ours.tables.size()) + " vs " + std::to_string(vanilla.tables.size()));
    std::map<std::uint32_t, std::uint32_t> paired;   ///< our instruction offset → vanilla's
    if (m.status == Status::Variant) {
        const auto a = disassemble(code.bytes);
        const auto b = disassemble(vanillaCode);
        if (!a || !b) return fail("undecodable code");
        for (const auto& [ia, ib] : alignInstructions(*a, *b))
            if (ia >= 0 && ib >= 0) paired[(*a)[std::size_t(ia)].off] = (*b)[std::size_t(ib)].off;
    }
    for (std::size_t k = 0; k < ours.tables.size(); ++k) {
        const auto& x = ours.tables[k];
        const auto& y = vanilla.tables[k];
        if (x.entrySize != y.entrySize || x.entries.size() != y.entries.size())
            return fail("table " + std::to_string(k) + " has " + std::to_string(x.entries.size()) + " vs " + std::to_string(y.entries.size()) + " entries");
        for (std::size_t e = 0; e < x.entries.size(); ++e) {
            const bool same = x.entrySize == 1 || m.status != Status::Variant
                                  ? x.entries[e] == y.entries[e]
                                  : paired.count(std::uint32_t(x.entries[e])) && paired.at(std::uint32_t(x.entries[e])) == y.entries[e];
            if (!same)
                return fail("table " + std::to_string(k) + " entry " + std::to_string(e) + ": ours +0x" + hex(std::uint64_t(x.entries[e])) +
                            ", vanilla +0x" + hex(std::uint64_t(y.entries[e])));
        }
    }
}

// ── differential testing: ours and vanilla run on the same synthetic inputs ─────────────────────────────────────────

/// @brief A function's C++ signature, read from its mangled name: where the arguments go and what comes back.
struct Signature {
    enum class Kind { Void, Bool, Int8, Int16, Int32, Int64, Ptr, Ref, Float, Double };
    bool              hasThis = false;
    Kind              ret = Kind::Int64;
    std::vector<Kind> params;   ///< after `this`
    std::string       text;
};

Signature::Kind kindOf(std::string t)
{
    using K = Signature::Kind;
    const bool isEnum = t.find("enum ") != std::string::npos;
    if (t.find('*') != std::string::npos) return K::Ptr;
    if (t.find('&') != std::string::npos) return K::Ref;
    for (const char* noise : { "const ", " const", "volatile ", "__ptr64", "class ", "struct ", "union ", "enum " })
        for (std::size_t p; (p = t.find(noise)) != std::string::npos;) t.erase(p, std::strlen(noise));
    t = std::string(CB::engine::text::Trim(t));
    if (t == "void") return K::Void;
    if (t == "bool") return K::Bool;
    if (t == "float") return K::Float;
    if (t == "double") return K::Double;
    if (t == "char" || t == "signed char" || t == "unsigned char") return K::Int8;
    if (t == "short" || t == "unsigned short" || t == "wchar_t") return K::Int16;
    if (t == "int" || t == "unsigned int" || t == "long" || t == "unsigned long" || isEnum) return K::Int32;
    if (t == "__int64" || t == "unsigned __int64") return K::Int64;
    return K::Ptr;   // a class by value: an address as far as the inputs go
}

Signature parseSignature(const std::string& mangled)
{
    Signature s;
    {
        std::lock_guard lk(g_dbghelp);
        char            buf[4096];
        if (!UnDecorateSymbolName(mangled.c_str(), buf, sizeof buf, UNDNAME_COMPLETE)) return s;
        s.text = buf;
    }
    const std::string& t     = s.text;
    const auto         close = t.rfind(')');
    if (close == std::string::npos) return s;
    std::size_t open = std::string::npos;
    for (std::size_t k = close + 1, depth = 0; k-- > 0;) {
        if (t[k] == ')') ++depth;
        else if (t[k] == '(' && --depth == 0) { open = k; break; }
    }
    if (open == std::string::npos) return s;
    const std::string list = t.substr(open + 1, close - open - 1);
    if (list != "void" && !list.empty()) {
        int         depth = 0;
        std::string cur;
        for (char c : list) {
            if (c == '<' || c == '(') ++depth;
            if (c == '>' || c == ')') --depth;
            if (c == ',' && depth == 0) { s.params.push_back(kindOf(cur)); cur.clear(); continue; }
            cur += c;
        }
        s.params.push_back(kindOf(cur));
    }
    const auto  cc   = t.find("__cdecl");
    std::string head = cc == std::string::npos ? std::string() : t.substr(0, cc);
    s.hasThis = (head.find("public:") != std::string::npos || head.find("protected:") != std::string::npos ||
                 head.find("private:") != std::string::npos) && head.find("static ") == std::string::npos;
    for (const char* w : { "public:", "protected:", "private:", "virtual ", "static " })
        for (std::size_t p; (p = head.find(w)) != std::string::npos;) head.erase(p, std::strlen(w));
    head  = std::string(CB::engine::text::Trim(head));
    s.ret = head.empty() ? Signature::Kind::Void : kindOf(head);
    return s;
}

std::uint64_t mix64(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::uint64_t seeded(std::uint64_t seed, std::uint64_t key) { return mix64(mix64(seed) ^ key); }

constexpr std::uint64_t kPage       = 0x1000;
constexpr std::uint64_t kHeapBase   = 0x0000100000000000ull;   ///< synthetic pointers point into the 4 GiB above
constexpr std::uint64_t kHeapSpan   = 0x0000000100000000ull;
constexpr std::uint64_t kStackBase  = 0x00007ff000000000ull;
constexpr std::uint64_t kStackSize  = 0x0000000000100000ull;
constexpr std::uint64_t kStubBase   = 0x00007fe000000000ull;   ///< import slots point here
constexpr std::uint64_t kStubSize   = 0x0000000000010000ull;
constexpr std::uint64_t kSentinel   = kStubBase;               ///< the return address the function under test gets
constexpr std::uint64_t kStackToken = 0x5354414b5354414bull;   ///< what any pointer into the stack compares as
constexpr std::size_t   kMaxPages   = 8192;
constexpr std::uint64_t kBudget     = 20000;   ///< instructions per run; a run that needs more (a loop over a huge synthetic count) is inconclusive
constexpr std::uint64_t kInputTag   = 1ull << 63;               ///< keys of inputs that are not memory (arguments, call results)

std::uint64_t argKey(std::size_t k)            { return kInputTag | 0xA4600ull | k; }
std::uint64_t resultKey(std::uint64_t call)    { return kInputTag | (0xCA11ull << 32) | call; }

constexpr float         kFloats[] = { 0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 0.25f, 0.001f, -0.001f, 0.0005f, 30.0f, -2.5f, 0.0133333f, 1e-6f, 100.0f, 0.75f, 3.0f };
constexpr std::uint32_t kInts[]   = { 0, 1, 2, 3, 4, 5, 8, 0xffffu };   ///< no 32-bit -1: read as a float it is a NaN

std::uint32_t floatBits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
std::uint32_t synthFloat(std::uint64_t h) { return floatBits(kFloats[h % std::size(kFloats)]); }
std::uint32_t synthDword(std::uint64_t h) { return (h & 1) ? kInts[(h >> 1) % std::size(kInts)] : synthFloat(h >> 1); }
/// @brief Both 32-bit lanes made finite floats (an all-ones exponent loses its low bit), since any qword may be read as floats.
std::uint64_t finite(std::uint64_t v)
{
    for (int lane = 0; lane < 2; ++lane)
        if (((v >> (32 * lane)) & 0x7F800000ull) == 0x7F800000ull) v ^= 0x00800000ull << (32 * lane);
    return v;
}

std::uint64_t heapPointer(std::uint64_t h) { return finite(kHeapBase + (((h >> 4) % kHeapSpan) & ~0xFull)); }

/// @brief The synthetic value of the qword at `key`: nulls, pointers into the synthetic heap, small ints and the floats
///        the code compares against, bools, and noise — a pure function of (seed, key), so both runs read the same.
/// @note No NaN, infinity or -0: /fp:fast lets the compiler order min/max and compares freely, which only those tell apart.
std::uint64_t synthQword(std::uint64_t seed, std::uint64_t key)
{
    const std::uint64_t h = seeded(seed, key);
    const std::uint64_t r = mix64(h);
    switch (h % 20) {
        case 0: case 1:                         return 0;
        case 2: case 3: case 4: case 5: case 6: return heapPointer(r);
        case 15: case 16:                       return r & 0x0101010101010101ull;
        case 17: case 18: case 19:              return finite(r);
        default:                                return std::uint64_t(synthDword(r)) | (std::uint64_t(synthDword(mix64(r))) << 32);
    }
}

/// @brief The value a forced input takes: a pointer to its own synthetic object.
std::uint64_t forcedPointer(std::uint64_t seed, std::uint64_t key) { return heapPointer(seeded(seed, key ^ 0x9E3779B97F4A7C15ull)); }

std::uint64_t roundUp(std::uint64_t v, std::uint64_t a) { return (v + a - 1) & ~(a - 1); }

std::uint64_t lowBytes(std::uint64_t v, std::uint8_t width) { return width >= 8 ? v : v & ((1ull << (width * 8)) - 1); }

/// @brief One call out of the function under test: every argument slot as the callee would find it, and how much of
///        each the caller wrote since the previous call (or was passed in) — for callees whose signature is unknown.
struct CallRecord {
    std::uint64_t                target = 0;
    std::array<std::uint64_t, 4> gpr{};
    std::array<std::uint8_t, 4>  gprWidth{};   ///< bytes defined; 0 = left alone
    std::array<std::uint64_t, 4> xmm{};        ///< low 64 bits
    std::array<bool, 4>          xmmDefined{};
    std::array<std::uint64_t, 6> stack{};      ///< stack argument slots 5..10
    std::array<std::uint8_t, 6>  stackWidth{};
};

/// @brief An argument's value as the callee can observe it: its type's width, and any pointer into the stack (whose
///        layout is the caller's business) as one token.
std::uint64_t observed(std::uint64_t v, std::uint8_t width)
{
    v = lowBytes(v, width);
    return (width == 8 && v >= kStackBase && v < kStackBase + kStackSize) ? kStackToken : v;
}

std::uint8_t widthOf(Signature::Kind k)
{
    using K = Signature::Kind;
    switch (k) {
        case K::Bool: case K::Int8: return 1;
        case K::Int16:              return 2;
        case K::Int32: case K::Float: return 4;
        default:                    return 8;
    }
}

struct RunResult {
    enum class End { Returned, Fault, Budget } end = End::Fault;
    std::string                           fault;
    std::vector<CallRecord>               calls;
    std::map<std::uint64_t, std::uint8_t> written;   ///< final byte at every address written outside the stack
    std::uint64_t                         rax = 0, xmm0 = 0;
    bool                                  preserved = true;   ///< callee-saved registers and rsp restored on return
    std::vector<std::pair<std::string, std::uint64_t>> pointers;   ///< pointer arguments, for naming addresses
    std::set<std::uint32_t>               executed;   ///< function offsets
    std::map<std::uint32_t, std::uint8_t> branches;   ///< conditional jump offset → 1 fell through | 2 taken
    std::optional<std::uint64_t>          suspect;    ///< on a fault: the input whose value the bad address came from
    bool                                  nan = false;   ///< a float compare, min or max saw a NaN: /fp:fast code may then legitimately differ
};

bool isFloatCompare(ZydisMnemonic m)
{
    switch (m) {
        case ZYDIS_MNEMONIC_COMISS: case ZYDIS_MNEMONIC_UCOMISS: case ZYDIS_MNEMONIC_COMISD: case ZYDIS_MNEMONIC_UCOMISD:
        case ZYDIS_MNEMONIC_MINSS:  case ZYDIS_MNEMONIC_MAXSS:   case ZYDIS_MNEMONIC_MINPS:  case ZYDIS_MNEMONIC_MAXPS:
        case ZYDIS_MNEMONIC_CMPSS:  case ZYDIS_MNEMONIC_CMPPS:   return true;
        default:                    return false;
    }
}

/// @brief Unicorn over the exe's image, plus a lazily materialized synthetic heap, a stack, and import stubs. Every call
///        that leaves the function under test is answered by a stub (logged; deterministic results; out-arguments on
///        the stack filled), so a run is the function's own behavior on one seed's inputs.
class Emulator {
public:
    explicit Emulator(const PeImage& pe) : pe_(pe)
    {
        if (uc_open(UC_ARCH_X86, UC_MODE_64, &uc_) != UC_ERR_OK) throw std::runtime_error("unicorn: cannot open an x86-64 engine");
        std::uint64_t span = 0;
        for (const auto& s : pe.sections) span = std::max<std::uint64_t>(span, s.va + std::max(s.vsize, s.rawSize));
        span_  = roundUp(span, kPage);
        image_ = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, span_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        stack_ = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, kStackSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!image_ || !stack_) throw std::runtime_error("cannot allocate the emulated image");
        std::memcpy(image_, pe.bytes.data(), std::min<std::size_t>(kPage, pe.bytes.size()));
        for (const auto& s : pe.sections) {
            const std::size_t n = std::min<std::size_t>(s.rawSize, s.vsize ? s.vsize : s.rawSize);
            if (s.raw + n <= pe.bytes.size()) std::memcpy(image_ + s.va, pe.bytes.data() + s.raw, n);
        }
        std::uint64_t stub = kStubBase + 0x10;
        for (const auto& [slot, name] : pe.importsBySlot) {
            std::memcpy(image_ + (slot - pe.imageBase), &stub, 8);
            stubNames_[stub] = name;
            stub += 0x10;
        }
        pristine_.assign(image_, image_ + span_);

        auto check = [](uc_err e, const char* what) { if (e != UC_ERR_OK) throw std::runtime_error(std::string("unicorn: ") + what + ": " + uc_strerror(e)); };
        std::uint64_t first = span_;
        for (const auto& s : pe.sections) first = std::min<std::uint64_t>(first, s.va);
        check(uc_mem_map_ptr(uc_, pe.imageBase, first, UC_PROT_READ, image_), "map headers");
        for (std::size_t i = 0; i < pe.sections.size(); ++i) {
            const auto&   s    = pe.sections[i];
            std::uint64_t size = roundUp(std::max(s.vsize, s.rawSize), kPage);
            for (const auto& o : pe.sections) if (o.va > s.va) size = std::min<std::uint64_t>(size, o.va - s.va);
            if (size == 0) continue;
            std::uint32_t prot = UC_PROT_READ;
            if (s.characteristics & IMAGE_SCN_MEM_WRITE) prot |= UC_PROT_WRITE;
            if (s.executable()) prot |= UC_PROT_EXEC;
            check(uc_mem_map_ptr(uc_, pe.imageBase + s.va, size, prot, image_ + s.va), "map section");
        }
        check(uc_mem_map_ptr(uc_, kStackBase, kStackSize, UC_PROT_READ | UC_PROT_WRITE, stack_), "map stack");
        check(uc_mem_map(uc_, kStubBase, kStubSize, UC_PROT_ALL), "map stubs");
        const Bytes rets(kStubSize, 0xC3);
        uc_mem_write(uc_, kStubBase, rets.data(), rets.size());
        uc_mem_protect(uc_, kStubBase, kStubSize, UC_PROT_READ | UC_PROT_EXEC);

        uc_hook h;
        check(uc_hook_add(uc_, &h, UC_HOOK_CODE, reinterpret_cast<void*>(&Emulator::onCode), this, 1, 0), "hook code");
        check(uc_hook_add(uc_, &h, UC_HOOK_MEM_WRITE, reinterpret_cast<void*>(&Emulator::onWrite), this, 1, 0), "hook writes");
        check(uc_hook_add(uc_, &h, UC_HOOK_MEM_READ, reinterpret_cast<void*>(&Emulator::onRead), this, 1, 0), "hook reads");
        check(uc_hook_add(uc_, &h, UC_HOOK_MEM_UNMAPPED, reinterpret_cast<void*>(&Emulator::onUnmapped), this, 1, 0), "hook unmapped");
        check(uc_hook_add(uc_, &h, UC_HOOK_MEM_PROT, reinterpret_cast<void*>(&Emulator::onProt), this, 1, 0), "hook protection");
        check(uc_hook_add(uc_, &h, UC_HOOK_INTR, reinterpret_cast<void*>(&Emulator::onIntr), this, 1, 0), "hook interrupts");
        ZydisDecoderInit(&dec_, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    }

    ~Emulator()
    {
        if (uc_) uc_close(uc_);
        if (image_) VirtualFree(image_, 0, MEM_RELEASE);
        if (stack_) VirtualFree(stack_, 0, MEM_RELEASE);
    }

    Emulator(const Emulator&)            = delete;
    Emulator& operator=(const Emulator&) = delete;

    /// @brief Maps (or replaces) our relocated code at `base`, next to the image so its rel32 references reach.
    void loadOurs(std::uint64_t base, const Bytes& code)
    {
        if (oursSize_) uc_mem_unmap(uc_, oursBase_, oursSize_);
        oursBase_ = base;
        oursSize_ = roundUp(code.size(), kPage);
        if (uc_mem_map(uc_, base, oursSize_, UC_PROT_ALL) != UC_ERR_OK) throw std::runtime_error("unicorn: cannot map our code at 0x" + hex(base));
        uc_mem_write(uc_, base, code.data(), code.size());
        uc_mem_protect(uc_, base, oursSize_, UC_PROT_READ | UC_PROT_EXEC);   // a stray store faults, as it would in .text
        decoded_.clear();
    }

    /// @brief What the byte at `addr` holds before any run touches it; nullopt where nothing defines it.
    std::optional<std::uint8_t> initialByte(std::uint64_t addr, std::uint64_t seed, const std::set<std::uint64_t>& forced) const
    {
        if (forced.count(addr & ~7ull)) return static_cast<std::uint8_t>(forcedPointer(seed, addr & ~7ull) >> ((addr & 7) * 8));
        if (addr >= pe_.imageBase && addr < pe_.imageBase + span_) return pristine_[addr - pe_.imageBase];
        if (addr < kHeapBase || addr >= kHeapBase + kHeapSpan) return std::nullopt;
        return static_cast<std::uint8_t>(synthQword(seed, addr & ~7ull) >> ((addr & 7) * 8));
    }

    /// @param forced  inputs (memory addresses, argKey, resultKey) that hold a valid pointer whatever the seed says
    RunResult run(std::uint64_t start, std::uint64_t end, const Signature& sig, std::uint64_t seed, const std::set<std::uint64_t>& forced,
                  std::uint64_t budget)
    {
        forced_ = &forced;
        reset(seed, start, end);
        for (const std::uint64_t key : forced)
            if (key >= pe_.imageBase && key + 8 <= pe_.imageBase + span_) {
                const std::uint64_t p = forcedPointer(seed, key);
                std::memcpy(image_ + (key - pe_.imageBase), &p, 8);
                dirtyImage_.insert((key - pe_.imageBase) & ~(kPage - 1));
                dirtyImage_.insert((key + 7 - pe_.imageBase) & ~(kPage - 1));
            }
        static constexpr uc_x86_reg gprs[] = { UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RSI,
                                               UC_X86_REG_RDI, UC_X86_REG_RBP, UC_X86_REG_R8,  UC_X86_REG_R9,  UC_X86_REG_R10,
                                               UC_X86_REG_R11, UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15 };
        for (std::size_t k = 0; k < std::size(gprs); ++k) writeReg(gprs[k], synthQword(seed, 0xFEED0000ull + k));
        for (int k = 0; k < 16; ++k) {
            const std::uint64_t v[2] = { std::uint64_t(synthFloat(seeded(seed, 0xF10A7ull + k))), seeded(seed, 0xF10A8ull + k) };
            uc_reg_write(uc_, UC_X86_REG_XMM0 + k, v);
        }
        entryRsp_ = kStackBase + kStackSize - 0x8000 - 8;
        std::uint64_t sentinel = kSentinel;
        uc_mem_write(uc_, entryRsp_, &sentinel, 8);

        static constexpr uc_x86_reg argRegs[] = { UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9 };
        std::vector<Signature::Kind> args;
        if (sig.hasThis) args.push_back(Signature::Kind::Ref);
        args.insert(args.end(), sig.params.begin(), sig.params.end());
        result_.pointers.clear();
        for (std::size_t k = 0; k < args.size(); ++k) {
            using K = Signature::Kind;
            const std::uint64_t h   = seeded(seed, 0xA4600ull + k);
            const K             kind = args[k];
            std::uint64_t       v    = 0;
            switch (kind) {
                case K::Ref:    v = heapPointer(h); break;
                case K::Ptr:    v = (h % 10 == 0) ? 0 : heapPointer(h); break;
                case K::Bool:   v = h & 1; break;
                case K::Float:  v = synthFloat(h); break;
                case K::Double: { const double d = kFloats[h % std::size(kFloats)]; std::memcpy(&v, &d, 8); break; }
                default:        v = kInts[h % std::size(kInts)]; break;
            }
            if (forced.count(argKey(k))) v = forcedPointer(seed, argKey(k));
            recent_.emplace_back(v, argKey(k));
            if ((kind == K::Ref || kind == K::Ptr) && v)
                result_.pointers.emplace_back(sig.hasThis && k == 0 ? std::string("this") : "arg" + std::to_string(sig.hasThis ? k - 1 : k), v);
            const bool fp = kind == K::Float || kind == K::Double;
            if (k < 4) {
                if (fp) {
                    const std::uint64_t x[2] = { v, 0 };
                    uc_reg_write(uc_, UC_X86_REG_XMM0 + int(k), x);
                    xmmDefined_[k] = true;
                } else {
                    writeReg(argRegs[k], v);
                    gprWidth_[k] = 8;
                }
            } else {
                uc_mem_write(uc_, entryRsp_ + 8 + 8 * k, &v, 8);
            }
        }
        static constexpr uc_x86_reg saved[] = { UC_X86_REG_RBX, UC_X86_REG_RBP, UC_X86_REG_RDI, UC_X86_REG_RSI,
                                                UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15 };
        std::array<std::uint64_t, std::size(saved)> savedGpr{};
        for (std::size_t k = 0; k < std::size(saved); ++k) savedGpr[k] = readReg(saved[k]);
        std::array<std::array<std::uint64_t, 2>, 10> savedXmm{};
        for (int k = 0; k < 10; ++k) uc_reg_read(uc_, UC_X86_REG_XMM6 + k, savedXmm[k].data());

        writeReg(UC_X86_REG_RSP, entryRsp_);
        writeReg(UC_X86_REG_RIP, start);
        writeReg(UC_X86_REG_EFLAGS, 0x202);
        writeReg(UC_X86_REG_GS_BASE, heapPointer(seeded(seed, 0x6573ull)));
        std::uint64_t mxcsr = 0x1F80;
        uc_reg_write(uc_, UC_X86_REG_MXCSR, &mxcsr);

        const uc_err err = uc_emu_start(uc_, start, kSentinel, 0, budget);
        const std::uint64_t pc = readReg(UC_X86_REG_RIP);
        if (!stopReason_.empty())             { result_.end = RunResult::End::Fault; result_.fault = stopReason_; }
        else if (err != UC_ERR_OK)            { result_.end = RunResult::End::Fault; result_.fault = uc_strerror(err) + (faultAt_.empty() ? std::string() : " " + faultAt_) + " at +0x" + hex(pc - start); }
        else if (pc == kSentinel)             result_.end = RunResult::End::Returned;
        else                                  result_.end = RunResult::End::Budget;
        if (runaway_) result_.end = RunResult::End::Budget;

        if (result_.end == RunResult::End::Returned) {
            result_.preserved = readReg(UC_X86_REG_RSP) == entryRsp_ + 8;
            for (std::size_t k = 0; k < std::size(saved); ++k) result_.preserved = result_.preserved && readReg(saved[k]) == savedGpr[k];
            for (int k = 0; k < 10; ++k) {
                std::array<std::uint64_t, 2> x{};
                uc_reg_read(uc_, UC_X86_REG_XMM6 + k, x.data());
                result_.preserved = result_.preserved && x == savedXmm[k];
            }
        }
        result_.rax = readReg(UC_X86_REG_RAX);
        std::uint64_t x0[2] = {};
        uc_reg_read(uc_, UC_X86_REG_XMM0, x0);
        result_.xmm0 = x0[0];
        for (const std::uint64_t a : writtenAddrs_) {
            std::uint8_t byte = 0;
            if (uc_mem_read(uc_, a, &byte, 1) == UC_ERR_OK) result_.written[a] = byte;
        }
        return std::move(result_);
    }

private:
    struct Decoded { ZydisDecodedInstruction ins{}; ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT]{}; bool ok = false; };

    void reset(std::uint64_t seed, std::uint64_t start, std::uint64_t end)
    {
        for (const std::uint64_t page : dirtyImage_) std::memcpy(image_ + page, pristine_.data() + page, kPage);
        dirtyImage_.clear();
        for (const std::uint64_t page : heapPages_) uc_mem_unmap(uc_, page, kPage);
        heapPages_.clear();
        std::memset(stack_, 0, kStackSize);
        result_ = RunResult{};
        writtenAddrs_.clear();
        stackWritten_.clear();
        gprWidth_.fill(0);
        xmmDefined_.fill(false);
        seed_ = seed; start_ = start; end_ = end; entered_ = false;
        stopReason_.clear(); faultAt_.clear();
        recent_.clear();
        outRegions_.clear();
        stackTouched_.clear();
        pendingJcc_ = false; runaway_ = false;
        tsc_ = 0;
    }

    std::uint64_t readReg(int r) const { std::uint64_t v = 0; uc_reg_read(uc_, r, &v); return v; }
    void          writeReg(int r, std::uint64_t v) { uc_reg_write(uc_, r, &v); }

    const Decoded& decode(std::uint64_t pc, std::uint32_t size)
    {
        auto [it, fresh] = decoded_.try_emplace(pc);
        if (fresh) {
            std::uint8_t buf[16] = {};
            uc_mem_read(uc_, pc, buf, std::min<std::uint32_t>(size, 16));
            it->second.ok = ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec_, buf, std::min<std::uint32_t>(size, 16), &it->second.ins, it->second.ops));
        }
        return it->second;
    }

    void define(ZydisRegister reg)
    {
        const auto cls = ZydisRegisterGetClass(reg);
        if (cls == ZYDIS_REGCLASS_XMM || cls == ZYDIS_REGCLASS_YMM || cls == ZYDIS_REGCLASS_ZMM) {
            const auto id = ZydisRegisterGetId(reg);
            if (id < 4) xmmDefined_[std::size_t(id)] = true;
            return;
        }
        std::uint8_t width = 0;
        switch (cls) {
            case ZYDIS_REGCLASS_GPR8:  width = 1; break;
            case ZYDIS_REGCLASS_GPR16: width = 2; break;
            case ZYDIS_REGCLASS_GPR32:
            case ZYDIS_REGCLASS_GPR64: width = 8; break;   // a 32-bit write zero-extends
            default:                   return;
        }
        switch (ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg)) {
            case ZYDIS_REGISTER_RCX: gprWidth_[0] = std::max(gprWidth_[0], width); break;
            case ZYDIS_REGISTER_RDX: gprWidth_[1] = std::max(gprWidth_[1], width); break;
            case ZYDIS_REGISTER_R8:  gprWidth_[2] = std::max(gprWidth_[2], width); break;
            case ZYDIS_REGISTER_R9:  gprWidth_[3] = std::max(gprWidth_[3], width); break;
            default: break;
        }
    }

    void inside(std::uint64_t pc, std::uint32_t size)
    {
        const auto off = static_cast<std::uint32_t>(pc - start_);
        result_.executed.insert(off);
        if (pendingJcc_) { result_.branches[jccOff_] |= (pc == jccFall_) ? 1 : 2; pendingJcc_ = false; }
        const Decoded& d = decode(pc, size);
        if (!d.ok) return;
        if (d.ins.meta.category == ZYDIS_CATEGORY_COND_BR) { pendingJcc_ = true; jccOff_ = off; jccFall_ = pc + d.ins.length; }
        if (isFloatCompare(d.ins.mnemonic)) {
            const bool packed = d.ins.mnemonic == ZYDIS_MNEMONIC_MINPS || d.ins.mnemonic == ZYDIS_MNEMONIC_MAXPS || d.ins.mnemonic == ZYDIS_MNEMONIC_CMPPS;
            const bool dbl    = d.ins.mnemonic == ZYDIS_MNEMONIC_COMISD || d.ins.mnemonic == ZYDIS_MNEMONIC_UCOMISD;
            for (std::uint8_t k = 0; k < d.ins.operand_count_visible; ++k) {
                if (d.ops[k].type != ZYDIS_OPERAND_TYPE_REGISTER || ZydisRegisterGetClass(d.ops[k].reg.value) != ZYDIS_REGCLASS_XMM) continue;
                std::uint32_t lanes[4] = {};
                uc_reg_read(uc_, UC_X86_REG_XMM0 + ZydisRegisterGetId(d.ops[k].reg.value), lanes);
                if (dbl) {
                    const std::uint64_t q = lanes[0] | (std::uint64_t(lanes[1]) << 32);
                    if ((q & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (q & 0x000FFFFFFFFFFFFFull)) result_.nan = true;
                } else {
                    for (int l = 0; l < (packed ? 4 : 1); ++l)
                        if ((lanes[l] & 0x7F800000u) == 0x7F800000u && (lanes[l] & 0x007FFFFFu)) result_.nan = true;
                }
            }
        }
        if (d.ins.mnemonic == ZYDIS_MNEMONIC_RDTSC) {   // the timer markers: deterministic ticks
            writeReg(UC_X86_REG_RAX, 0x1000 * ++tsc_);
            writeReg(UC_X86_REG_RDX, 0);
            writeReg(UC_X86_REG_RIP, pc + d.ins.length);
            return;
        }
        for (std::uint8_t k = 0; k < d.ins.operand_count; ++k)
            if (d.ops[k].type == ZYDIS_OPERAND_TYPE_REGISTER && (d.ops[k].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)) define(d.ops[k].reg.value);
    }

    /// @brief The function under test called (or jumped to) `target`: log what the callee would see, give it results,
    ///        and return to the caller as `ret` would.
    void stub(std::uint64_t target)
    {
        if (result_.calls.size() >= 20000) { runaway_ = true; uc_emu_stop(uc_); return; }
        const std::uint64_t rsp = readReg(UC_X86_REG_RSP);
        std::uint64_t       ret = 0;
        if (uc_mem_read(uc_, rsp, &ret, 8) != UC_ERR_OK) { stopReason_ = "stub at 0x" + hex(target) + " with an unreadable stack"; uc_emu_stop(uc_); return; }
        const std::uint64_t callIndex = result_.calls.size();
        if (trace)
            std::fprintf(stderr, "  stub #%zu 0x%llx ret 0x%llx rsp 0x%llx\n", result_.calls.size(), (unsigned long long)target, (unsigned long long)ret, (unsigned long long)rsp);

        CallRecord c;
        c.target = target;
        static constexpr uc_x86_reg argRegs[] = { UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9 };
        for (std::size_t k = 0; k < 4; ++k) {
            c.gprWidth[k]   = gprWidth_[k];
            c.gpr[k]        = readReg(argRegs[k]);
            c.xmmDefined[k] = xmmDefined_[k];
            std::uint64_t x[2] = {};
            uc_reg_read(uc_, UC_X86_REG_XMM0 + int(k), x);
            c.xmm[k] = x[0];
        }
        std::vector<std::pair<std::uint64_t, std::size_t>> stackPointees;   // (address, argument position)
        for (std::size_t k = 0; k < 4; ++k)
            if (gprWidth_[k] == 8) {
                const std::uint64_t v = readReg(argRegs[k]);
                if (v >= kStackBase && v < kStackBase + kStackSize) stackPointees.emplace_back(v, k);
            }
        for (std::size_t s = 0; s < c.stack.size(); ++s) {
            const std::uint64_t slot  = rsp + 0x28 + 8 * s;
            std::uint8_t        width = 0;
            while (width < 8 && stackWritten_.count(slot + width)) ++width;
            if (width != 1 && width != 2 && width != 4 && width != 8) width = width >= 4 ? 4 : (width >= 2 ? 2 : (width ? 1 : 0));
            c.stackWidth[s] = width;
            std::uint64_t v = 0;
            uc_mem_read(uc_, slot, &v, 8);
            c.stack[s] = v;
            if (width == 8 && v >= kStackBase && v < kStackBase + kStackSize) stackPointees.emplace_back(v, 4 + s);
        }
        // Out-arguments: the callee would write them. Each pointee becomes a region (up to the next pointee, 64 bytes, or
        // the edge of the caller's frame, never its return address); read() fills what the function never wrote itself.
        std::sort(stackPointees.begin(), stackPointees.end());
        for (std::size_t p = 0; p < stackPointees.size(); ++p) {
            const auto [addr, pos] = stackPointees[p];
            if (addr <= rsp) continue;
            std::uint64_t limit = std::min<std::uint64_t>(addr + 64, addr < entryRsp_ ? entryRsp_ : entryRsp_ + 0x28);
            if (p + 1 < stackPointees.size()) limit = std::min(limit, stackPointees[p + 1].first);
            outRegions_.push_back({ addr, limit, 0x0C0000000000ull + (callIndex << 16) + (std::uint64_t(pos) << 8) });
        }
        result_.calls.push_back(c);

        const std::uint64_t result = input(resultKey(callIndex));
        writeReg(UC_X86_REG_RAX, result);
        recent_.emplace_back(result, resultKey(callIndex));
        const std::uint64_t x0[2] = { synthFloat(seeded(seed_, 0x0F100000ull + callIndex)), 0 };
        uc_reg_write(uc_, UC_X86_REG_XMM0, x0);
        static constexpr uc_x86_reg volatileRegs[] = { UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11 };
        for (std::size_t k = 0; k < std::size(volatileRegs); ++k) writeReg(volatileRegs[k], seeded(seed_, 0x0B0B0000ull + (callIndex << 4) + k));
        for (int k = 1; k < 6; ++k) {
            const std::uint64_t x[2] = { seeded(seed_, 0x0B0C0000ull + (callIndex << 4) + k), 0 };
            uc_reg_write(uc_, UC_X86_REG_XMM0 + k, x);
        }
        gprWidth_.fill(0);
        xmmDefined_.fill(false);
        stackWritten_.clear();
        writeReg(UC_X86_REG_RSP, rsp + 8);
        writeReg(UC_X86_REG_RIP, ret);
    }

    /// @note A second arrival at the entry is a call back into the function (recursion, or a pointer that happens to
    ///       equal it): a stub on both sides, since a synthetic call target may land on ours' load address, where
    ///       vanilla's run sees an outside stub.
    void code(std::uint64_t pc, std::uint32_t size)
    {
        if (pc == start_ && entered_) stub(pc);
        else if (pc >= start_ && pc < end_) { entered_ = true; inside(pc, size); }
        else if (pc != kSentinel) stub(pc);
    }

    void write(std::uint64_t addr, int size)
    {
        for (int k = 0; k < size; ++k) {
            const std::uint64_t a = addr + std::uint64_t(k);
            if (a >= kStackBase && a < kStackBase + kStackSize) { stackWritten_.insert(a); stackTouched_.insert(a); continue; }
            writtenAddrs_.insert(a);
            if (a >= pe_.imageBase && a < pe_.imageBase + span_) dirtyImage_.insert((a - pe_.imageBase) & ~(kPage - 1));
        }
    }

    std::uint64_t input(std::uint64_t key) const { return forced_->count(key) ? forcedPointer(seed_, key) : synthQword(seed_, key); }

    /// @brief Remembers where each pointer-sized value came from, so a fault can name the input to force.
    void read(std::uint64_t addr, int size)
    {
        if (addr >= kStackBase && addr < kStackBase + kStackSize) { fillOut(addr, size); return; }
        if (size != 8 || (addr & 7)) return;
        std::uint64_t v = 0;   // the hook runs before the access, so a page not yet materialized reads as what it will get
        if (uc_mem_read(uc_, addr, &v, 8) != UC_ERR_OK) v = input(addr);
        recent_.emplace_back(v, addr);
    }

    /// @brief A stack byte the function never wrote, inside an out-argument of an earlier call, reads as that callee left
    ///        it: a value keyed by call, argument position and offset from the pointee, so both layouts agree.
    void fillOut(std::uint64_t addr, int size)
    {
        for (std::uint64_t x = addr; x < addr + std::uint64_t(size); ++x) {
            if (stackTouched_.count(x)) continue;
            for (auto r = outRegions_.rbegin(); r != outRegions_.rend(); ++r) {
                if (x < r->begin || x >= r->end) continue;
                const std::uint8_t b = static_cast<std::uint8_t>(synthQword(seed_, r->key + (x - r->begin) / 8) >> (((x - r->begin) & 7) * 8));
                uc_mem_write(uc_, x, &b, 1);
                break;
            }
        }
    }

    /// @brief The most recent input whose value the faulting address is a small offset from (a field of a bad pointer).
    std::optional<std::uint64_t> suspect(std::uint64_t addr) const
    {
        for (auto it = recent_.rbegin(); it != recent_.rend(); ++it) {
            // unicorn hands hooks the address cut to 52 bits (no paging, so no canonical-address fault): compare likewise
            const auto d = static_cast<std::int64_t>(((addr - it->first) & ((1ull << 52) - 1)) << 12) >> 12;
            if (d >= -0x100 && d < 0x10000) return it->second;
        }
        return std::nullopt;
    }

    bool unmapped(uc_mem_type type, std::uint64_t addr)
    {
        const char* what = type == UC_MEM_FETCH_UNMAPPED ? "fetch" : type == UC_MEM_WRITE_UNMAPPED ? "write" : "read";
        // Only the synthetic heap materializes: anything else is a wild pointer, which would fault on hardware too.
        if (addr < kHeapBase || addr >= kHeapBase + kHeapSpan || heapPages_.size() >= kMaxPages) {
            faultAt_ = std::string(what) + " 0x" + hex(addr);
            result_.suspect = suspect(addr);
            return false;
        }
        const std::uint64_t page = addr & ~(kPage - 1);
        if (uc_mem_map(uc_, page, kPage, UC_PROT_ALL) != UC_ERR_OK) { faultAt_ = std::string(what) + " 0x" + hex(addr) + " (unmappable)"; return false; }
        std::array<std::uint64_t, kPage / 8> fill{};
        for (std::size_t q = 0; q < fill.size(); ++q) fill[q] = input(page + q * 8);
        uc_mem_write(uc_, page, fill.data(), kPage);
        heapPages_.push_back(page);
        return true;
    }

    static void onCode(uc_engine*, std::uint64_t pc, std::uint32_t size, void* self) { static_cast<Emulator*>(self)->code(pc, size); }
    static void onWrite(uc_engine*, uc_mem_type, std::uint64_t addr, int size, std::int64_t, void* self) { static_cast<Emulator*>(self)->write(addr, size); }
    static void onRead(uc_engine*, uc_mem_type, std::uint64_t addr, int size, std::int64_t, void* self) { static_cast<Emulator*>(self)->read(addr, size); }
    static bool onUnmapped(uc_engine*, uc_mem_type type, std::uint64_t addr, int, std::int64_t, void* self) { return static_cast<Emulator*>(self)->unmapped(type, addr); }
    static bool onProt(uc_engine*, uc_mem_type type, std::uint64_t addr, int, std::int64_t, void* self)
    {
        auto* e    = static_cast<Emulator*>(self);
        e->faultAt_ = std::string(type == UC_MEM_FETCH_PROT ? "fetch" : type == UC_MEM_WRITE_PROT ? "write" : "read") + " protected 0x" + hex(addr);
        e->result_.suspect = e->suspect(addr);
        return false;
    }
    static void onIntr(uc_engine* uc, std::uint32_t n, void* self)
    {
        static_cast<Emulator*>(self)->stopReason_ = "cpu exception " + std::to_string(n);
        uc_emu_stop(uc);
    }

    const PeImage&                          pe_;
    uc_engine*                              uc_ = nullptr;
    std::uint8_t*                           image_ = nullptr;
    std::uint8_t*                           stack_ = nullptr;
    std::uint64_t                           span_ = 0;
    Bytes                                   pristine_;
    std::map<std::uint64_t, std::string>    stubNames_;
    std::uint64_t                           oursBase_ = 0, oursSize_ = 0;
    ZydisDecoder                            dec_{};
    std::map<std::uint64_t, Decoded>        decoded_;

    std::uint64_t                           seed_ = 0, start_ = 0, end_ = 0, entryRsp_ = 0, tsc_ = 0;
    bool                                    entered_ = false;   ///< the run has passed the function's entry once
    RunResult                               result_;
    std::set<std::uint64_t>                 writtenAddrs_, stackWritten_, dirtyImage_;
    std::vector<std::uint64_t>              heapPages_;
    const std::set<std::uint64_t>*          forced_ = nullptr;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> recent_;   ///< (value, input key) in the order the run met them
    struct OutRegion { std::uint64_t begin, end, key; };
    std::vector<OutRegion>                  outRegions_;
    std::set<std::uint64_t>                 stackTouched_;   ///< stack bytes the function itself wrote this run
    std::array<std::uint8_t, 4>             gprWidth_{};
    std::array<bool, 4>                     xmmDefined_{};
    bool                                    pendingJcc_ = false, runaway_ = false;
    std::uint32_t                           jccOff_ = 0;
    std::uint64_t                           jccFall_ = 0;
    std::string                             stopReason_, faultAt_;

public:
    bool trace = false;   ///< print each stub call and each forcing round (--diff-seed)

    std::string stubName(std::uint64_t a) const { const auto it = stubNames_.find(a); return it == stubNames_.end() ? std::string() : it->second; }
};

/// @brief Copies our function's bytes with every relocation applied for code placed at `base`: engine symbols from
///        engine.yaml, compiler constants and string literals at vanilla's copy (the instruction alignment says which),
///        imports at the exe's slot. Returns why not, or empty.
std::string relocateOurs(const CoffFunction& ours, std::uint64_t base, const Bytes& vanilla, std::uint64_t va, const EngineSymbols& syms,
                         const PeImage& pe, Bytes& code, std::map<std::uint64_t, Signature>& callees)
{
    code = ours.bytes;
    std::map<std::uint32_t, std::uint64_t> viaVanilla;
    const auto a = disassemble(codeOnly(ours, oursLayout(ours).codeEnd).bytes);
    const auto b = disassemble(Bytes(vanilla.begin(), vanilla.begin() + vanillaLayout(vanilla, va, pe.imageBase).codeEnd));
    if (a && b)
        for (const auto& [ia, ib] : alignInstructions(*a, *b)) {
            if (ia < 0 || ib < 0) continue;
            const Insn& x = (*a)[std::size_t(ia)];
            const Insn& y = (*b)[std::size_t(ib)];
            const auto  t = absoluteTarget(y, va + y.off);
            if (!t) continue;
            for (std::uint32_t o = x.off; o < x.off + x.ins.length; ++o) if (ours.targets.count(o)) viaVanilla[o] = *t;
        }
    // Image-relative references into this function (jump tables and their entries) resolve against an __ImageBase of
    // `base`, so each is just the function offset; `lea r, [__ImageBase]` gets `base` itself.
    for (const auto& [at, t] : ours.targets) {
        if (t.type == IMAGE_REL_AMD64_ADDR32NB && t.local) {
            const auto v = static_cast<std::uint32_t>(*t.local);
            std::memcpy(code.data() + at, &v, 4);
        }
    }
    for (const auto& [at, t] : ours.targets) {
        std::optional<std::uint64_t> target;
        if (t.type == IMAGE_REL_AMD64_ADDR32NB && t.local) continue;
        if (t.mangled == "__ImageBase") target = base;
        auto                         it = syms.byCpp.find(t.name);
        if (it == syms.byCpp.end()) it = syms.byCpp.find(t.mangled);
        const auto vv = viaVanilla.find(at);
        if (target) {
            // __ImageBase, placed above: the alignment would hand it vanilla's image base
        } else if (it != syms.byCpp.end()) {
            target = it->second;
        } else if (t.mangled.rfind("__imp_", 0) == 0) {
            for (const auto& [slot, name] : pe.importsBySlot) if (name == t.mangled.substr(6)) target = slot;
        } else if (vv != viaVanilla.end()) {
            const auto self = selfCheck(t.mangled, vv->second, pe);
            const bool literal = t.mangled.rfind("??_C@", 0) == 0 || t.name == "`string'";
            if ((self && self->empty()) || literal) target = vv->second;
        }
        if (!target && (t.mangled.rfind("__real@", 0) == 0 || t.mangled.rfind("__xmm@", 0) == 0)) {
            // A compiler constant no vanilla instruction lines up with: its name spells its bytes, so place our own copy
            // after the code, most-significant byte first in the name.
            const std::string digits = t.mangled.substr(t.mangled.find('@') + 1);
            code.resize(roundUp(code.size(), 16));
            target = base + code.size();
            for (std::size_t k = digits.size() / 2; k-- > 0;)
                code.push_back(static_cast<std::uint8_t>(std::stoul(digits.substr(k * 2, 2), nullptr, 16)));
        }
        if (!target) return "cannot place reference " + t.name + " at +0x" + hex(at);
        if (t.mangled.rfind("?", 0) == 0 && !callees.count(*target))
            if (Signature cs = parseSignature(t.mangled); cs.text.find('(') != std::string::npos) callees.emplace(*target, std::move(cs));
        switch (t.type) {
            case IMAGE_REL_AMD64_REL32: case IMAGE_REL_AMD64_REL32_1: case IMAGE_REL_AMD64_REL32_2:
            case IMAGE_REL_AMD64_REL32_3: case IMAGE_REL_AMD64_REL32_4: case IMAGE_REL_AMD64_REL32_5: {
                const std::int64_t addend = rd<std::int32_t>(code, at);
                const std::int64_t value  = std::int64_t(*target) + addend - std::int64_t(base + at + 4 + (t.type - IMAGE_REL_AMD64_REL32));
                if (value < INT32_MIN || value > INT32_MAX) return "reference " + t.name + " out of rel32 reach";
                const auto v32 = static_cast<std::int32_t>(value);
                std::memcpy(code.data() + at, &v32, 4);
                break;
            }
            case IMAGE_REL_AMD64_ADDR64: {
                const std::uint64_t v = *target + rd<std::uint64_t>(code, at);
                std::memcpy(code.data() + at, &v, 8);
                break;
            }
            default: return "relocation type " + std::to_string(t.type) + " (" + t.name + ") not supported";
        }
    }
    return {};
}

struct DiffReport {
    std::size_t seeds = 0, agreed = 0, faultedAlike = 0, inconclusive = 0, diverged = 0;
    std::size_t runs = 0;
    std::size_t nan = 0;   ///< seeds that differed only after a NaN reached a float compare
    std::string firstDivergence, error;
    std::size_t insnsCovered = 0, insnsTotal = 0, sidesCovered = 0, sidesTotal = 0;
    double      ms = 0;
};

std::string nameAddress(std::uint64_t a, const RunResult& r)
{
    for (const auto& [label, p] : r.pointers) if (a >= p && a < p + 0x1000) return label + "+0x" + hex(a - p);
    return "0x" + hex(a);
}

/// @brief Why ours and vanilla behaved differently on one seed, or nullopt when they agree.
std::optional<std::string> divergence(const RunResult& o, const RunResult& v, const Signature& sig, const std::map<std::uint64_t, Signature>& callees,
                                      const Emulator& emu, std::uint64_t seed, const std::set<std::uint64_t>& forced)
{
    using End = RunResult::End;
    auto endName = [](const RunResult& r) { return r.end == End::Returned ? std::string("returned") : r.end == End::Budget ? std::string("ran out of budget") : "faulted (" + r.fault + ")"; };
    if ((o.end == End::Returned) != (v.end == End::Returned)) return "ours " + endName(o) + ", vanilla " + endName(v);
    auto targetName = [&](std::uint64_t t) { const auto n = emu.stubName(t); return "0x" + hex(t) + (n.empty() ? "" : " " + n); };
    const std::size_t common = std::min(o.calls.size(), v.calls.size());
    for (std::size_t k = 0; k < common; ++k) {
        const auto& x = o.calls[k];
        const auto& y = v.calls[k];
        if (x.target != y.target) return "call #" + std::to_string(k) + ": ours calls " + targetName(x.target) + ", vanilla " + targetName(y.target);
        auto differs = [&](const std::string& slot, std::uint64_t a, std::uint64_t b, std::uint8_t w) -> std::optional<std::string> {
            if (observed(a, w) == observed(b, w)) return std::nullopt;
            return "call #" + std::to_string(k) + " to " + targetName(x.target) + ": " + slot + " ours 0x" + hex(lowBytes(a, w)) + ", vanilla 0x" + hex(lowBytes(b, w));
        };
        static const char* gprName[] = { "rcx", "rdx", "r8", "r9" };
        const auto known = callees.find(x.target);
        if (known != callees.end()) {
            // The callee's signature says exactly which slots are arguments and how wide.
            std::vector<Signature::Kind> args;
            if (known->second.hasThis) args.push_back(Signature::Kind::Ref);
            args.insert(args.end(), known->second.params.begin(), known->second.params.end());
            for (std::size_t p = 0; p < args.size() && p < 4 + x.stack.size(); ++p) {
                const auto w  = widthOf(args[p]);
                const bool fp = args[p] == Signature::Kind::Float || args[p] == Signature::Kind::Double;
                std::optional<std::string> why;
                if (p >= 4)  why = differs("stack argument " + std::to_string(p + 1), x.stack[p - 4], y.stack[p - 4], w);
                else if (fp) why = differs("xmm" + std::to_string(p), x.xmm[p], y.xmm[p], w);
                else         why = differs(gprName[p], x.gpr[p], y.gpr[p], w);
                if (why) return why;
            }
        } else if (x.gprWidth[0] && y.gprWidth[0]) {
            // Unknown callee (virtual, import): only rcx, which every call with an argument uses; other registers both
            // sides wrote may just be temporaries.
            if (auto why = differs("rcx", x.gpr[0], y.gpr[0], std::min(x.gprWidth[0], y.gprWidth[0]))) return why;
        }
    }
    if (o.end == End::Fault) return std::nullopt;   // both faulted: the calls up to the fault are all that is ordered
    if (o.calls.size() != v.calls.size())
        return "ours makes " + std::to_string(o.calls.size()) + " calls, vanilla " + std::to_string(v.calls.size()) +
               (o.calls.size() > common ? "; ours goes on to " + targetName(o.calls[common].target) : "; vanilla goes on to " + targetName(v.calls[common].target));
    std::set<std::uint64_t> addrs;
    for (const auto& [a, _] : o.written) addrs.insert(a);
    for (const auto& [a, _] : v.written) addrs.insert(a);
    for (const std::uint64_t a : addrs) {
        const auto ox = o.written.count(a) ? std::optional<std::uint8_t>(o.written.at(a)) : emu.initialByte(a, seed, forced);
        const auto vx = v.written.count(a) ? std::optional<std::uint8_t>(v.written.at(a)) : emu.initialByte(a, seed, forced);
        if (ox != vx) {
            auto show = [](std::optional<std::uint8_t> b) { return b ? "0x" + hex(*b) : std::string("?"); };
            return "memory " + nameAddress(a, v) + ": ours " + show(ox) + ", vanilla " + show(vx) +
                   (o.written.count(a) ? "" : " (ours never writes it)") + (v.written.count(a) ? "" : " (vanilla never writes it)");
        }
    }
    using K = Signature::Kind;
    std::uint64_t mask = 0;
    bool          fp   = false;
    switch (sig.ret) {
        case K::Void:   break;
        case K::Bool: case K::Int8: mask = 0xFF; break;
        case K::Int16:  mask = 0xFFFF; break;
        case K::Int32:  mask = 0xFFFFFFFF; break;
        case K::Float:  mask = 0xFFFFFFFF; fp = true; break;
        case K::Double: mask = ~0ull; fp = true; break;
        default:        mask = ~0ull; break;
    }
    if (mask) {
        const std::uint64_t x = (fp ? o.xmm0 : o.rax) & mask, y = (fp ? v.xmm0 : v.rax) & mask;
        if (x != y) return std::string("return value: ours 0x") + hex(x) + ", vanilla 0x" + hex(y);
    }
    if (!o.preserved && v.preserved) return "ours does not restore a callee-saved register or rsp";
    return std::nullopt;
}

DiffReport diffFunction(Emulator& emu, const CoffFunction& ours, const Bytes& vanilla, std::uint64_t va, const EngineSymbols& syms,
                        const PeImage& pe, std::size_t seeds, std::optional<std::uint64_t> onlySeed)
{
    DiffReport    rep;
    const auto    t0   = std::chrono::steady_clock::now();
    const auto    base = (pe.imageBase - 0x01000000ull) & ~(kPage - 1);
    Bytes                             code;
    std::map<std::uint64_t, Signature> callees;
    if (auto why = relocateOurs(ours, base, vanilla, va, syms, pe, code, callees); !why.empty()) { rep.error = why; return rep; }
    emu.loadOurs(base, code);
    const Signature sig = parseSignature(ours.mangled);
    if (sig.text.empty()) { rep.error = "cannot read the signature of " + ours.mangled; return rep; }

    std::set<std::uint32_t>               executed;
    std::map<std::uint32_t, std::uint8_t> sides;
    for (std::uint64_t s = 1; s <= seeds; ++s) {
        if (onlySeed && s != *onlySeed) continue;
        ++rep.seeds;
        // A fault on a bad pointer just ends the run early: force the input it came from to a valid pointer and run the
        // seed again, so the seeds explore paths past the dereferences rather than dying on them.
        std::set<std::uint64_t> forced;
        RunResult               v;
        for (int round = 0;; ++round) {
            v = emu.run(va, va + vanilla.size(), sig, s, forced, kBudget);
            ++rep.runs;
            if (emu.trace)
                std::fprintf(stderr, "seed %llu round %d: end %d %s suspect %s calls %zu\n", (unsigned long long)s, round, int(v.end), v.fault.c_str(),
                             v.suspect ? hex(*v.suspect).c_str() : "-", v.calls.size());
            if (v.end != RunResult::End::Fault || !v.suspect || forced.count(*v.suspect) || round == 24) break;
            forced.insert(*v.suspect);
        }
        const RunResult o = emu.run(base, base + ours.bytes.size(), sig, s, forced, kBudget);
        ++rep.runs;
        executed.insert(v.executed.begin(), v.executed.end());
        for (const auto& [off, bits] : v.branches) sides[off] |= bits;
        if (o.end == RunResult::End::Budget || v.end == RunResult::End::Budget) { ++rep.inconclusive; continue; }
        if (const auto why = divergence(o, v, sig, callees, emu, s, forced)) {
            if (o.nan || v.nan) { ++rep.nan; continue; }   // /fp:fast assumes no NaN, so code may differ on one
            if (rep.diverged++ == 0) rep.firstDivergence = "seed " + std::to_string(s) + ": " + *why;
            continue;
        }
        (o.end == RunResult::End::Fault ? rep.faultedAlike : rep.agreed)++;
    }
    if (const auto insns = disassemble(Bytes(vanilla.begin(), vanilla.begin() + vanillaLayout(vanilla, va, pe.imageBase).codeEnd))) {
        rep.insnsTotal = insns->size();
        for (const auto& i : *insns) {
            if (executed.count(i.off)) ++rep.insnsCovered;
            if (i.ins.meta.category == ZYDIS_CATEGORY_COND_BR) {
                rep.sidesTotal += 2;
                const auto it = sides.find(i.off);
                if (it != sides.end()) rep.sidesCovered += ((it->second & 1) ? 1 : 0) + ((it->second & 2) ? 1 : 0);
            }
        }
    }
    rep.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return rep;
}

// ── driver ───────────────────────────────────────────────────────────────────────────────────────────────────────

// ── inventory: what there is to match ─────────────────────────────────────────────────────────────────────────────

/// @brief `*` matches any run of characters; everything else literally (names are compared exactly, case included).
bool globMatch(std::string_view pattern, std::string_view s)
{
    if (pattern.empty()) return s.empty();
    if (pattern.front() == '*') {
        for (std::size_t k = 0; k <= s.size(); ++k)
            if (globMatch(pattern.substr(1), s.substr(k))) return true;
        return false;
    }
    return !s.empty() && pattern.front() == s.front() && globMatch(pattern.substr(1), s.substr(1));
}

/// @brief A function as .pdata bounds it: chained unwind entries (a function's later fragments) fold into it.
struct ExeFunction {
    std::uint32_t rva = 0, end = 0;
};

std::vector<ExeFunction> exeFunctions(const PeImage& pe)
{
    std::vector<ExeFunction> out;
    const auto               base = pe.fileOffset(pe.pdataRva, pe.pdataSize);
    if (!base) return out;
    for (std::size_t i = 0; i < pe.pdataSize / 12; ++i) {
        const std::size_t   o     = *base + i * 12;
        const std::uint32_t begin = rd<std::uint32_t>(pe.bytes, o), end = rd<std::uint32_t>(pe.bytes, o + 4);
        const std::uint32_t info  = rd<std::uint32_t>(pe.bytes, o + 8);
        const auto          u     = pe.fileOffset(info & ~1u, 1);
        const bool          chain = (info & 1u) || (u && ((pe.bytes[*u] >> 3) & UNW_FLAG_CHAININFO));
        if (chain && !out.empty()) out.back().end = std::max(out.back().end, end);
        else                       out.push_back({ begin, end });
    }
    return out;
}

bool executableRva(const PeImage& pe, std::uint64_t rva)
{
    for (const auto& s : pe.sections)
        if (s.executable() && rva >= s.va && rva < s.va + std::max(s.vsize, s.rawSize)) return true;
    return false;
}

/// @brief Direct call / jmp rel32 edges between functions: callee rva → the rvas of the functions that reach it.
/// @note A target need not have a .pdata entry: leaf functions that never touch the stack have none. Indirect calls
///       (vtables, function pointers) are not edges; vtable slots are found from RTTI instead.
std::map<std::uint32_t, std::set<std::uint32_t>> directCallers(const PeImage& pe, const std::vector<ExeFunction>& fns)
{
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> edges(fns.size());
    CB::engine::exec::ParallelFor(fns.size(), [&](std::size_t k) {
        const auto& f   = fns[k];
        const auto  off = pe.fileOffset(f.rva, f.end - f.rva);
        if (!off) return;
        ZydisDecoder dec;
        ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        for (std::uint32_t p = 0; p < f.end - f.rva;) {
            ZydisDecodedInstruction ins;
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&dec, nullptr, pe.bytes.data() + *off + p, f.end - f.rva - p, &ins))) { ++p; continue; }
            if ((ins.mnemonic == ZYDIS_MNEMONIC_CALL || ins.mnemonic == ZYDIS_MNEMONIC_JMP) && ins.raw.imm[0].is_relative &&
                ins.raw.imm[0].size == 32) {
                const std::int64_t target = std::int64_t(f.rva) + p + ins.length + ins.raw.imm[0].value.s;
                if (target >= 0 && (target < f.rva || target >= f.end) && executableRva(pe, std::uint64_t(target)))
                    edges[k].emplace_back(std::uint32_t(target), f.rva);
            }
            p += ins.length;
        }
    });
    std::map<std::uint32_t, std::set<std::uint32_t>> callers;
    for (const auto& list : edges)
        for (const auto& [to, from] : list) callers[to].insert(from);
    return callers;
}

/// @brief A polymorphic class as MSVC's RTTI describes it, with its primary vtable.
struct RttiClass {
    std::string                name, parent;
    std::uint32_t              vtable = 0;   ///< rva of slot 0
    std::vector<std::uint32_t> slots;        ///< function rvas
};

/// @brief Every class with a primary vtable: complete object locators (found by their self-rva), their type and
///        hierarchy descriptors, and the vtable whose slot -1 points at the locator.
/// @note A name inside a namespace or template is left out — Havok's and Bethesda's runtime classes are global.
std::map<std::string, RttiClass> rttiClasses(const PeImage& pe)
{
    auto cstrAt = [&](std::uint32_t rva) -> std::string {
        const auto o = pe.fileOffset(rva, 1);
        if (!o) return {};
        const char* p = reinterpret_cast<const char*>(pe.bytes.data() + *o);
        return std::string(p, strnlen(p, pe.bytes.size() - *o));
    };
    auto typeName = [&](std::uint32_t td) -> std::string {
        const std::string raw = cstrAt(td + 0x10);
        if (raw.rfind(".?AV", 0) != 0 && raw.rfind(".?AU", 0) != 0) return {};
        const std::string body = raw.substr(4);
        if (body.size() < 3 || body.compare(body.size() - 2, 2, "@@") != 0) return {};
        const std::string name = body.substr(0, body.size() - 2);
        return (name.find('@') != std::string::npos || name.find('?') != std::string::npos) ? std::string() : name;
    };
    auto dword = [&](std::uint32_t rva) -> std::optional<std::uint32_t> {
        const auto o = pe.fileOffset(rva, 4);
        if (!o) return std::nullopt;
        return rd<std::uint32_t>(pe.bytes, *o);
    };
    auto inExecutable = [&](std::uint64_t va) {
        if (va < pe.imageBase) return false;
        const std::uint64_t rva = va - pe.imageBase;
        for (const auto& s : pe.sections)
            if (s.executable() && rva >= s.va && rva < s.va + std::max(s.vsize, s.rawSize)) return true;
        return false;
    };

    std::map<std::uint64_t, RttiClass> byLocator;   ///< locator va → class (primary locators only)
    for (const auto& s : pe.sections) {
        if (s.executable()) continue;
        for (std::uint32_t r = 0; r + 0x18 <= s.rawSize; r += 4) {
            const std::size_t o = s.raw + r;
            if (rd<std::uint32_t>(pe.bytes, o) != 1 || rd<std::uint32_t>(pe.bytes, o + 0x14) != s.va + r) continue;
            if (rd<std::uint32_t>(pe.bytes, o + 4) != 0) continue;   // a secondary base's vtable
            RttiClass c;
            c.name = typeName(rd<std::uint32_t>(pe.bytes, o + 0xC));
            if (c.name.empty()) continue;
            const std::uint32_t chd = rd<std::uint32_t>(pe.bytes, o + 0x10);
            const auto          nb  = dword(chd + 8);
            const auto          bca = dword(chd + 0xC);
            if (nb && bca && *nb > 1)
                if (const auto bcd = dword(*bca + 4))
                    if (const auto td = dword(*bcd)) c.parent = typeName(*td);
            byLocator[pe.imageBase + s.va + r] = std::move(c);
        }
    }
    std::map<std::string, RttiClass> out;
    for (const auto& s : pe.sections) {
        if (s.executable()) continue;
        for (std::uint32_t r = 0; r + 16 <= s.rawSize; r += 8) {
            const auto it = byLocator.find(rd<std::uint64_t>(pe.bytes, s.raw + r));
            if (it == byLocator.end() || out.count(it->second.name)) continue;
            RttiClass c = it->second;
            c.vtable    = s.va + r + 8;
            for (std::uint32_t q = r + 8; q + 8 <= s.rawSize; q += 8) {
                const std::uint64_t v = rd<std::uint64_t>(pe.bytes, s.raw + q);
                if (!inExecutable(v)) break;
                c.slots.push_back(std::uint32_t(v - pe.imageBase));
            }
            out.emplace(c.name, std::move(c));
        }
    }
    return out;
}

/// @brief One function of the inventory.
struct InventoryFunction {
    std::uint64_t va = 0;
    std::uint32_t size = 0;
    int           slot = -1;              ///< vtable slot, or -1 for a non-virtual member
    std::string   via;                    ///< non-virtual: tu (inside the class's address range) or call (only its code calls it)
    std::uint64_t id = 0;                 ///< Address Library id (0 = none)
    std::vector<std::string> classes;     ///< shared functions: every class that uses it
};

/// @brief Writes the inventory: every in-scope class's own functions (its vtable slots that differ from its parent's,
///        then helpers whose every direct caller is already that class's), and the functions several classes share
///        (one body the linker folded into several vtables, or a helper called from several classes).
int buildInventory(const Manifest& m, const RuntimeSpec& rt)
{
    if (m.inventory.file.empty()) throw std::runtime_error("the manifest has no inventory: { file: ... }");
    const PeImage pe = loadPe(rt.exe);
    std::map<std::uint64_t, std::uint64_t> idByOffset;
    if (!rt.addressLibrary.empty())
        for (const auto& [id, off] : loadAddressLibrary(rt.addressLibrary)) idByOffset.emplace(off, id);

    std::set<std::string> schemaClasses;
    if (!m.inventory.schema.empty())
        for (const auto& e : fs::recursive_directory_iterator(m.inventory.schema))
            if (e.is_regular_file() && e.path().extension() == ".yaml") schemaClasses.insert(e.path().stem().string());
    auto inScope = [&](const std::string& name) {
        for (const auto& x : m.inventory.exclude) if (globMatch(x, name)) return false;
        if (schemaClasses.count(name)) return true;
        for (const auto& g : m.inventory.include) if (globMatch(g, name)) return true;
        return false;
    };

    const auto fns     = exeFunctions(pe);
    const auto callers = directCallers(pe, fns);
    const auto classes = rttiClasses(pe);

    // Every function start: .pdata's, plus direct-call targets and vtable slots (leaf functions have no .pdata entry;
    // their end is the first ret / jmp no earlier forward branch reaches past, never past the next start).
    std::map<std::uint32_t, std::uint32_t> endOf;
    for (const auto& f : fns) endOf[f.rva] = f.end;
    for (const auto& [callee, from] : callers) endOf.emplace(callee, 0);
    for (const auto& [name, c] : classes)
        for (const auto s : c.slots) endOf.emplace(s, 0);
    {
        ZydisDecoder dec;
        ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        for (auto it = endOf.begin(); it != endOf.end(); ++it) {
            if (it->second) continue;
            const auto          next  = std::next(it);
            const std::uint32_t limit = next == endOf.end() ? it->first + 0x1000 : next->first;
            const auto          off   = pe.fileOffset(it->first, limit - it->first);
            std::uint32_t       p = 0, reach = 0;   ///< reach: the furthest forward branch target inside the function
            for (; off && it->first + p < limit;) {
                ZydisDecodedInstruction ins;
                if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&dec, nullptr, pe.bytes.data() + *off + p, limit - it->first - p, &ins))) break;
                p += ins.length;
                if (ins.raw.imm[0].is_relative && ins.mnemonic != ZYDIS_MNEMONIC_CALL) {
                    const std::int64_t t = std::int64_t(p) + ins.raw.imm[0].value.s;
                    if (t > std::int64_t(p) && it->first + t < limit) reach = std::max(reach, std::uint32_t(t));
                }
                if ((ins.mnemonic == ZYDIS_MNEMONIC_RET || ins.mnemonic == ZYDIS_MNEMONIC_JMP || ins.mnemonic == ZYDIS_MNEMONIC_INT3) && p >= reach)
                    break;
            }
            it->second = it->first + p;
        }
    }
    std::map<std::uint32_t, std::uint32_t> sizeOf;
    for (const auto& [rva, end] : endOf) sizeOf[rva] = end - rva;

    // A slot belongs to a class when its parent's vtable doesn't hold the same function there.
    std::map<std::uint32_t, std::set<std::string>> claims;   ///< function rva → classes whose own slot it is
    std::map<std::pair<std::string, std::uint32_t>, int> slotOf;
    for (const auto& [name, c] : classes) {
        const RttiClass* parent = c.parent.empty() ? nullptr : (classes.count(c.parent) ? &classes.at(c.parent) : nullptr);
        for (std::size_t i = 0; i < c.slots.size(); ++i) {
            if (parent && i < parent->slots.size() && parent->slots[i] == c.slots[i]) continue;
            claims[c.slots[i]].insert(name);
            slotOf.emplace(std::make_pair(name, c.slots[i]), int(i));
        }
    }
    std::map<std::uint32_t, std::string> owner;
    std::map<std::uint32_t, std::set<std::string>> shared;
    for (const auto& [rva, names] : claims) {
        bool allInScope = true;
        for (const auto& n : names) allInScope = allInScope && inScope(n);
        if (names.size() == 1 && allInScope) owner[rva] = *names.begin();
        else if (names.size() > 1 && allInScope) shared[rva] = names;
    }
    // A translation unit's functions sit together in .text, so a class's code is the address range around its own
    // slots: the densest cluster of them (gaps under 16 KiB), which drops slots compiled elsewhere. Unowned functions
    // inside it are the class's non-virtual members, whoever else calls them.
    std::map<std::uint32_t, std::set<std::string>> inRange;
    std::map<std::uint32_t, std::string>           via;   ///< how a function was attributed: slot / tu / call
    for (const auto& [rva, name] : owner) via[rva] = "slot";
    std::map<std::string, std::vector<std::uint32_t>> ownSlots;
    for (const auto& [rva, name] : owner) ownSlots[name].push_back(rva);
    for (auto& [name, list] : ownSlots) {
        std::sort(list.begin(), list.end());
        std::size_t bestBegin = 0, bestLen = 0;
        for (std::size_t i = 0; i < list.size();) {
            std::size_t j = i + 1;
            while (j < list.size() && list[j] - list[j - 1] < 0x4000) ++j;
            if (j - i > bestLen) { bestBegin = i; bestLen = j - i; }
            i = j;
        }
        const std::uint32_t lo = list[bestBegin], hi = list[bestBegin + bestLen - 1];
        for (auto it = endOf.lower_bound(lo); it != endOf.end() && it->first <= hi; ++it) inRange[it->first].insert(name);
    }
    for (const auto& [rva, names] : inRange) {
        if (owner.count(rva) || claims.count(rva) || shared.count(rva)) continue;
        if (names.size() == 1) { owner[rva] = *names.begin(); via[rva] = "tu"; }
        else                   shared[rva] = names;
    }

    // What's left: a function every direct caller of which already belongs to one class belongs to it too.
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [callee, from] : callers) {
            if (owner.count(callee) || claims.count(callee) || shared.count(callee)) continue;
            std::set<std::string> by;
            bool                  allOwned = true;
            for (const auto c : from) {
                const auto it = owner.find(c);
                if (it == owner.end()) { allOwned = false; break; }
                by.insert(it->second);
            }
            if (!allOwned) continue;
            if (by.size() == 1) { owner[callee] = *by.begin(); via[callee] = "call"; }
            else                shared[callee] = by;
            grew = true;
        }
    }

    std::map<std::string, std::vector<InventoryFunction>> byClass;
    for (const auto& [rva, name] : owner) {
        InventoryFunction f;
        f.va   = pe.imageBase + rva;
        f.size = sizeOf.count(rva) ? sizeOf[rva] : 0;
        const auto s = slotOf.find({ name, rva });
        f.slot = s == slotOf.end() ? -1 : s->second;
        f.id   = idByOffset.count(rva) ? idByOffset[rva] : 0;
        if (f.slot < 0) f.via = via[rva];
        byClass[name].push_back(f);
    }
    std::size_t total = 0;
    for (const auto& [n, list] : byClass) total += list.size();

    std::string y;
    y += "# Generated by `cbmatch --inventory` from the exe's RTTI, vtables and direct calls; do not edit, regenerate.\n";
    y += "# A class owns its vtable slots that differ from its parent's (slot); then the unowned functions inside the\n";
    y += "# address range of its densest cluster of slots, since a translation unit's functions sit together (tu); then\n";
    y += "# whatever is left whose every direct caller it already owns (call). Sizes without a .pdata entry run to the first\n";
    y += "# ret / jmp. Not found: functions reached only through data outside every class's range (finish-loaded\n";
    y += "# constructors, callbacks).\n";
    y += "runtime: " + rt.name + "\n";
    y += "summary: { classes: " + std::to_string(byClass.size()) + ", functions: " + std::to_string(total) + ", shared: " +
         std::to_string(shared.size()) + " }\n";
    y += "classes:\n";
    for (const auto& [name, list] : byClass) {
        const auto& c = classes.count(name) ? classes.at(name) : RttiClass{};
        y += "  " + name + ":\n";
        if (!c.parent.empty()) y += "    parent: " + c.parent + "\n";
        if (c.vtable) y += "    vtable: 0x" + hex(pe.imageBase + c.vtable) + "\n";
        y += "    functions:\n";
        for (const auto& f : list) {
            y += "      - { va: 0x" + hex(f.va) + ", size: " + std::to_string(f.size);
            if (f.slot >= 0) y += ", slot: " + std::to_string(f.slot);
            else             y += ", via: " + f.via;
            if (f.id) y += ", id: " + std::to_string(f.id);
            y += " }\n";
        }
    }
    y += "shared:\n";
    for (const auto& [rva, names] : shared) {
        y += "  - { va: 0x" + hex(pe.imageBase + rva) + ", size: " + std::to_string(sizeOf.count(rva) ? sizeOf[rva] : 0);
        if (idByOffset.count(rva)) y += ", id: " + std::to_string(idByOffset[rva]);
        y += ", classes: [";
        bool first = true;
        for (const auto& n : names) { y += (first ? "" : ", ") + n; first = false; }
        y += "] }\n";
    }
    std::string err;
    if (!CB::engine::disk::WriteFile(m.inventory.file, y, {}, &err)) throw std::runtime_error("cannot write inventory: " + err);
    std::printf("cbmatch: inventory of %zu classes, %zu functions (%zu shared) -> %s\n", byClass.size(), total, shared.size(),
                m.inventory.file.string().c_str());
    return 0;
}

/// @brief The inventory as written: class → its functions' addresses (and their slot / size).
struct Inventory {
    std::map<std::string, std::vector<InventoryFunction>> classes;
    std::map<std::uint64_t, std::string>                  parked;   ///< va → why it is set aside
};

Inventory loadInventory(const InventorySpec& spec)
{
    Inventory inv;
    auto      parse = [](const fs::path& p) {
        auto text = CB::engine::disk::ReadFile(p, { .stripBom = true });
        if (!text) throw std::runtime_error("cannot read " + p.string());
        return std::make_pair(std::move(*text), c4::yml::Tree());
    };
    ensureRymlThrows();
    {
        auto [text, tree] = parse(spec.file);
        tree              = c4::yml::parse_in_place(c4::to_substr(text));
        const auto root   = tree.crootref();
        if (has(root, "classes"))
            for (const auto c : root["classes"].children()) {
                auto& list = inv.classes[str(c.key())];
                for (const auto f : c["functions"].children()) {
                    InventoryFunction fn;
                    fn.va   = parseAddress(val(f["va"]));
                    fn.size = static_cast<std::uint32_t>(std::stoul(val(f["size"])));
                    fn.slot = has(f, "slot") ? std::stoi(val(f["slot"])) : -1;
                    fn.via  = has(f, "via") ? val(f["via"]) : std::string();
                    list.push_back(fn);
                }
            }
    }
    if (!spec.parked.empty() && fs::exists(spec.parked)) {
        auto [text, tree] = parse(spec.parked);
        tree              = c4::yml::parse_in_place(c4::to_substr(text));
        const auto root   = tree.crootref();
        if (has(root, "parked"))
            for (const auto p : root["parked"].children()) inv.parked[parseAddress(val(p["va"]))] = val(p["why"]);
    }
    return inv;
}

/// @brief The functions of `cls` the manifest doesn't list yet and the ledger hasn't parked.
int printTodo(const Manifest& m, const std::string& runtime, const std::string& cls)
{
    const Inventory inv = loadInventory(m.inventory);
    const auto      it  = inv.classes.find(cls);
    if (it == inv.classes.end()) throw std::runtime_error("the inventory has no class " + cls);
    std::set<std::uint64_t> listed;
    for (const auto& u : m.units)
        for (const auto& f : u.functions)
            if (const auto va = f.va.find(runtime); va != f.va.end()) listed.insert(va->second);
    std::size_t todo = 0;
    for (const auto& f : it->second) {
        if (listed.count(f.va)) continue;
        const auto p = inv.parked.find(f.va);
        std::printf("  0x%s  %6u bytes  %s%s\n", hex(f.va).c_str(), f.size, f.slot >= 0 ? ("slot " + std::to_string(f.slot)).c_str() : f.via.c_str(),
                    p == inv.parked.end() ? "" : ("  [parked: " + p->second + "]").c_str());
        if (p == inv.parked.end()) ++todo;
    }
    std::printf("%s: %zu of %zu functions to do\n", cls.c_str(), todo, it->second.size());
    return 0;
}

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
        "  --allow-variant      exit 0 when every function is EXACT, REGALLOC or VARIANT; a VARIANT that needs rule A\n"
        "                       (local stack slots), E (SIMD schedule), I (a duplicated return tail) or J (register setup\n"
        "                       schedule) always runs --diff (>= 256 seeds) and is STRUCTURAL if a seed diverges or none\n"
        "                       agrees\n"
        "  --suggest            print engine.yaml lines (with Address Library ids) for unmapped references\n"
        "  --inventory          (re)generate the manifest's inventory from the exe instead of matching\n"
        "  --todo <class>       list that class's inventory functions the manifest doesn't cover yet (no compile)\n"
        "  --coverage           after a full run, list every inventory class (default: only those started)\n"
        "  --diff <seeds>       run each REGALLOC/VARIANT function and vanilla side by side under emulation on <seeds>\n"
        "                       synthetic inputs, comparing calls, memory writes and results (covers VARIANT's blind spots)\n"
        "  --diff-seed <n>      run only seed <n> of --diff, printing every stubbed call and forced pointer\n"
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
        "  inventory:                                   # optional: what there is to match, for --inventory/--todo/coverage\n"
        "    file:    inventory.yaml                    # generated\n"
        "    parked:  parked.yaml                       # hand-kept: parked: [ { va: 0x..., why: \"...\" } ]\n"
        "    schema:  ../schema                         # every class with a YAML descriptor there is in scope\n"
        "    include: [\"hkb*\"]                          # RTTI class-name globs in scope\n"
        "    exclude: [\"hkp*\"]\n"
        "\n"
        "Statuses: EXACT (identical with relocated fields masked), REGALLOC (identical but for register choice),\n"
        "VARIANT (the same calls and memory writes in the same order at the same offsets and the same tested conditions;\n"
        "register choice, branch layout and side-effect-free instructions differ - what a different build of the same\n"
        "compiler produces; it does NOT check register dataflow, so a dropped or changed register computation such as a\n"
        "loop counter step passes), STRUCTURAL, MISSING (symbol or address not found), COMPILE-ERROR.\n"
        "With a registry, a relocation that points somewhere other than its engine.yaml address makes the function\n"
        "STRUCTURAL whatever its bytes say. A switch's jump tables (after the code) are compared apart from the instructions:\n"
        "byte index tables identical, every case target on corresponding code. Exit code 0 only when every function is EXACT.");
}

struct Options {
    fs::path    manifest, exe, work, report;
    std::string runtime, only, todo;
    bool        show = false, showAll = false, changesOnly = false, allowRegalloc = false, allowVariant = false, suggest = false;
    bool        inventory = false, coverageAll = false;
    unsigned    jobs = 0;
    std::size_t diffSeeds = 0;
    std::optional<std::uint64_t> diffSeed;   ///< --diff-seed: just this one, traced
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
    if (opt.inventory) {
        RuntimeSpec spec = rt ? *rt : RuntimeSpec{ runtime, exePath, {}, runtime };
        spec.exe         = exePath;
        return buildInventory(m, spec);
    }
    if (!opt.todo.empty()) return printTodo(m, runtime, opt.todo);
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

    struct Row { std::string name; std::uint64_t va = 0; Match best; std::vector<std::pair<std::string, Match>> all; std::optional<DiffReport> diff; };
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
                            // A switch's jump tables follow the code: the instructions are compared without them, then
                            // the tables against each other.
                            const CodeLayout oursLay    = oursLayout(ours);
                            const CodeLayout vanillaLay = vanillaLayout(vanilla, row.va, pe.imageBase);
                            if (oursLay.tables.empty() && vanillaLay.tables.empty()) {
                                mt = compare(ours, vanilla, row.va, opt.show || opt.showAll, syms ? &*syms : nullptr, pe);
                            } else {
                                const CoffFunction code = codeOnly(ours, oursLay.codeEnd);
                                const Bytes        vcode(vanilla.begin(), vanilla.begin() + vanillaLay.codeEnd);
                                mt = compare(code, vcode, row.va, opt.show || opt.showAll, syms ? &*syms : nullptr, pe);
                                checkTables(mt, code, vcode, oursLay, vanillaLay);
                                mt.oursLen    = ours.bytes.size();
                                mt.vanillaLen = vanilla.size();
                            }
                            mt.toolchain = tc;
                            mt.ours      = &ours;
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
            rows.push_back(std::move(row));
        }
    }

    // A VARIANT that rests on rule A or E (local stack slots, SIMD schedule) is only as good as its dataflow: --diff runs
    // on it whether asked for or not, and a divergence, or a run that cannot confirm anything, makes it STRUCTURAL.
    std::unique_ptr<Emulator> emu;
    auto emulator = [&]() -> Emulator& {
        if (!emu) {
            emu = std::make_unique<Emulator>(pe);
            emu->trace = opt.diffSeed.has_value();
        }
        return *emu;
    };
    const std::size_t gateSeeds = std::max<std::size_t>({ opt.diffSeeds, opt.diffSeed.value_or(0), 256 });
    for (auto& r : rows) {
        if (r.best.status != Status::Variant || !r.best.needsDiff || !r.best.ours) continue;
        std::string why;
        if (!syms) {
            why = "needs --diff, which needs the registry and an Address Library";
        } else if (const auto off = pe.fileOffset(static_cast<std::uint32_t>(r.va - pe.imageBase), r.best.vanillaLen); !off) {
            why = "needs --diff, but vanilla's bytes are not in the file image";
        } else {
            const Bytes vanilla(pe.bytes.begin() + *off, pe.bytes.begin() + *off + r.best.vanillaLen);
            r.diff = diffFunction(emulator(), *r.best.ours, vanilla, r.va, *syms, pe, gateSeeds, opt.diffSeed);
            if (!r.diff->error.empty()) why = "needs --diff, which could not run: " + r.diff->error;
            else if (r.diff->diverged) why = "--diff diverges: " + r.diff->firstDivergence;
            else if (r.diff->agreed == 0) why = "--diff confirmed no seed (all faulted, inconclusive or NaN)";
        }
        if (!why.empty()) {
            r.best.status = Status::Structural;
            r.best.detail = "--diff-gated rule (A, E, I, J) unconfirmed: " + why + "\n" + r.best.detail;
        }
    }
    if (opt.diffSeeds || opt.diffSeed) {
        if (!syms) throw std::runtime_error("--diff needs the registry and the runtime's Address Library to place references");
        for (auto& r : rows) {
            if ((r.best.status != Status::Regalloc && r.best.status != Status::Variant) || !r.best.ours || r.diff) continue;
            const auto off = pe.fileOffset(static_cast<std::uint32_t>(r.va - pe.imageBase), r.best.vanillaLen);
            if (!off) continue;
            const Bytes vanilla(pe.bytes.begin() + *off, pe.bytes.begin() + *off + r.best.vanillaLen);
            r.diff = diffFunction(emulator(), *r.best.ours, vanilla, r.va, *syms, pe, std::max<std::size_t>(opt.diffSeeds, opt.diffSeed.value_or(0)), opt.diffSeed);
        }
    }
    for (const auto& r : rows) {
        const bool pass = r.best.status == Status::Exact ||
                          ((opt.allowRegalloc || opt.allowVariant) && r.best.status == Status::Regalloc) ||
                          (opt.allowVariant && r.best.status == Status::Variant);
        gatePassed = gatePassed && pass && !(r.diff && r.diff->diverged);
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
        if (r.diff) {
            const auto& d = *r.diff;
            if (!d.error.empty()) {
                std::printf("    diff: not run: %s\n", d.error.c_str());
            } else {
                std::printf("    diff: %zu seeds (%zu runs): %zu agree, %zu fault alike, %zu inconclusive, %zu NaN, %zu DIVERGE; vanilla coverage %zu/%zu "
                            "instructions, %zu/%zu branch sides; %.0f ms\n",
                            d.seeds, d.runs, d.agreed, d.faultedAlike, d.inconclusive, d.nan, d.diverged, d.insnsCovered, d.insnsTotal, d.sidesCovered,
                            d.sidesTotal, d.ms);
                if (d.diverged) std::printf("    diff: first divergence, %s\n", d.firstDivergence.c_str());
            }
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

    // Coverage: this run's statuses over the inventory (a partial run would undercount, so only after a full one).
    if (!m.inventory.file.empty() && fs::exists(m.inventory.file) && opt.only.empty()) {
        const Inventory               inv = loadInventory(m.inventory);
        std::map<std::uint64_t, Status> statusOf;
        for (const auto& r : rows) statusOf[r.va] = r.best.status;
        std::set<std::uint64_t> inInventory;
        std::size_t total = 0, matched = 0, parked = 0;
        struct ClassRow { std::string name; std::size_t total = 0, exact = 0, regalloc = 0, variant = 0, other = 0, parked = 0; };
        std::vector<ClassRow> classRows;
        for (const auto& [name, list] : inv.classes) {
            ClassRow c;
            c.name  = name;
            c.total = list.size();
            for (const auto& f : list) {
                inInventory.insert(f.va);
                const auto s = statusOf.find(f.va);
                if (s != statusOf.end()) {
                    if (s->second == Status::Exact) ++c.exact;
                    else if (s->second == Status::Regalloc) ++c.regalloc;
                    else if (s->second == Status::Variant) ++c.variant;
                    else ++c.other;
                } else if (inv.parked.count(f.va)) {
                    ++c.parked;
                }
            }
            total += c.total;
            matched += c.exact + c.regalloc + c.variant;
            parked += c.parked;
            classRows.push_back(c);
        }
        std::size_t outside = 0;
        for (const auto& r : rows) outside += inInventory.count(r.va) ? 0 : 1;
        std::printf("\ncoverage: %zu of %zu inventory functions matched (%.1f%%) across %zu classes; %zu parked; %zu manifest "
                    "functions outside the inventory\n",
                    matched, total, total ? 100.0 * double(matched) / double(total) : 0.0, inv.classes.size(), parked, outside);
        std::printf("  %-44s %9s  %5s %8s %7s %5s %6s\n", "class", "matched", "EXACT", "REGALLOC", "VARIANT", "other", "parked");
        for (const auto& c : classRows) {
            const std::size_t done = c.exact + c.regalloc + c.variant;
            if (!opt.coverageAll && done + c.other + c.parked == 0) continue;
            std::printf("  %-44s %4zu/%-4zu  %5zu %8zu %7zu %5zu %6zu\n", c.name.c_str(), done, c.total, c.exact, c.regalloc, c.variant,
                        c.other, c.parked);
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
        else if (a == "--inventory") opt.inventory = true;
        else if (a == "--todo") opt.todo = next();
        else if (a == "--coverage") opt.coverageAll = true;
        else if (a == "--diff") opt.diffSeeds = std::stoul(next());
        else if (a == "--diff-seed") opt.diffSeed = std::stoull(next());
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
