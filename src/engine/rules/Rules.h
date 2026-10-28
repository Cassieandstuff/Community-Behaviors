#pragma once

#include <codec/serialization/HkyArchive.h>
#include <decompile/BehaviorDecompile.h>
#include <interface/reflection/HavokSchema.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

/// @brief RULES — reversible transforms over a whole unit set (graphs, characters and projects together), declared as
///        data (`kind: rule`, schema/metadata/rules/) and run by registered C++ operations.
/// @note  Every op ships its inverse. The regen applies the rules after the emit; its served-bytes gate reverses them
///        in memory, from the records Apply returned in the same run, and compares to the source. Nothing is persisted
///        in the bundle, so the inverse never touches what the source itself authored. The converter runs them forward
///        only, on mod graphs too.
namespace CB::engine::rules {

using UnitKind = codec::HkyArchive::UnitKind;

enum class Stage { Regen, Converter };
/// @brief The `runs:` word for `s` ("regen", "converter").
const char* StageName(Stage s);

/// @brief The units a rule run reads and writes, by serve key ("meshes/actors/character/behaviors/0_master.hkx").
class IUnitStore {
public:
    virtual ~IUnitStore() = default;
    virtual std::vector<codec::HkyArchive::Unit>              Units() const = 0;
    virtual std::shared_ptr<const common::IUnitSource>        Source(const std::string& key) const = 0;
    /// @brief The bones unit `key` parses with (a graph's bound rig, as everything else that reads it parses it); null
    ///        = the rig is unknown (bone fields stay numeric).
    virtual const std::vector<std::string>* Bones(const std::string& key) = 0;
};

/// @brief The units under a directory: every `<path>.hkx/` folder holding a graph.yaml, its kind read from it.
/// @param bones the parse bones per serve key (the host's rig binding); empty = none.
class DirUnitStore final : public IUnitStore {
public:
    DirUnitStore(std::string root, std::function<const std::vector<std::string>*(const std::string&)> bones);
    std::vector<codec::HkyArchive::Unit>       Units() const override { return m_units; }
    std::shared_ptr<const common::IUnitSource> Source(const std::string& key) const override;
    const std::vector<std::string>*            Bones(const std::string& key) override { return m_bones ? m_bones(key) : nullptr; }
    /// @brief The folder unit `key` lives in.
    std::string                                Dir(const std::string& key) const;

private:
    std::string                                                       m_root;
    std::vector<codec::HkyArchive::Unit>                              m_units;
    std::function<const std::vector<std::string>*(const std::string&)> m_bones;
};

/// @brief A packed bundle's units.
class ArchiveUnitStore final : public IUnitStore {
public:
    ArchiveUnitStore(std::shared_ptr<const codec::HkyArchive> arc,
                     std::function<const std::vector<std::string>*(const std::string&)> bones);
    std::vector<codec::HkyArchive::Unit>       Units() const override { return m_arc ? m_arc->units() : std::vector<codec::HkyArchive::Unit>{}; }
    std::shared_ptr<const common::IUnitSource> Source(const std::string& key) const override { return m_arc ? m_arc->source(key) : nullptr; }
    const std::vector<std::string>*            Bones(const std::string& key) override { return m_bones ? m_bones(key) : nullptr; }

private:
    std::shared_ptr<const codec::HkyArchive>                          m_arc;
    std::function<const std::vector<std::string>*(const std::string&)> m_bones;
};

/// @brief A character as the rules see it: its project and what its string data names, resolved to unit keys.
struct Character {
    std::string key;        ///< the character unit
    std::string project;    ///< the project unit listing it
    std::string rig;        ///< the rig skeleton unit key (path identity), empty when unnamed
    std::string behavior;   ///< the root behavior unit key, empty when absent from the set
};

/// @brief The unit set one rule run sees: units parsed on demand and cached, the project / character / graph
///        relations, and which units an op changed.
/// @note  Keys are the store's spelling; a reference (a project-relative path in a unit) finds its unit by path identity
///        (Find), as the engine's own lookup does. Not thread-safe except Prefetch, which parses in parallel.
class Context {
public:
    Context(IUnitStore& store, const schema::SchemaRegistry& reg);

    const schema::SchemaRegistry& Registry() const { return m_reg; }
    IUnitStore&                   Store() { return m_store; }

    /// @brief Every unit of `kind`, in store order.
    std::vector<std::string> Units(UnitKind kind) const;
    /// @brief The unit key path `key` names by path identity (either slash, any case), or empty.
    std::string Find(const std::string& key) const;

    /// @brief Unit `key` parsed (cached); null with `err` set when it does not parse.
    decompile::ParsedHky* Unit(const std::string& key, std::string& err);
    /// @brief Parse every key in `keys` in parallel into the cache; false with `err` naming the first failure.
    bool Prefetch(const std::vector<std::string>& keys, std::string& err);
    /// @brief Whether `key` is cached.
    bool Parsed(const std::string& key) const { return m_units.count(key) != 0; }
    /// @brief Drop a cached unit the run did not change (a touched one stays).
    void Release(const std::string& key);

    /// @brief Mark `key` changed: the host re-emits it after the forward run; the gate serves it from the cache.
    void Touch(const std::string& key) { m_touched.insert(key); }
    const std::set<std::string>& Touched() const { return m_touched; }

    /// @brief Every character a project of the set lists, keys resolved (built once).
    const std::vector<Character>& Characters();
    /// @brief The behavior graphs `character` runs: its root behavior, then every behavior reachable through a
    ///        `unitref: behavior` string of a graph already reached (hkbBehaviorReferenceGenerator.behaviorName),
    ///        breadth-first, each once. References resolve against the character's PROJECT folder.
    /// @return false with `err` set when a reached graph does not parse; a reference to a unit outside the set is
    ///         skipped.
    bool GraphsOf(const Character& character, std::vector<std::string>& out, std::string& err);
    /// @brief The characters running graph `key` (the inverse of GraphsOf over every character), in Characters() order.
    bool RunningCharacters(const std::string& key, std::vector<const Character*>& out, std::string& err);
    /// @brief The bones of a character's own rig (its skeleton unit's animation skeleton); null when unknown.
    const std::vector<std::string>* RigBones(const Character& character);

