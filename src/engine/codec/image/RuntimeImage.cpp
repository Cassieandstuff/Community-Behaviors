#include "codec/image/RuntimeImage.h"

#include <cstring>

namespace CB::engine::codec::image {

    namespace {

        constexpr char          kMagic[4] = { 'C', 'B', 'R', 'I' };
        constexpr std::uint32_t kFormat   = 1;

        struct Writer {
            std::vector<std::uint8_t> out;
            template <class T>
            void pod(T v)
            {
                const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
                out.insert(out.end(), p, p + sizeof v);
            }
            void str(std::string_view s)
            {
                pod(static_cast<std::uint32_t>(s.size()));
                out.insert(out.end(), s.begin(), s.end());
            }
            void bytes(const std::vector<std::uint8_t>& b)
            {
                pod(static_cast<std::uint32_t>(b.size()));
                out.insert(out.end(), b.begin(), b.end());
            }
        };

        struct Reader {
            const std::vector<std::uint8_t>& in;
            std::size_t                      pos = 0;
            bool                             ok  = true;
            template <class T>
            T pod()
            {
                T v{};
                if (!ok || in.size() - pos < sizeof v) { ok = false; return v; }
                std::memcpy(&v, in.data() + pos, sizeof v);
                pos += sizeof v;
                return v;
            }
            std::string str()
            {
                const auto n = pod<std::uint32_t>();
                if (!ok || in.size() - pos < n) { ok = false; return {}; }
                std::string s(reinterpret_cast<const char*>(in.data() + pos), n);
                pos += n;
                return s;
            }
            std::vector<std::uint8_t> bytes()
            {
                const auto n = pod<std::uint32_t>();
                if (!ok || in.size() - pos < n) { ok = false; return {}; }
                std::vector<std::uint8_t> b(in.begin() + std::ptrdiff_t(pos), in.begin() + std::ptrdiff_t(pos + n));
                pos += n;
                return b;
            }
        };

    }  // namespace

    std::uint64_t Fnv1a64(const std::uint8_t* data, std::size_t size) noexcept
    {
        std::uint64_t h = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 0x100000001b3ull;
        return h;
    }

    std::string Relocate(const Function& f, std::uint8_t* out, std::uint64_t at, std::uint64_t exeBase)
    {
        std::memcpy(out, f.code.data(), f.code.size());
        for (const auto& r : f.refs) {
            const std::uint64_t target = r.own ? at + r.target : exeBase + r.target;
            if (r.kind == RefKind::Addr64) {
                const std::uint64_t v = target + static_cast<std::uint64_t>(r.addend);
                std::memcpy(out + r.at, &v, 8);
                continue;
            }
            const std::int64_t value = static_cast<std::int64_t>(target) + r.addend - static_cast<std::int64_t>(at + r.at + 4 + r.delta);
            if (value < INT32_MIN || value > INT32_MAX) return "a reference at +" + std::to_string(r.at) + " is out of rel32 reach";
            const auto v32 = static_cast<std::int32_t>(value);
            std::memcpy(out + r.at, &v32, 4);
        }
        return {};
    }

    std::vector<std::uint8_t> Encode(const Image& image)
    {
        Writer w;
        w.out.insert(w.out.end(), std::begin(kMagic), std::end(kMagic));
        w.pod(kFormat);
        w.str(image.exeVersion);
        w.pod(static_cast<std::uint32_t>(image.classes.size()));
        for (const auto& c : image.classes) w.str(c);
        w.pod(static_cast<std::uint32_t>(image.functions.size()));
        for (const auto& f : image.functions) {
            w.pod(f.classIndex);
            w.str(f.name);
            w.pod(f.vanillaRva);
            w.pod(f.vanillaLength);
            w.pod(f.vanillaHash);
            w.pod(f.status);
            w.bytes(f.code);
            w.pod(static_cast<std::uint32_t>(f.refs.size()));
            for (const auto& r : f.refs) {
                w.pod(r.at);
                w.pod(static_cast<std::uint8_t>(r.kind));
                w.pod(r.delta);
                w.pod(static_cast<std::uint8_t>(r.own ? 1 : 0));
                w.pod(r.target);
                w.pod(r.addend);
            }
            w.pod(static_cast<std::uint32_t>(f.unwinds.size()));
            for (const auto& u : f.unwinds) {
                w.pod(u.begin);
                w.pod(u.end);
                w.pod(u.info);
            }
            w.bytes(f.unwindData);
            w.pod(static_cast<std::uint32_t>(f.chains.size()));
            for (const auto& c : f.chains) {
                w.pod(c.at);
                w.pod(c.parent);
            }
        }
        return std::move(w.out);
    }

