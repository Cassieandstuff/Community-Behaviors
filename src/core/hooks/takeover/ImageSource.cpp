#include "PCH.h"

#include "core/hooks/takeover/ImageSource.h"

#include <PluginLogger.h>

#include <disk/Disk.h>

#include <cstring>

namespace CB::core::hooks {

    namespace img = CB::engine::codec::image;

    namespace {

        std::size_t Footprint(const img::Function& f) { return ((f.code.size() + 15) & ~std::size_t(15)) + f.unwindData.size() + 4; }

    }  // namespace

    std::unique_ptr<ImageSource> ImageSource::Load(const std::filesystem::path& file)
    {
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) {
            LOG_INFO("havok runtime: no image at {} — vanilla runs", file.string());
            return nullptr;
        }
        const auto bytes = CB::engine::disk::ReadBytes(file);
        if (!bytes) {
            LOG_ERROR("havok runtime: cannot read {} — vanilla runs", file.string());
            return nullptr;
        }
        std::string why;
        auto        image = img::Decode(*bytes, &why);
        if (!image) {
            LOG_ERROR("havok runtime: {}: {} — vanilla runs", file.string(), why);
            return nullptr;
        }
        const std::string running = REL::Module::get().version().string(".");
        if (image->exeVersion != running) {
            LOG_WARN("havok runtime: the image was matched against Skyrim {}, this is {} — vanilla runs", image->exeVersion, running);
            return nullptr;
        }
        std::size_t total = 16;
        for (const auto& f : image->functions) total += Footprint(f);
        std::unique_ptr<ImageSource> src(new ImageSource(std::move(*image)));
        src->arena_.create(total);
        src->base_ = static_cast<std::byte*>(src->arena_.allocate(total));
        src->next_ = src->base_;
        src->left_ = total;
        LOG_INFO("havok runtime: image of {} functions ({} bytes placed) over {} classes, matched against Skyrim {}",
                 src->image_.functions.size(), total, src->image_.classes.size(), running);
        return src;
    }

    std::byte* ImageSource::Take(std::size_t size, std::size_t align)
    {
        const auto pad = (align - reinterpret_cast<std::uintptr_t>(next_) % align) % align;
        if (left_ < pad + size) return nullptr;
        std::byte* const at = next_ + pad;
        next_               = at + size;
        left_ -= pad + size;
        return at;
    }

    std::vector<Replacement> ImageSource::Provide(std::string_view cls)
    {
        std::vector<Replacement> out;
        const auto               it = std::ranges::find(image_.classes, cls);
        if (it == image_.classes.end()) return out;
        const auto           index = static_cast<std::uint32_t>(it - image_.classes.begin());
        const std::uintptr_t exe   = REL::Module::get().base();
        const auto           rva   = [&](const std::byte* p) { return static_cast<DWORD>(p - base_); };
        auto&                table = tables_.emplace_back();
        for (const auto& f : image_.functions) {
            if (f.classIndex != index) continue;
            std::byte* const place  = Take(f.code.size(), 16);
            std::byte* const unwind = place ? Take(f.unwindData.size(), 4) : nullptr;
            if (!place || (!unwind && !f.unwindData.empty())) {
                LOG_ERROR("havok runtime: {} does not fit the arena — vanilla runs it", f.name);
                continue;
            }
            const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(place);
            if (const auto why = img::Relocate(f, reinterpret_cast<std::uint8_t*>(place), at, exe); !why.empty()) {
                LOG_ERROR("havok runtime: {}: {} — vanilla runs it", f.name, why);
                continue;
            }
            if (!f.unwindData.empty()) std::memcpy(unwind, f.unwindData.data(), f.unwindData.size());
            const auto entryOf = [&](const img::Function::Unwind& u) {
                return RUNTIME_FUNCTION{ rva(place + u.begin), rva(place + u.end), rva(unwind + u.info) };
            };
            for (const auto& c : f.chains) {
                const RUNTIME_FUNCTION parent = entryOf(f.unwinds[c.parent]);
                std::memcpy(unwind + c.at, &parent, sizeof parent);
            }
            for (const auto& u : f.unwinds) table.push_back(entryOf(u));
            out.push_back({ f.name, f.vanillaRva, f.vanillaLength, f.vanillaHash, at });
        }
        // Placed in address order, so the table is sorted as the unwinder's binary search needs.
        if (!table.empty() && !RtlAddFunctionTable(table.data(), static_cast<DWORD>(table.size()), reinterpret_cast<DWORD64>(base_)))
            LOG_ERROR("havok runtime: {}'s unwind table was refused — a stack walk through its code will stop there", cls);
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        return out;
    }

}  // namespace CB::core::hooks
