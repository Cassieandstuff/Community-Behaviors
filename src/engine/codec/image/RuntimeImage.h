#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @brief The runtime image: reimplemented engine functions as compiled code plus the references the loader fills in,
///        keyed by the vanilla function each one replaces.
/// @note tools/cbmatch writes it (--emit-image) from the functions that pass its gate, placing every reference with the
///       same resolution --diff runs them under; the plugin's takeover installer reads it, relocates the code next to
///       the running exe and jumps each vanilla entry to it.
namespace CB::engine::codec::image {

    /// @brief How a reference's field is written.
    enum class RefKind : std::uint8_t {
        Rel32,    ///< int32 = target + addend - (field + 4 + delta)
        Addr64,   ///< uint64 = target + addend
    };

    struct Ref {
        std::uint32_t at = 0;       ///< the field's offset in the function's code
        RefKind       kind = RefKind::Rel32;
        std::uint8_t  delta = 0;    ///< Rel32: bytes of the instruction after the field (IMAGE_REL_AMD64_REL32_<delta>)
        bool          own = false;  ///< target is an offset into this function's own code (else an exe RVA)
        std::uint32_t target = 0;
        std::int64_t  addend = 0;
    };

    struct Function {
        std::uint32_t    classIndex = 0;   ///< into Image::classes
        std::string      name;             ///< undecorated, for logs
        std::uint32_t    vanillaRva = 0;
        std::uint32_t    vanillaLength = 0;
        /// @brief FNV-1a 64 over vanilla's bytes as the exe file holds them.
        /// @note A loaded function whose bytes hash differently was patched by someone (a hook) or is another build:
        ///       the installer leaves it alone.
        std::uint64_t    vanillaHash = 0;
        std::uint8_t     status = 0;       ///< cbmatch's verdict: 0 EXACT, 1 REGALLOC, 2 VARIANT
        std::vector<std::uint8_t> code;    ///< reference fields hold zero; the references say what goes there
        std::vector<Ref> refs;
        /// @brief The function's RUNTIME_FUNCTIONs, offsets into `code`; `info` is an offset into `unwindData`.
        /// @note The loader registers them (RtlAddFunctionTable) so exceptions and stack walks unwind through the code.
        struct Unwind { std::uint32_t begin = 0, end = 0, info = 0; };
        std::vector<Unwind>       unwinds;
        std::vector<std::uint8_t> unwindData;   ///< UNWIND_INFO records, 4-aligned, without exception handlers
        /// @brief A chained UNWIND_INFO's parent RUNTIME_FUNCTION, zero in unwindData until the loader writes it.
        struct Chain { std::uint32_t at = 0, parent = 0; };   ///< offset in unwindData; index into `unwinds`
        std::vector<Chain>        chains;
    };

    struct Image {
        /// @brief The exe build the image was matched against, as its version resource spells it ("1.6.1170.0").
        std::string              exeVersion;
        std::vector<std::string> classes;
        std::vector<Function>    functions;
    };

    [[nodiscard]] std::uint64_t Fnv1a64(const std::uint8_t* data, std::size_t size) noexcept;

    /// @brief Writes `f`'s code to `out` with every reference filled in for code that runs at `at` beside an exe loaded at
    ///        `exeBase`. Returns why not (a reference out of rel32 reach), or empty.
    /// @pre `out` holds f.code.size() bytes.
    /// @note The one placement both the loader and tools/cbmatch's image check use.
    [[nodiscard]] std::string Relocate(const Function& f, std::uint8_t* out, std::uint64_t at, std::uint64_t exeBase);

    [[nodiscard]] std::vector<std::uint8_t> Encode(const Image& image);

    /// @return the image, or nullopt when `bytes` is not one (wrong magic, an unknown format, truncated); `error` says
    ///         which.
    [[nodiscard]] std::optional<Image> Decode(const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);

}  // namespace CB::engine::codec::image
