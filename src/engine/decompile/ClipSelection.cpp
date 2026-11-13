#include <decompile/BehaviorDecompile.h>

#include <RymlInclude.h>

#include <stdexcept>
#include <string>

namespace CB::engine::decompile {

    namespace {

        bool HasKey(const c4::yml::ConstNodeRef& n, const char* k)
        {
            return n.readable() && n.is_map() && n.has_child(c4::to_csubstr(k));
        }

        std::shared_ptr<io::SchemaObject> Parse(std::string text, const char* key, const schema::SchemaRegistry& reg,
                                                const std::string& where, std::string& err, bool unit)
        {
            common::EnsureRymlThrows();
            try {
                c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(text));
                c4::yml::ConstNodeRef at = tree.crootref();
                if (unit) {
                    if (HasKey(at, "animation")) at = at["animation"];
                    if (!HasKey(at, key)) return nullptr;
                    at = at[c4::to_csubstr(key)];
                }
                return ParseEmbeddedObject(at, "CBClipSelection", reg, where, err);
            } catch (const std::exception& e) {
                err = where + ": " + e.what();
                return nullptr;
            }
        }

    }  // namespace

    std::shared_ptr<io::SchemaObject> ParseClipSelection(std::string_view mapYaml, const schema::SchemaRegistry& reg,
                                                         const std::string& where, std::string& err)
    {
        return Parse(std::string(mapYaml), nullptr, reg, where, err, false);
    }

    std::shared_ptr<io::SchemaObject> ClipSelectionOfAnimation(std::string_view unitText, const schema::SchemaRegistry& reg,
                                                               const std::string& where, std::string& err)
    {
        err.clear();
        return Parse(std::string(unitText), "selection", reg, where, err, true);
    }

}  // namespace CB::engine::decompile
