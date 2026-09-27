#pragma once
// Detail.h — internal registry accessors shared by Expression.cpp / EvalContext.cpp.
// Not part of the public API (Conditions.h). Namespace CB::conditions::detail.
#include "Conditions.h"
#include <span>

namespace CB::conditions::detail {
    PrimitiveFn            PrimFn(PrimId);
    VType                  PrimReturns(PrimId);
    FunctionFn             FnFn(FnId);
    VType                  FnReturns(FnId);
    std::span<const VType> FnParams(FnId);
}
