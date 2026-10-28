#include <rules/Rules.h>

#include <codec/format/RigIndex.h>
#include <codec/resource/PathKey.h>
#include <disk/Disk.h>
#include <exec/Parallel.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <unordered_set>

namespace CB::engine::rules {

namespace fs = std::filesystem;
using codec::resource::PathKey;
using skeleton::RigIndex;

void RegisterBuiltinOps(OpRegistry& reg);

const char* StageName(Stage s) { return s == Stage::Regen ? "regen" : "converter"; }

// ── stores ──────────────────────────────────────────────────────────────────────────────────────────────────────────

DirUnitStore::DirUnitStore(std::string root, std::function<const std::vector<std::string>*(const std::string&)> bones)
    : m_root(std::move(root)), m_bones(std::move(bones)) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(m_root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fe;
        if (!it->is_directory(fe) || !codec::resource::HasExt(it->path().filename().string(), ".hkx")) continue;
        const auto graph = disk::ReadFile(it->path() / "graph.yaml");
        if (!graph) continue;
        it.disable_recursion_pending();
        m_units.push_back({ fs::relative(it->path(), m_root, fe).generic_string(), codec::HkyArchive::KindOfGraphYaml(*graph) });
    }
    std::sort(m_units.begin(), m_units.end(), [](const auto& a, const auto& b) { return a.prefix < b.prefix; });
}

std::shared_ptr<const common::IUnitSource> DirUnitStore::Source(const std::string& key) const {
    return std::make_shared<common::DiskUnitSource>(Dir(key));
}

std::string DirUnitStore::Dir(const std::string& key) const { return (fs::path(m_root) / key).string(); }

ArchiveUnitStore::ArchiveUnitStore(std::shared_ptr<const codec::HkyArchive> arc,
                                   std::function<const std::vector<std::string>*(const std::string&)> bones)
    : m_arc(std::move(arc)), m_bones(std::move(bones)) {}

// ── context ─────────────────────────────────────────────────────────────────────────────────────────────────────────

Context::Context(IUnitStore& store, const schema::SchemaRegistry& reg) : m_store(store), m_reg(reg), m_list(store.Units()) {
    for (const auto& u : m_list) m_byIdentity.emplace(PathKey(u.prefix), u.prefix);
}

std::vector<std::string> Context::Units(UnitKind kind) const {
    std::vector<std::string> out;
    for (const auto& u : m_list) if (u.kind == kind) out.push_back(u.prefix);
    return out;
}

std::string Context::Find(const std::string& key) const {
    const auto it = m_byIdentity.find(PathKey(key));
    return it == m_byIdentity.end() ? std::string{} : it->second;
}

namespace {
bool parseUnit(IUnitStore& store, const schema::SchemaRegistry& reg, const std::string& key, decompile::ParsedHky& out,
               std::string& err) {
    const auto src = store.Source(key);
    if (!src) { err = key + ": not in the unit set"; return false; }
    if (!decompile::ParseHkyReflective(*src, reg, out, err, store.Bones(key), key)) { err = key + ": " + err; return false; }
    return true;
}
}  // namespace

decompile::ParsedHky* Context::Unit(const std::string& key, std::string& err) {
    if (const auto it = m_units.find(key); it != m_units.end()) return it->second.get();
    auto p = std::make_unique<decompile::ParsedHky>();
    if (!parseUnit(m_store, m_reg, key, *p, err)) return nullptr;
    return m_units.emplace(key, std::move(p)).first->second.get();
}

bool Context::Prefetch(const std::vector<std::string>& keys, std::string& err) {
    std::vector<std::string> todo;
    for (const auto& k : keys) if (!m_units.count(k)) todo.push_back(k);
    std::vector<std::unique_ptr<decompile::ParsedHky>> parsed(todo.size());
    std::vector<std::string>                           errs(todo.size());
    exec::ParallelFor(todo.size(), [&](std::size_t i) {
        auto p = std::make_unique<decompile::ParsedHky>();
        if (parseUnit(m_store, m_reg, todo[i], *p, errs[i])) parsed[i] = std::move(p);
    });
    for (std::size_t i = 0; i < todo.size(); ++i) {
        if (!parsed[i]) { err = errs[i]; return false; }
        m_units.emplace(todo[i], std::move(parsed[i]));
    }
    return true;
}

void Context::Release(const std::string& key) {
    if (!m_touched.count(key)) m_units.erase(key);
}