    /// @brief Serve unit `key` as packfile bytes: the cached (possibly changed) parse, else a fresh parse.
    bool Serve(const std::string& key, std::vector<std::uint8_t>& out, std::string& err);
    /// @brief Write cached unit `key` as a reflective unit into `dir` (cleared first).
    bool Emit(const std::string& key, const std::string& dir, std::string& err);

private:
    IUnitStore&                                                   m_store;
    const schema::SchemaRegistry&                                 m_reg;
    std::vector<codec::HkyArchive::Unit>                          m_list;
    std::map<std::string, std::string>                            m_byIdentity;   ///< PathKey -> key
    std::map<std::string, std::unique_ptr<decompile::ParsedHky>>  m_units;
    std::set<std::string>                                         m_touched;
    std::optional<std::vector<Character>>                         m_characters;
    std::map<std::string, std::vector<std::string>>               m_graphsOf;     ///< character key -> GraphsOf
    std::map<std::string, std::vector<std::string>>               m_rigBones;     ///< rig key -> bones (empty = none)
};

/// @brief What an op's Apply recorded for its Inverse: op-defined, keyed by stable identity (unit key, node FormID,
///        field name), never by pointer — the inverse runs on a fresh parse of the emitted units.
struct Record {
    virtual ~Record() = default;
};

/// @brief One rule's operation, made per run from its descriptor (whose `params:` it reads).
class IOp {
public:
    virtual ~IOp() = default;
    /// @brief Transform the unit set; Touch every unit changed. `out` may stay null when nothing changed.
    virtual bool Apply(Context& ctx, std::unique_ptr<Record>& out, std::string& err) = 0;
    /// @brief Undo exactly what Apply recorded, on the units as Apply left them; Touch every unit changed.
    virtual bool Inverse(Context& ctx, const Record& record, std::string& err) = 0;
};

using OpFactory = std::function<std::unique_ptr<IOp>(const schema::RuleDef& rule, std::string& err)>;

/// @brief The process-wide op registry, keyed by the exact `op:` name.
/// @note  A name registered twice keeps its first op; every later one is listed in Clobbers(), which a run reports.
///        The engine's own ops are registered by Instance() itself: the engine is a static library, so a TU holding
///        only a static registrar would never be linked in. CB_REGISTER_RULE_OP is for ops compiled into a program.
class OpRegistry {
public:
    static OpRegistry& Instance();
    void Register(std::string name, OpFactory factory);
    std::unique_ptr<IOp> Make(const schema::RuleDef& rule, std::string& err) const;
    bool Has(std::string_view name) const;
    const std::vector<std::string>& Clobbers() const { return m_clobbers; }

private:
    OpRegistry() = default;
    mutable std::mutex                             m_mu;
    std::map<std::string, OpFactory, std::less<>>  m_ops;
    std::vector<std::string>                       m_clobbers;
};

template <class Op>
struct AutoRegisterOp {
    explicit AutoRegisterOp(const char* name) {
        OpRegistry::Instance().Register(name, [](const schema::RuleDef& r, std::string& e) -> std::unique_ptr<IOp> {
            return std::make_unique<Op>(r, e);
        });
    }
};

/// @brief One rule applied in a run, with what its op recorded.
struct Applied {
    const schema::RuleDef*  rule = nullptr;
    std::unique_ptr<IOp>    op;
    std::unique_ptr<Record> record;   ///< null = the rule changed nothing
};
struct RunResult {
    std::vector<Applied>     applied;   ///< in run order
    std::vector<std::string> log;
};

/// @brief The rules of `reg` that run at `stage`, ordered by their `order:` constraints (ties by name).
/// @return false with `err` set on a dependency cycle or a rule whose op is not registered.
bool RulesFor(const schema::SchemaRegistry& reg, Stage stage, std::vector<const schema::RuleDef*>& out, std::string& err);

/// @brief Apply `rules` in order over `ctx`.
bool ApplyRules(Context& ctx, const std::vector<const schema::RuleDef*>& rules, RunResult& out, std::string& err);
/// @brief Every rule `reg` runs at `stage` (RulesFor + ApplyRules).
bool ApplyRules(Context& ctx, Stage stage, RunResult& out, std::string& err);
/// @brief Undo `run` over `ctx` (a fresh context over the units the forward run left), last rule first.
bool InverseRules(Context& ctx, const RunResult& run, std::string& err);

/// @brief Every (object, field index) of the cached unit `hky` whose field is of one of `rule`'s `select:` kinds.
void ForEachSelected(const decompile::ParsedHky& hky, const schema::RuleDef& rule,
                     const std::function<void(codec::io::SchemaObject&, std::size_t)>& fn);

}  // namespace CB::engine::rules

/// @brief Register op `Type` (constructible from (const schema::RuleDef&, std::string& err)) under `Name` at static
///        init, in a program's own TU.
#define CB_DETAIL_RULE_OP_CONCAT2(a, b) a##b
#define CB_DETAIL_RULE_OP_CONCAT(a, b)  CB_DETAIL_RULE_OP_CONCAT2(a, b)
#define CB_REGISTER_RULE_OP(Name, Type) \
    static ::CB::engine::rules::AutoRegisterOp<Type> CB_DETAIL_RULE_OP_CONCAT(_cb_rule_op_reg_, __LINE__)(Name)
