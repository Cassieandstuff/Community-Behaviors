#pragma once

#include <string>

namespace CB::config::tests {

/// @brief Count a failure (and print `what`) when `ok` is false.
void Check(bool ok, const std::string& what);

void RunCompatTests();

}  // namespace CB::config::tests
