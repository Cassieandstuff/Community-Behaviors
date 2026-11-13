#include <compile/DynamicClips.h>

#include <algorithm>
#include <filesystem>

namespace CB::engine::compile {

    namespace {

        constexpr const char* kGenerator = "CBDynamicClipGenerator";

        /// @brief The generator's clip settings, given to every clip it plays.
        constexpr const char* kFloats[] = { "cropStartAmountLocalTime", "cropEndAmountLocalTime", "startTime",
                                            "playbackSpeed", "enforcedDuration", "userControlledTimeFraction" };

        std::shared_ptr<io::SchemaObject> MakeClip(decompile::GraphEdit& graph, io::SchemaObject& gen, const std::string& animationName,
                                                   const std::string& name)
        {
            auto clip = graph.Make("hkbClipGenerator");
            if (!clip) return nullptr;
            clip->Str("name")          = name;
            clip->Str("animationName") = animationName;
            clip->FieldRef("triggers").obj = gen.FieldRef("triggers").obj;
            for (const char* f : kFloats) clip->SetFloat(f, gen.GetFloat(f));
            clip->SetInt("mode", gen.GetInt("mode"));
            clip->SetInt("flags", gen.GetInt("flags"));
            clip->SetInt("animationBindingIndex", -1);
            return clip;
        }

        /// @return the clip's file name without its extension, for the clip generator's node name.
        std::string Stem(const std::string& animationName)
        {
            const std::size_t slash = animationName.find_last_of("\\/");
            std::string       file  = slash == std::string::npos ? animationName : animationName.substr(slash + 1);
            if (const std::size_t dot = file.rfind('.'); dot != std::string::npos) file.resize(dot);
            return file;
        }

    }  // namespace

    std::string ProjectRootOf(std::string_view unitKey)
    {
        const std::filesystem::path p{ std::string(unitKey) };
        const std::filesystem::path root = p.parent_path().parent_path();
        if (root.empty()) return {};
        std::string out = root.generic_string();
        return out + "/";
    }

    DynamicClipReport FillDynamicClipTables(decompile::GraphEdit& graph, std::string_view graphKey, const ClipLister& list)
    {
        DynamicClipReport report;
        const std::string project = ProjectRootOf(graphKey);
        const auto&       reg     = graph.Registry();
        std::vector<io::SchemaObject*> gens;
        graph.ForEach(kGenerator, [&](io::SchemaObject& so) { gens.push_back(&so); });
        for (io::SchemaObject* gen : gens) {
            const std::string genName = gen->Str("name");
            std::vector<FolderClip> clips = list ? list(project, gen->Str("folder")) : std::vector<FolderClip>{};
            std::sort(clips.begin(), clips.end(), [](const FolderClip& a, const FolderClip& b) { return a.animationName < b.animationName; });
            clips.erase(std::unique(clips.begin(), clips.end(), [](const FolderClip& a, const FolderClip& b) { return a.animationName == b.animationName; }),
                        clips.end());

            auto& rows = gen->FieldRef("rows").objs;
            rows.clear();
            for (const FolderClip& c : clips) {
                auto row = graph.Make("CBDynamicClipRow");
                auto clip = MakeClip(graph, *gen, c.animationName, genName + "/" + Stem(c.animationName));
                if (!row || !clip) continue;
                row->FieldRef("clip").obj = clip;
                row->SetFloat("weight", 1.0f);
                if (!c.unitText.empty()) {
                    std::string err;
                    const std::string where = std::string(graphKey) + ": " + c.animationName;
                    if (const auto sel = decompile::ClipSelectionOfAnimation(c.unitText, reg, where, err)) {
                        row->FieldRef("condition").obj = sel->FieldRef("condition").obj;
                        row->SetInt("priority", sel->GetInt("priority"));
                        row->SetFloat("weight", sel->GetFloat("weight"));
                        row->SetInt("flags", sel->GetInt("flags"));
                    } else if (!err.empty()) {
                        report.warnings.push_back(err + " - the clip keeps the default selection");
                    }
                }
                rows.push_back(std::move(row));
            }

            const std::string& fallback = gen->Str("fallback");
            gen->FieldRef("fallbackClip").obj = fallback.empty() ? nullptr : MakeClip(graph, *gen, fallback, genName + "/fallback");
            if (rows.empty() && fallback.empty())
                report.warnings.push_back(std::string(graphKey) + ": " + kGenerator + " '" + genName + "' has no clip in folder '" +
                                          gen->Str("folder") + "' and no fallback - it plays nothing");
            ++report.generators;
            report.rows += rows.size();
        }
        return report;
    }

    std::vector<std::string> DynamicClipFolders(decompile::GraphEdit& graph)
    {
        std::vector<std::string> out;
        graph.ForEach(kGenerator, [&](io::SchemaObject& so) {
            const std::string& f = so.Str("folder");
            if (!f.empty() && std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
        });
        return out;
    }

}  // namespace CB::engine::compile
