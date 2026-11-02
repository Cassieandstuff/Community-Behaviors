#pragma once

#include <platform/hooks/takeover/Takeover.h>

#include <map>

namespace CB::platform::hooks {

    /// @brief The code source `dll`: the CB SDK functions compiled into this plugin. A registry function row is
    ///        provided when the link resolved its decorated name to the SDK's definition rather than the shim's stub.
    /// @note A function's class is its registry name up to the first '_' (hkbClipGenerator_Activate → hkbClipGenerator).
    /// @note Vanilla's length comes from the exe's unwind table: a function without one (a leaf) is left to vanilla.
    class DllSource final : public ICodeSource {
    public:
        DllSource();

        [[nodiscard]] const char*              Name() const noexcept override { return "dll"; }
        [[nodiscard]] std::vector<std::string> Classes() const override;
        [[nodiscard]] std::size_t              Count() const noexcept override { return count_; }
        [[nodiscard]] std::vector<Replacement> Provide(std::string_view cls) override;

    private:
        /// @brief Class → shim slots whose definition the plugin compiled.
        std::map<std::string, std::vector<std::size_t>, std::less<>> slots_;
        std::size_t                                                  count_ = 0;
    };

}  // namespace CB::platform::hooks
