#pragma once

#include <PCH.h>

#include <core/hooks/takeover/Takeover.h>

#include <codec/image/RuntimeImage.h>

#include <deque>
#include <filesystem>
#include <memory>

namespace CB::core::hooks {

    /// @brief The code source `image`: the runtime image tools/cbmatch emitted, relocated into one executable block
    ///        within rel32 reach of the exe (its references and the entry jumps both need it).
    class ImageSource final : public ICodeSource {
    public:
        /// @return the source, or nullptr (logged) when the file is absent, unreadable, or matched against another
        ///         exe build than the running one.
        [[nodiscard]] static std::unique_ptr<ImageSource> Load(const std::filesystem::path& file);

        [[nodiscard]] const char*              Name() const noexcept override { return "image"; }
        [[nodiscard]] std::vector<std::string> Classes() const override { return image_.classes; }
        [[nodiscard]] std::vector<Replacement> Provide(std::string_view cls) override;

    private:
        explicit ImageSource(CB::engine::codec::image::Image image) : image_(std::move(image)) {}

        /// @return `size` bytes of the arena at `align`, or nullptr when it is full.
        [[nodiscard]] std::byte* Take(std::size_t size, std::size_t align);

        CB::engine::codec::image::Image image_;
        /// @brief The arena's start, just past the exe's image: the unwind tables' image base.
        /// @note Not an SKSE::Trampoline: that only promises ±2 GB of the end of .text, and a block near that window's
        ///       edge leaves the exe's later sections (.data, TLS slots) out of rel32 reach — 856 of 1307 functions on the
        ///       first launch.
        std::byte*                      base_ = nullptr;
        std::byte*                      next_ = nullptr;
        std::size_t                     left_ = 0;
        /// @brief One registered RUNTIME_FUNCTION table per provided class.
        /// @invariant Never shrinks or moves an inner vector once registered: RtlAddFunctionTable keeps the pointer.
        std::deque<std::vector<RUNTIME_FUNCTION>> tables_;
    };

}  // namespace CB::core::hooks
