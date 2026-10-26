#pragma once
#include <codec/serialization/HkyArchive.h>
#include <interface/reflection/HavokSchema.h>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief Bundle assembly: partitions, loose files, generators and inject configs packed into one .hky — what
///        `hky-tool assemble` builds CB's feature bundles with and what an editor's Export lowers a project into.
/// @note  Every failure is a hard error, never a skip: a config naming a missing path, two configs for one file's field,
///        a field the file's kind does not accept, a value of the wrong shape, two sources supplying one path.

namespace CB::engine::compile {

/// @brief What a bundle is assembled from.
struct BundleInput {
    /// @brief Packed YAML trees laid out like a Data folder, by name ("Animations.hky"): every file ships at its path,
    ///        and a generator's `from` names one.
    std::map<std::string, std::shared_ptr<codec::HkyArchive>> partitions;
    /// @brief Files shipped at their path (manifest.json, units, animations).
    std::vector<codec::HkyArchive::PackEntry> loose;
    /// @brief (config name, JSON text): `{"generator": <kind>, ...}` configs, run in order before injection, so an inject
    ///        config may target a generated file.
    std::vector<std::pair<std::string, std::string>> generators;
    /// @brief (config name, JSON text): `{"targets": [...]}` configs writing fields into animation files.
    std::vector<std::pair<std::string, std::string>> injections;
};

struct AssembleReport {
    std::size_t              files = 0, loose = 0, generated = 0, measured = 0, injected = 0, touched = 0;
    std::vector<std::string> errors;   ///< why nothing was packed
};

/// @brief Assemble `in` into `outHky`.
/// @param injectablesJson the injectables table ({kind: {field: type}}; schema/metadata/injectables.json), naming the
///        fields each file kind accepts
/// @param reg the class schema the generators build through; null = none (a generator that needs it fails)
/// @return false with `report.errors` set when anything fails; nothing is packed then
bool AssembleBundle(const BundleInput& in, std::string_view injectablesJson, const schema::SchemaRegistry* reg,
                    const std::string& outHky, AssembleReport& report);

}  // namespace CB::engine::compile
