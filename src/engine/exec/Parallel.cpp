#include "Parallel.h"

#include <cstdlib>

namespace CB::engine::exec {
namespace {

std::atomic<unsigned> g_set{ 0 };

unsigned FromEnvironment()
{
    char*       text = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&text, &size, "CB_MAX_THREADS") != 0 || !text) return 0;
    const long n = std::strtol(text, nullptr, 10);
    std::free(text);
    return n > 0 ? static_cast<unsigned>(n) : 0;
}

}  // namespace

unsigned MaxWorkers()
{
    if (const unsigned set = g_set.load(std::memory_order_relaxed)) return set;
    static const unsigned kEnvironment = FromEnvironment();
    if (kEnvironment) return kEnvironment;
    return (std::max)(1u, std::thread::hardware_concurrency() / 2);
}

void SetMaxWorkers(unsigned n) { g_set.store(n, std::memory_order_relaxed); }

}  // namespace CB::engine::exec
