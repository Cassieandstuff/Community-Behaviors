#include "core/resolve/LoadOrderReader.h"

#include <mutex>
#include <vector>

namespace CB::core::resolve {

    namespace {

        /// @note Function-local, so a feature subscribing from its own static initializer finds it constructed.
        struct Seam {
            std::mutex                                               lock;
            std::optional<LoadOrderReader>                           reader;
            std::vector<std::function<void(const LoadOrderReader&)>> waiting;
        };
        Seam& S()
        {
            static Seam s;
            return s;
        }

    }  // namespace

    void ArmLoadOrderReader(LoadOrderReader reader)
    {
        std::vector<std::function<void(const LoadOrderReader&)>> run;
        {
            std::scoped_lock lock(S().lock);
            S().reader = std::move(reader);
            run.swap(S().waiting);
        }
        for (auto& fn : run) fn(*S().reader);
    }

    void OnLoadOrderReady(std::function<void(const LoadOrderReader&)> fn)
    {
        {
            std::scoped_lock lock(S().lock);
            if (!S().reader) {
                S().waiting.push_back(std::move(fn));
                return;
            }
        }
        fn(*S().reader);
    }

}  // namespace CB::core::resolve
