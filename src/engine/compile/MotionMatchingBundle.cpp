#include <compile/MotionMatchingBundle.h>

#include <codec/formid/FormId.h>
#include <codec/resource/PathKey.h>
#include <codec/serialization/HavokIo.h>
#include <decompile/BehaviorDecompile.h>
#include <disk/Disk.h>
#include <interface/HavokEnums.h>
#include <text/Text.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace CB::engine::compile {

    namespace {

        namespace fs  = std::filesystem;
        namespace io  = CB::engine::codec::io;
        namespace dc  = CB::engine::decompile;
        namespace res = CB::engine::codec::resource;

        using Obj = std::shared_ptr<io::SchemaObject>;

        long EnumValue(const char* table, const char* item)
        {
            const auto& t  = CB::engine::common::enums::EnumTable(table);
            const auto  it = t.find(item);
            if (it == t.end()) throw std::runtime_error(std::string("enum ") + table + " has no " + item);
            return it->second;
        }

        std::string FileName(std::string_view path)
        {
            const auto slash = path.find_last_of("/\\");
            return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
        }

        /// @brief Every check both outputs depend on: the databases and their clips. Throws with why.
        void ValidateDatabases(const MmBundleSpec& spec)
        {
            if (spec.databases.empty()) throw std::runtime_error("no databases");
            std::set<std::string> names;
            for (const auto& db : spec.databases) {
                if (db.name.empty()) throw std::runtime_error("a database has no name");
                if (!names.insert(db.name).second) throw std::runtime_error("database '" + db.name + "' is named twice");
                if (db.clips.empty()) throw std::runtime_error("database '" + db.name + "' holds no clips");
                for (const auto& c : db.clips) {
                    if (MmAnimationName(c.path, spec.rosterRoot).empty())
                        throw std::runtime_error("database '" + db.name + "': '" + c.path + "' is not a .hkx file below '" +
                                                 spec.rosterRoot + "'");
                    if (c.flags & ~(kMmDisableReselection | kMmLooping))
                        throw std::runtime_error("database '" + db.name + "': '" + c.path + "' has unknown flag bits");
                }
            }
        }

        /// @brief The graph's variable declarations, in order.
        std::vector<MmVariable> Variables(const MmBundleSpec& spec)
        {
            std::vector<MmVariable> vars{ { spec.clipVariable, "VARIABLE_TYPE_INT32", 0 }, { spec.timeVariable, "VARIABLE_TYPE_REAL", 0 } };
            for (const auto& v : kMmChooserVariables) vars.push_back({ std::string(v.name), std::string(v.type), 0 });
            vars.insert(vars.end(), spec.extraVariables.begin(), spec.extraVariables.end());
            return vars;
        }

        void ValidateBehavior(const MmBundleSpec& spec)
        {
            ValidateDatabases(spec);
            if (!res::HasExt(spec.unitPath, ".hkx")) throw std::runtime_error("unit path '" + spec.unitPath + "' is not a .hkx unit");
            if (spec.selectorName.empty() || spec.clipVariable.empty() || spec.timeVariable.empty())
                throw std::runtime_error("the selector name, clip variable and time variable must be named");
            std::set<std::string> dbs, vars;
            for (const auto& db : spec.databases) dbs.insert(db.name);
            for (const auto& v : Variables(spec)) {
                if (v.name.empty()) throw std::runtime_error("a variable has no name");
                if (!vars.insert(v.name).second) throw std::runtime_error("variable '" + v.name + "' is declared twice");
            }
            const auto known = [&](const std::string& name, const std::string& where) {
                if (!dbs.count(name)) throw std::runtime_error(where + " names no database '" + name + "'");
            };
            /// @note A row's `when` is not checked here: the expression language's vocabulary is registered in the plugin,
            ///       which compiles it when it reads the graph and logs one that fails.
            for (std::size_t r = 0; r < spec.rows.size(); ++r)
                for (const auto& d : spec.rows[r].databases) known(d, "chooser row " + std::to_string(r));
            for (const auto& d : spec.fallback) known(d, "the chooser's fallback");
        }

        Obj Make(dc::GraphEdit& edit, std::string_view cls)
        {
            auto o = edit.Make(cls);
            if (!o) throw std::runtime_error("class " + std::string(cls) + " is not in the schema");
            return o;
        }

        Obj Binding(dc::GraphEdit& edit, const char* member, std::size_t variable)
        {
            auto set = Make(edit, "hkbVariableBindingSet");
            auto b   = Make(edit, "hkbVariableBindingSetBinding");
            b->Str("memberPath") = member;
            b->SetInt("variableIndex", static_cast<std::int64_t>(variable));
            b->SetInt("bitIndex", -1);
            b->SetInt("bindingType", EnumValue("BindingType", "BINDING_TYPE_VARIABLE"));
            set->FieldRef("bindings").objs.push_back(b);
            set->SetInt("indexOfBindingToEnable", -1);
            return set;
        }

        /// @brief The graph, as a SchemaObject graph, serialized.
        std::vector<std::uint8_t> BuildGraphBytes(const MmBundleSpec& spec, const schema::SchemaRegistry& reg)
        {
            dc::ParsedHky  g;
            dc::GraphEdit edit(g, reg);
            auto root = Make(edit, "hkRootLevelContainer"), entry = Make(edit, "hkRootLevelContainerNamedVariant");
            auto graph = Make(edit, "hkbBehaviorGraph"), data = Make(edit, "hkbBehaviorGraphData");
            g.root = root;
            entry->Str("name")         = "hkbBehaviorGraph";
            entry->Str("className")    = "hkbBehaviorGraph";
            entry->FieldRef("variant").obj = graph;
            root->FieldRef("namedVariants").objs.push_back(entry);
            const std::string unit = FileName(spec.unitPath);
            graph->Str("name") = unit.substr(0, unit.size() - 4) + ".hkb";
            graph->SetInt("variableMode", EnumValue("VariableMode", "VARIABLE_MODE_DISCARD_WHEN_INACTIVE"));
            graph->FieldRef("data").obj = data;

            std::string err;
            for (const auto& v : Variables(spec))
                if (edit.AddVariable(v.name, v.type, v.word, err) != dc::GraphEdit::Add::Added)
                    throw std::runtime_error("variable '" + v.name + "': " + (err.empty() ? "already declared" : err));
            const std::size_t clipIndex = *edit.VariableIndex(spec.clipVariable);
            const std::size_t timeIndex = *edit.VariableIndex(spec.timeVariable);

            auto selector = Make(edit, "CBPoseSearchGenerator");
            graph->FieldRef("rootGenerator").obj = selector;
            selector->Str("name")                     = spec.selectorName;
            selector->FieldRef("variableBindingSet").obj = Binding(edit, "selectedIndex", clipIndex);

            auto& children = selector->FieldRef("generators").objs;
            auto& dbs      = selector->FieldRef("databases").objs;
            std::vector<std::uint8_t> flags;
            const long userControlled = EnumValue("PlaybackMode", "MODE_USER_CONTROLLED");
            for (const auto& db : spec.databases) {
                auto d = Make(edit, "CBPoseSearchDatabase");
                d->Str("name") = db.name;
                d->SetInt("firstChild", static_cast<std::int64_t>(children.size()));
                d->SetInt("numChildren", static_cast<std::int64_t>(db.clips.size()));
                d->Str("baseCostBias")           = db.baseCostBias;
                d->Str("continuingPoseCostBias") = db.continuingPoseCostBias;
                d->Str("loopingCostBias")        = db.loopingCostBias;
                d->FieldRef("tags").strs = db.tags;
                dbs.push_back(d);
                for (const auto& c : db.clips) {
                    const std::string stem = FileName(c.path);
                    auto clip = Make(edit, "hkbClipGenerator");
                    clip->Str("name")          = stem.substr(0, stem.size() - 4);
                    clip->Str("animationName") = c.animationName.empty() ? MmAnimationName(c.path, spec.rosterRoot) : c.animationName;
                    clip->SetFloat("playbackSpeed", 1.0f);
                    clip->SetInt("animationBindingIndex", -1);
                    clip->SetInt("mode", userControlled);
                    clip->FieldRef("variableBindingSet").obj = Binding(edit, "userControlledTimeFraction", timeIndex);
                    children.push_back(clip);
                    flags.push_back(c.flags);
                }
            }
            selector->SetArray<std::uint8_t>("childFlags", flags);

            auto chooser = Make(edit, "CBPoseSearchChooser");
            for (const auto& r : spec.rows) {
                auto row = Make(edit, "CBPoseSearchChooserRow");
                row->Str("when")                = r.when;
                row->FieldRef("databases").strs = r.databases;
                chooser->FieldRef("rows").objs.push_back(row);
            }
            chooser->FieldRef("fallback").strs = spec.fallback;
            selector->FieldRef("chooser").obj  = chooser;

            /// @note Every object takes a local id (class names first), and a local id is 16 bits.
            if (g.objects.size() + 256 > 0xFFFFu)
                throw std::runtime_error(std::to_string(g.objects.size()) + " objects is past what 16-bit FormIDs number");

            std::vector<std::uint8_t> bytes;
            if (!dc::SerializeGraph(g, bytes, err)) throw std::runtime_error(err);
            return bytes;
        }

        /// @brief A fresh directory under the system temp folder, removed with everything in it on destruction.
        class TempDir {
        public:
            TempDir()
            {
                std::random_device rd;
                const auto         seed = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
                for (int attempt = 0; attempt < 16; ++attempt) {
                    char name[40];
                    std::snprintf(name, sizeof(name), "cb-mm-%08x%08llx", rd(), seed + static_cast<unsigned long long>(attempt));
                    std::error_code ec;
                    const fs::path  p = fs::temp_directory_path(ec) / name;
                    if (!ec && fs::create_directory(p, ec) && !ec) { m_path = p; return; }
                }
                throw std::runtime_error("cannot create a temporary directory");
            }
            ~TempDir()
            {
                std::error_code ec;
                fs::remove_all(disk::LongPath(m_path), ec);
            }
            TempDir(const TempDir&)            = delete;
            TempDir& operator=(const TempDir&) = delete;
            const fs::path& Path() const { return m_path; }

        private:
            fs::path m_path;
        };

        /// @brief `bytes` decompiled into unit files under `unitPath`, after proving they parse back to `bytes`.
        MmFiles DecompileUnit(const std::vector<std::uint8_t>& bytes, const std::string& unitPath, const schema::SchemaRegistry& reg)
        {
            TempDir           tmp;
            const std::string dir = tmp.Path().string();
            std::string       err;
            if (!dc::DecompileBehaviorReflective(bytes, reg, dir, err)) throw std::runtime_error(err);

            dc::ParsedHky             back;
            std::vector<std::uint8_t> again;
            if (!dc::ParseHkyReflective(dir, reg, back, err)) throw std::runtime_error("the emitted unit does not parse: " + err);
            if (!dc::SerializeGraph(back, again, err)) throw std::runtime_error(err);
            if (again != bytes) {
                std::size_t i = 0;
                while (i < again.size() && i < bytes.size() && again[i] == bytes[i]) ++i;
                throw std::runtime_error("the emitted unit parses to a different graph (first byte difference at " +
                                         std::to_string(i) + ")");
            }

            std::vector<std::string> rels;
            std::error_code          ec;
            const fs::path           base = disk::LongPath(tmp.Path());
            for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file()) rels.push_back(it->path().lexically_relative(base).generic_string());
            if (ec) throw std::runtime_error("cannot list the emitted unit: " + ec.message());
            std::sort(rels.begin(), rels.end());
            MmFiles files;
            for (const auto& rel : rels) {
                auto text = disk::ReadFile(tmp.Path() / rel);
                if (!text) throw std::runtime_error("cannot read back the emitted " + rel);
                files.emplace_back(unitPath + "/" + rel, std::move(*text));
            }
            return files;
        }

        template <class F>
        bool Guard(std::string& err, const char* what, F&& body)
        {
            try {
                body();
                return true;
            } catch (const std::exception& e) {
                err = std::string(what) + ": " + e.what();
                return false;
            }
        }

    }  // namespace

    std::string MmAnimationName(std::string_view clipPath, std::string_view rosterRoot)
    {
        std::string root = res::PathKey(rosterRoot);
        if (!root.empty() && root.back() != '/') root += '/';
        if (!res::HasExt(clipPath, ".hkx") || !res::PathKey(clipPath).starts_with(root)) return {};
        std::string name(clipPath.substr(root.size()));
        std::replace(name.begin(), name.end(), '/', '\\');
        return name;
    }

    bool BuildMotionMatchingBehavior(const MmBundleSpec& spec, const schema::SchemaRegistry& reg, MmFiles& out, std::string& err)
    {
        return Guard(err, "motion-matching behavior", [&] {
            ValidateBehavior(spec);
            auto files = DecompileUnit(BuildGraphBytes(spec, reg), spec.unitPath, reg);
            out.insert(out.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
        });
    }

    bool BuildMotionMatchingRosters(const MmBundleSpec& spec, MmFiles& out, std::string& err)
    {
        return Guard(err, "motion-matching rosters", [&] {
            ValidateDatabases(spec);
            std::unordered_set<std::string> seen;
            std::string                     names;
            for (const auto& db : spec.databases)
                for (const auto& c : db.clips) {
                    const std::string name = c.animationName.empty() ? MmAnimationName(c.path, spec.rosterRoot) : c.animationName;
                    if (seen.insert(name).second) names += (names.empty() ? "" : ", ") + text::Quote(name);
                }
            MmFiles files;
            for (const auto& [path, root] : spec.characters) {
                if (path.empty()) throw std::runtime_error("a character has no unit path");
                if (!CB::engine::codec::formid::parse(root))
                    throw std::runtime_error(path + ": '" + root + "' is not a FormID like 0001x000b");
                files.emplace_back(path + "/graph.yaml",
                    "id: " + root + "\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbCharacterData':\n    variant:\n"
                    "      stringData:\n        animationNames: [" + names + "]\n");
            }
            out.insert(out.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
        });
    }

    bool BuildMotionMatchingBundle(const MmBundleSpec& spec, const schema::SchemaRegistry& reg, MmFiles& out, std::string& err)
    {
        MmFiles files;
        if (!BuildMotionMatchingBehavior(spec, reg, files, err) || !BuildMotionMatchingRosters(spec, files, err)) return false;
        out.insert(out.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
        return true;
    }

}  // namespace CB::engine::compile
