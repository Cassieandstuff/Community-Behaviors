#pragma once
// Detail.h — internal registry accessors shared by Expression.cpp / EvalContext.cpp.
// Not part of the public API (Relay.h). Namespace CB::platform::relay::detail.
#include "Relay.h"
#include <span>

namespace CB::platform::relay::detail {
    PrimitiveFn            PrimFn(PrimId);
    FunctionFn             FnFn(FnId);
    std::span<const VType> FnParams(FnId);
}
