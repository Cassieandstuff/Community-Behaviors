#pragma once

#include <interface/reflection/HavokSchema.h>

#include <string>

namespace CB::engine::tests {

/// @brief Count a failure (and print `what`) when `ok` is false.
void Check(bool ok, const std::string& what);

void RunUserSentinelTests(const schema::SchemaRegistry& reg);
void RunRulesTests(const schema::SchemaRegistry& reg);

}  // namespace CB::engine::tests
