#include "PCH.h"

#include "Startup.h"

#include <vector>

namespace CB::platform::startup {

    namespace {

        struct Callback {
            void (*fn)();
            const char* who;
        };

        /// @note A function-local static: features register from their own static initializers, in any order.
        std::vector<Callback>& DataLoaded()
        {
            static std::vector<Callback> callbacks;
            return callbacks;
        }

        bool g_ran = false;

    }  // namespace

    bool OnDataLoaded(void (*fn)(), const char* who)
    {
        if (g_ran) {
            LOG_ERROR("startup: '{}' registered for kDataLoaded after it ran - ignored", who);
            return false;
        }
        DataLoaded().push_back({ fn, who });
        return true;
    }

    void RunDataLoaded()
    {
        g_ran = true;
        for (const auto& [fn, who] : DataLoaded()) {
            LOG_INFO("startup: kDataLoaded - {}", who);
            fn();
        }
    }

}  // namespace CB::platform::startup