    std::optional<Image> Decode(const std::vector<std::uint8_t>& bytes, std::string* error)
    {
        auto fail = [&](const char* why) -> std::optional<Image> {
            if (error) *error = why;
            return std::nullopt;
        };
        if (bytes.size() < 8 || std::memcmp(bytes.data(), kMagic, 4) != 0) return fail("not a runtime image");
        Reader r{ bytes, 4 };
        if (r.pod<std::uint32_t>() != kFormat) return fail("unknown runtime image format");
        Image img;
        img.exeVersion = r.str();
        const auto nClasses = r.pod<std::uint32_t>();
        for (std::uint32_t c = 0; c < nClasses && r.ok; ++c) img.classes.push_back(r.str());
        const auto nFunctions = r.pod<std::uint32_t>();
        for (std::uint32_t k = 0; k < nFunctions && r.ok; ++k) {
            Function f;
            f.classIndex    = r.pod<std::uint32_t>();
            f.name          = r.str();
            f.vanillaRva    = r.pod<std::uint32_t>();
            f.vanillaLength = r.pod<std::uint32_t>();
            f.vanillaHash   = r.pod<std::uint64_t>();
            f.status        = r.pod<std::uint8_t>();
            f.code          = r.bytes();
            const auto nRefs = r.pod<std::uint32_t>();
            for (std::uint32_t q = 0; q < nRefs && r.ok; ++q) {
                Ref ref;
                ref.at     = r.pod<std::uint32_t>();
                ref.kind   = static_cast<RefKind>(r.pod<std::uint8_t>());
                ref.delta  = r.pod<std::uint8_t>();
                ref.own    = r.pod<std::uint8_t>() != 0;
                ref.target = r.pod<std::uint32_t>();
                ref.addend = r.pod<std::int64_t>();
                if (ref.kind != RefKind::Rel32 && ref.kind != RefKind::Addr64) return fail("unknown reference kind");
                const std::size_t width = ref.kind == RefKind::Rel32 ? 4 : 8;
                if (std::size_t(ref.at) + width > f.code.size()) return fail("a reference lies outside its function");
                f.refs.push_back(ref);
            }
            const auto nUnwinds = r.pod<std::uint32_t>();
            for (std::uint32_t q = 0; q < nUnwinds && r.ok; ++q) {
                Function::Unwind u;
                u.begin = r.pod<std::uint32_t>();
                u.end   = r.pod<std::uint32_t>();
                u.info  = r.pod<std::uint32_t>();
                f.unwinds.push_back(u);
            }
            f.unwindData        = r.bytes();
            const auto nChains  = r.pod<std::uint32_t>();
            for (std::uint32_t q = 0; q < nChains && r.ok; ++q) {
                Function::Chain c;
                c.at     = r.pod<std::uint32_t>();
                c.parent = r.pod<std::uint32_t>();
                f.chains.push_back(c);
            }
            if (!r.ok) break;
            for (const auto& u : f.unwinds)
                if (u.begin >= u.end || u.end > f.code.size() || std::size_t(u.info) + 4 > f.unwindData.size()) return fail("an unwind entry lies outside its function");
            for (const auto& c : f.chains)
                if (std::size_t(c.at) + 12 > f.unwindData.size() || c.parent >= f.unwinds.size()) return fail("a chained unwind entry is malformed");
            if (f.classIndex >= img.classes.size()) return fail("a function names no class");
            img.functions.push_back(std::move(f));
        }
        if (!r.ok) return fail("truncated runtime image");
        return img;
    }

}  // namespace CB::engine::codec::image