const std::vector<Character>& Context::Characters() {
    if (m_characters) return *m_characters;
    auto& out = m_characters.emplace();
    std::set<std::string> seen;
    for (const auto& proj : Units(UnitKind::Project)) {
        const auto src = m_store.Source(proj);
        if (!src) continue;
        const std::string dir = RigIndex::DirOf(PathKey(proj));
        for (const auto& cf : decompile::ReadProjectCharacters(*src, m_reg)) {
            const std::string key = Find(RigIndex::JoinRel(dir, cf));
            if (key.empty() || !seen.insert(key).second) continue;
            const auto csrc = m_store.Source(key);
            const auto cs   = csrc ? decompile::ReadCharacterStrings(*csrc) : std::nullopt;
            Character c{ key, proj, {}, {} };
            if (cs) {
                if (!cs->rigName.empty()) c.rig = RigIndex::JoinRel(dir, cs->rigName);
                if (!cs->behaviorFilename.empty()) c.behavior = Find(RigIndex::JoinRel(dir, cs->behaviorFilename));
            }
            out.push_back(std::move(c));
        }
    }
    return out;
}

bool Context::GraphsOf(const Character& character, std::vector<std::string>& out, std::string& err) {
    if (const auto it = m_graphsOf.find(character.key); it != m_graphsOf.end()) { out = it->second; return true; }
    out.clear();
    if (character.behavior.empty()) { m_graphsOf[character.key]; return true; }
    const std::string dir = RigIndex::DirOf(PathKey(character.project));
    std::unordered_set<std::string> reached{ character.behavior };
    std::deque<std::string>         queue{ character.behavior };
    while (!queue.empty()) {
        const std::string g = queue.front();
        queue.pop_front();
        out.push_back(g);
        const bool cached = Parsed(g);
        const decompile::ParsedHky* hky = Unit(g, err);
        if (!hky) return false;
        std::vector<std::string> refs;
        for (const auto& o : hky->objects) {
            if (!o) continue;
            const auto fields = o->Fields();
            const auto& vals  = o->Values();
            for (std::size_t i = 0; i < fields.size() && i < vals.size(); ++i) {
                if (fields[i]->unitRef != "behavior") continue;
                if (fields[i]->kind == schema::FieldKind::StringArray) refs.insert(refs.end(), vals[i].strs.begin(), vals[i].strs.end());
                else if (!vals[i].str.empty()) refs.push_back(vals[i].str);
            }
        }
        if (!cached) Release(g);
        for (const auto& r : refs) {
            const std::string key = Find(RigIndex::JoinRel(dir, r));
            if (!key.empty() && reached.insert(key).second) queue.push_back(key);
        }
    }
    m_graphsOf[character.key] = out;
    return true;
}

bool Context::RunningCharacters(const std::string& key, std::vector<const Character*>& out, std::string& err) {
    out.clear();
    for (const Character& c : Characters()) {
        std::vector<std::string> graphs;
        if (!GraphsOf(c, graphs, err)) return false;
        if (std::find(graphs.begin(), graphs.end(), key) != graphs.end()) out.push_back(&c);
    }
    return true;
}

const std::vector<std::string>* Context::RigBones(const Character& character) {
    if (character.rig.empty()) return nullptr;
    if (const auto* own = m_store.RigBones(character.rig)) return own;
    const std::string key = Find(character.rig);
    if (key.empty()) return nullptr;
    auto [it, fresh] = m_rigBones.try_emplace(key);
    if (fresh)
        if (const auto src = m_store.Source(key))
            if (auto bones = decompile::ReadSkeletonBones(*src, m_reg, key)) it->second = std::move(*bones);
    return it->second.empty() ? nullptr : &it->second;
}

bool Context::Serve(const std::string& key, std::vector<std::uint8_t>& out, std::string& err) {
    if (const auto it = m_units.find(key); it != m_units.end()) return decompile::SerializeGraph(*it->second, out, err);
    decompile::ParsedHky g;
    return parseUnit(m_store, m_reg, key, g, err) && decompile::SerializeGraph(g, out, err);
}

bool Context::Emit(const std::string& key, const std::string& dir, std::string& err) {
    const auto it = m_units.find(key);
    if (it == m_units.end()) { err = key + ": not parsed"; return false; }
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (ec) { err = dir + ": " + ec.message(); return false; }
    if (!decompile::EmitHkyReflective(it->second->identity, m_reg, dir, err, m_store.Bones(key), key)) { err = key + ": " + err; return false; }
    return true;
}

// ── ops ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

OpRegistry& OpRegistry::Instance() {
    static OpRegistry* reg = [] {
        auto* r = new OpRegistry();
        RegisterBuiltinOps(*r);
        return r;
    }();
    return *reg;
}

