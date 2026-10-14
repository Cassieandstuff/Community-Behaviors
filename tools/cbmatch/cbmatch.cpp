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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
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

struct Manifest {
    fs::path                                     dir;
    std::vector<std::pair<std::string, fs::path>> runtimes;     ///< in file order; the first is the default
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
    m.dir        = fs::absolute(path).parent_path();
    m.runtimes   = namedPaths(root, "runtimes", m.dir);
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
                fn.va[m.runtimes.front().first] = parseAddress(val(va));
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

    Bytes                bytes;
    std::uint64_t        imageBase = 0;
    std::vector<Section> sections;
    std::uint32_t        pdataRva = 0, pdataSize = 0;

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
    return pe;
}

// ── compiled objects ─────────────────────────────────────────────────────────────────────────────────────────────

struct CoffFunction {
    std::string                            mangled, name;
    Bytes                                  bytes;
    std::vector<bool>                      mask;     ///< bytes a relocation fills in at link time
    std::map<std::uint32_t, std::string>   targets;  ///< reloc offset → the symbol it refers to
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
                if (sym < names.size()) f.targets[at - begin] = undecorate(names[sym]);
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

struct Match {
    Status      status = Status::Missing;
    std::string toolchain;
    double      pct = 0;
    std::size_t oursLen = 0, vanillaLen = 0;
    long long   firstDiff = -1;
    std::string detail;
    std::vector<std::string> listing;
};

std::string targetNote(const CoffFunction& f, const Insn& i)
{
    for (std::uint32_t o = i.off; o < i.off + i.ins.length; ++o)
        if (auto it = f.targets.find(o); it != f.targets.end()) return "  -> " + it->second;
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

/// @brief Pairs of (ours, vanilla) instruction indices; -1 on one side marks an instruction the other side lacks.
/// @note LCS over instruction shapes, so one inserted instruction doesn't misalign the rest of the listing.
///       Falls back to index pairing when the table would be too large.
std::vector<std::pair<long, long>> alignInstructions(const std::vector<Insn>& a, const std::vector<Insn>& b)
{
    std::vector<std::pair<long, long>> rows;
    const std::size_t n = a.size(), m = b.size();
    if (n * m > 40'000'000) {
        for (std::size_t k = 0; k < std::max(n, m); ++k) rows.emplace_back(k < n ? long(k) : -1, k < m ? long(k) : -1);
        return rows;
    }
    std::vector<std::uint16_t> lcs((n + 1) * (m + 1), 0);
    auto at = [&](std::size_t i, std::size_t j) -> std::uint16_t& { return lcs[i * (m + 1) + j]; };
    for (std::size_t i = n; i-- > 0;)
        for (std::size_t j = m; j-- > 0;)
            at(i, j) = sameShape(a[i], b[j]) ? std::uint16_t(at(i + 1, j + 1) + 1) : std::max(at(i + 1, j), at(i, j + 1));
    std::size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && sameShape(a[i], b[j]) && at(i, j) == at(i + 1, j + 1) + 1) rows.emplace_back(long(i++), long(j++));
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
    return dead;
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
///          NOT caught: a changed or dropped register computation (e.g. a loop's `i--` removed — verified to pass), a
///          different value loaded into a register, a condition change hidden by a duplicated branch of the same family.
///          Caught (deliberately broken copies, D:\CB-tmp\matching-probe\mut): wrong field read or written, dropped or
///          extra call, dropped or extra store, wrong argument, wrong constant, >= → >. A VARIANT function still needs
///          behavioural verification.
bool variantEquivalent(const CoffFunction& ours, const std::vector<Insn>& a, const std::vector<Insn>& b, std::string& why)
{
    // Unpaired effects are matched as a set within each stretch between two aligned calls: a compiler may reorder
    // independent stores, never across a call.
    struct Effect { const Insn* insn; std::size_t segment; bool used = false; };
    std::vector<Effect>        effectsA, effectsB;
    /// Unpaired condition family → (count in ours, count in vanilla). Counts may differ (tail duplication copies a
    /// branch); a family unpaired on one side only means a tested condition changed.
    std::map<std::string, std::pair<int, int>> conditions;
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
    for (const auto& [ia, ib] : alignInstructions(liveA, liveB)) {
        if (ia >= 0 && ib >= 0) {
            const auto& x = liveA[std::size_t(ia)];
            const auto& y = liveB[std::size_t(ib)];
            const bool  masked = isMasked(ours, x);
            if (x.ins.mnemonic == ZYDIS_MNEMONIC_NOP) { ++aligned; continue; }   // alignment padding: its encoding is filler
            for (std::uint8_t o = 0; o < x.ins.operand_count_visible; ++o) {
                const auto& ox = x.ops[o];
                const auto& oy = y.ops[o];
                // A read-only memory operand may take another address form after strength reduction ([r9+rcx] for
                // [rcx]); its displacement still has to match, which is what pins the field being read.
                if (ox.type == ZYDIS_OPERAND_TYPE_MEMORY && oy.type == ZYDIS_OPERAND_TYPE_MEMORY && ox.mem.type == ZYDIS_MEMOP_TYPE_MEM &&
                    !(ox.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) && !(oy.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) &&
                    ox.size == oy.size && (ox.mem.base == ZYDIS_REGISTER_RIP) == (oy.mem.base == ZYDIS_REGISTER_RIP) &&
                    (masked || ox.mem.disp.value == oy.mem.disp.value))
                    continue;
                const bool relative = ox.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ox.imm.is_relative;
                if (!operandsMatch(ox, oy, masked || relative)) {
                    why = "operands differ beyond registers at ours +0x" + hex(x.off) + " / vanilla +0x" + hex(y.off);
                    return false;
                }
            }
            ++aligned;
            if (x.ins.mnemonic == ZYDIS_MNEMONIC_CALL) ++segment;
        } else if (ia >= 0) {
            const Insn& x = liveA[std::size_t(ia)];
            if (const char* fam = conditionFamily(x.ins.mnemonic)) ++conditions[fam].first;
            if (hasEffect(x) && x.off >= prologueA) effectsA.push_back({ &x, segment }); else ++glue;
        } else {
            const Insn& y = liveB[std::size_t(ib)];
            if (const char* fam = conditionFamily(y.ins.mnemonic)) ++conditions[fam].second;
            if (hasEffect(y) && y.off >= prologueB) effectsB.push_back({ &y, segment }); else ++glue;
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

Match compare(const CoffFunction& ours, const Bytes& vanilla, std::uint64_t va, bool wantListing)
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
    if (variantEquivalent(ours, *a, *b, variantWhy)) {
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
        "\n"
        "manifest:\n"
        "  runtimes:   { ae: ${CB_MATCH_EXE_AE} }        # name -> exe; ${VAR} reads the environment\n"
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
        "Exit code 0 only when every function is EXACT.");
}

struct Options {
    fs::path    manifest, exe, work, report;
    std::string runtime, only;
    bool        show = false, showAll = false, changesOnly = false, allowRegalloc = false, allowVariant = false;
    unsigned    jobs = 0;
};

int run(const Options& opt)
{
    const Manifest m = loadManifest(opt.manifest);
    if (m.runtimes.empty() && opt.exe.empty()) throw std::runtime_error("no runtime: add runtimes: to the manifest or pass --exe");

    std::string runtime = opt.runtime.empty() ? (m.runtimes.empty() ? std::string("default") : m.runtimes.front().first) : opt.runtime;
    fs::path    exePath = opt.exe;
    if (exePath.empty()) {
        const auto it = std::find_if(m.runtimes.begin(), m.runtimes.end(), [&](const auto& r) { return r.first == runtime; });
        if (it == m.runtimes.end()) throw std::runtime_error("manifest has no runtime '" + runtime + "'");
        exePath = it->second;
    }
    const PeImage pe = loadPe(exePath);

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
                            mt = compare(ours, vanilla, row.va, opt.show || opt.showAll);
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
        if ((opt.showAll || (opt.show && b.status != Status::Exact)) && !b.listing.empty()) {
            std::printf("    %-63s | vanilla 0x%s\n", ("ours (" + b.toolchain + ")").c_str(), hex(r.va).c_str());
            for (const auto& line : b.listing)
                if (!opt.changesOnly || line[0] != ' ') std::printf("    %s\n", line.c_str());
            std::printf("\n");
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