void OpRegistry::Register(std::string name, OpFactory factory) {
    std::lock_guard lk(m_mu);
    if (!m_ops.emplace(name, std::move(factory)).second) m_clobbers.push_back(std::move(name));
}

std::unique_ptr<IOp> OpRegistry::Make(const schema::RuleDef& rule, std::string& err) const {
    std::lock_guard lk(m_mu);
    const auto it = m_ops.find(rule.op);
    if (it == m_ops.end()) { err = rule.file + ": rule '" + rule.name + "': no op '" + rule.op + "' is registered"; return nullptr; }
    std::string ferr;
    auto op = it->second(rule, ferr);
    if (!op || !ferr.empty()) { err = rule.file + ": rule '" + rule.name + "': " + (ferr.empty() ? "op refused its descriptor" : ferr); return nullptr; }
    return op;
}

bool OpRegistry::Has(std::string_view name) const {
    std::lock_guard lk(m_mu);
    return m_ops.find(name) != m_ops.end();
}

// ── running ─────────────────────────────────────────────────────────────────────────────────────────────────────────

bool RulesFor(const schema::SchemaRegistry& reg, Stage stage, std::vector<const schema::RuleDef*>& out, std::string& err) {
    out.clear();
    std::map<std::string, const schema::RuleDef*> picked;
    for (const auto& [name, rd] : reg.Rules())
        if (std::find(rd.runs.begin(), rd.runs.end(), StageName(stage)) != rd.runs.end()) picked.emplace(name, &rd);
    for (const auto& [name, rd] : picked)
        if (!OpRegistry::Instance().Has(rd->op)) { err = rd->file + ": rule '" + name + "': no op '" + rd->op + "' is registered"; return false; }
    /// @note Constraints against a rule that does not run at this stage are trivially met.
    std::map<std::string, std::set<std::string>> preds;
    for (const auto& [name, rd] : picked) {
        preds[name];
        for (const auto& a : rd->after)  if (picked.count(a)) preds[name].insert(a);
        for (const auto& b : rd->before) if (picked.count(b)) preds[b].insert(name);
    }
    while (!preds.empty()) {
        const auto ready = std::find_if(preds.begin(), preds.end(), [](const auto& p) { return p.second.empty(); });
        if (ready == preds.end()) {
            err = "rule order cycle among:";
            for (const auto& [name, p] : preds) err += " " + name;
            return false;
        }
        const std::string name = ready->first;
        out.push_back(picked.at(name));
        preds.erase(ready);
        for (auto& [n, p] : preds) p.erase(name);
    }
    return true;
}

bool ApplyRules(Context& ctx, const std::vector<const schema::RuleDef*>& rules, RunResult& out, std::string& err) {
    for (const auto& c : OpRegistry::Instance().Clobbers())
        out.log.push_back("rule op '" + c + "' is registered twice; the first registration runs");
    for (const schema::RuleDef* rd : rules) {
        Applied a;
        a.rule = rd;
        a.op   = OpRegistry::Instance().Make(*rd, err);
        if (!a.op) return false;
        if (!a.op->Apply(ctx, a.record, err)) { err = "rule '" + rd->name + "': " + err; return false; }
        if (const std::string s = a.op->Summary(); !s.empty()) out.log.push_back(rd->name + ": " + s);
        out.applied.push_back(std::move(a));
    }
    return true;
}

bool ApplyRules(Context& ctx, Stage stage, RunResult& out, std::string& err) {
    std::vector<const schema::RuleDef*> rules;
    return RulesFor(ctx.Registry(), stage, rules, err) && ApplyRules(ctx, rules, out, err);
}

bool InverseRules(Context& ctx, const RunResult& run, std::string& err) {
    for (auto it = run.applied.rbegin(); it != run.applied.rend(); ++it) {
        if (!it->record) continue;
        if (!it->op->Inverse(ctx, *it->record, err)) { err = "rule '" + it->rule->name + "' inverse: " + err; return false; }
    }
    return true;
}

void ForEachSelected(const decompile::ParsedHky& hky, const schema::RuleDef& rule,
                     const std::function<void(codec::io::SchemaObject&, std::size_t)>& fn) {
    for (const auto& o : hky.objects) {
        if (!o) continue;
        const auto fields = o->Fields();
        for (std::size_t i = 0; i < fields.size(); ++i)
            for (const auto& t : rule.select)
                if (schema::RuleSelects(*fields[i], t)) { fn(*o, i); break; }
    }
}

}  // namespace CB::engine::rules
