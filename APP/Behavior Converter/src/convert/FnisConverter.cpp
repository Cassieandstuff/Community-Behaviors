#include "FnisConverter.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace CommunityBehaviors::fnis {
namespace {

void Say(const LogFn& log, const std::string& s) { if (log) log(s); }

// Long-path form of a path (Windows \\?\ prefix) so the DEEP behavior tree — which overruns
// MAX_PATH once rooted under an MO2 mod folder — can be created and written. Without this the
// deep container/clip files fail to write SILENTLY (std::ofstream just doesn't open), shipping
// a truncated bundle. Mirrors HkyArchive::PackDirectory's fix on the write side.
fs::path LongPath(const fs::path& p)
{
#ifdef _WIN32
    std::error_code ec;
    fs::path        abs = fs::absolute(p, ec);
    if (ec) return p;
    std::wstring       w   = abs.native();
    const std::wstring pfx = L"\\\\?\\";
    if (w.compare(0, pfx.size(), pfx) != 0) w = pfx + w;
    return fs::path(w);
#else
    return p;
#endif
}

std::string Fmt(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%f", static_cast<double>(v));
    return buf;
}

// Behavior-graph animation path for a list animFile. ScanForLists makes animFile relative
// to the actor's animations/ folder (e.g. "Dance\dance1.hkx"); the graph — the clip's
// animationName AND the character animationNames roster — references it as
// "Animations\Dance\dance1.hkx" (exactly what FNIS's own GenerateFNISforUsers emits). Without
// the "Animations\" prefix the engine can't find the file and the clip binds to nothing → the
// actor A-poses. Idempotent if the path already begins with "Animations\".
std::string AnimPath(const std::string& animFile)
{
    static const std::string pfx = "animations\\";
    bool                     has = animFile.size() >= pfx.size();
    for (std::size_t i = 0; has && i < pfx.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(animFile[i])) != pfx[i]) has = false;
    return has ? animFile : ("Animations\\" + animFile);
}

bool WriteFile(const fs::path& p, const std::string& content, std::size_t& count)
{
    const fs::path  lp = LongPath(p);
    std::error_code ec;
    fs::create_directories(lp.parent_path(), ec);
    std::ofstream f(lp, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (f) { ++count; return true; }
    return false;
}

// ── YAML emission helpers ───────────────────────────────────────────────────

// Emit the FNIS container graph unit (one per ≤30k clip batch).
//
// Structure mirrors what YamlBehaviorLoader expects:
//   behavior.yaml         — root manifest
//   data/graphdata.yaml   — events
//   generators/           — ReferencePoseGenerator
//   clips/                — one file per animation
//   states/               — root SM + entry state + per-clip states
//   transitions/          — blend effects
void EmitContainer(const fs::path&               unitDir,
                   const std::string&             containerName,
                   const std::vector<AnimGroup>&  groups,
                   std::vector<std::pair<std::string, int>>& outEvents,   // (event, dance stateId)
                   std::vector<std::string>&      outAnimNames,
                   std::size_t&                   fileCount,
                   std::vector<std::string>&      warnings,
                   const LogFn&                   log)
{
    // Collect unique events.
    std::vector<std::string> events;
    std::set<std::string>    eventSet;
    auto ensureEvent = [&](const std::string& ev) {
        if (eventSet.insert(ev).second)
            events.push_back(ev);
    };

    int chainCounter = 0;
    auto nextChainEvent = [&]() {
        std::string ev = "FNIS_chain_" + std::to_string(++chainCounter);
        ensureEvent(ev);
        return ev;
    };

    // Pre-scan for all events (before emitting graphdata).
    for (const auto& g : groups) {
        ensureEvent(g.head->event);
        for (const auto* c : g.continuations) ensureEvent(c->event);
        for (const auto& te : g.head->triggeredEvents) ensureEvent(te.event);
        for (const auto* c : g.continuations)
            for (const auto& te : c->triggeredEvents) ensureEvent(te.event);
    }
    // Reserve room for chain events (one per sequence link).
    for (const auto& g : groups)
        if (!g.continuations.empty())
            for (std::size_t i = 0; i < g.continuations.size(); ++i)
                ensureEvent("FNIS_chain_" + std::to_string(i + chainCounter + 1));
    chainCounter = 0;  // reset for actual emission

    // ── behavior.yaml ──
    {
        std::string y;
        y += "packfile:\n  classversion: 8\n  contentsversion: \"hk_2010.2.0-r1\"\n\n";
        y += "behavior:\n";
        y += "  name: \"" + containerName + ".hkb\"\n";
        y += "  variableMode: VARIABLE_MODE_DISCARD_WHEN_INACTIVE\n";
        y += "  rootGenerator: FNIS_RootSM\n";
        y += "  data: graphdata\n";
        WriteFile(unitDir / "behavior.yaml", y, fileCount);
    }

    // ── data/graphdata.yaml ──
    {
        std::string y = "events:\n";
        for (const auto& ev : events)
            y += "  - name: " + ev + "\n    flags: \"0\"\n";
        WriteFile(unitDir / "data" / "graphdata.yaml", y, fileCount);
    }

    // ── transitions ──
    // No entry/idle state and no return-to-idle wildcard: per the FNIS design, FNIS.hkx holds ONLY
    // dance states, each fired DIRECTLY by its own global wildcard. Exit is not ours to solve — when
    // a dance ends the actor falls back to 0_master's root (its default idle), or the mod ships its
    // own return-to-idle. So there is no FNIS_IdlePose generator, no FNIS_Entry state, and no
    // FNIS_BlendOut. (This is the OPPOSITE of ER, which enters+locks a sub-behavior with a variable.)
    auto emitTransition = [&](const std::string& name, const std::string& duration) {
        std::string y;
        y += "class: hkbBlendingTransitionEffect\n";
        y += "name: " + name + "\n";
        y += "selfTransitionMode: SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC\n";
        y += "duration: " + duration + "\n";
        y += "blendCurve: BLEND_CURVE_SMOOTH\n";
        WriteFile(unitDir / "transitions" / (name + ".yaml"), y, fileCount);
    };
    emitTransition("FNIS_BlendIn", "0.200000");
    emitTransition("FNIS_ChainBlend", "0.000000");

    // ── clips + states + SM wildcards ──
    // Only dance states — no entry/idle state. First dance is stateId 0.
    std::string smWildcards;
    std::string smStates;
    int nextStateId = 0;
    std::set<std::string> usedEvents;

    for (const auto& g : groups) {
        std::vector<const AnimDecl*> chain;
        chain.push_back(g.head);
        for (const auto* c : g.continuations) chain.push_back(c);

        struct Built { std::string stateName; int stateId; std::string event; };
        std::vector<Built> built;

        for (std::size_t ci = 0; ci < chain.size(); ++ci) {
            const auto& a = *chain[ci];

            if (!usedEvents.insert(a.event).second) {
                warnings.push_back("duplicate event '" + a.event + "' — skipping");
                continue;
            }

            std::string clipName  = "CLIP_" + a.event;
            std::string stateName = "ST_"   + a.event;

            // Clip YAML
            {
                std::string y;
                y += "class: hkbClipGenerator\n";
                y += "name: " + clipName + "\n";
                y += "animationName: " + AnimPath(a.animFile) + "\n";
                y += std::string("mode: ") + (a.acyclic ? "MODE_SINGLE_PLAY" : "MODE_LOOPING") + "\n";
                y += "playbackSpeed: 1.000000\n";

                // Triggers — only the animation's OWN annotation events. No synthetic return event:
                // exit is the mod's / root behavior's job (see the transitions note above).
                if (!a.triggeredEvents.empty()) {
                    y += "triggers:\n";
                    for (const auto& te : a.triggeredEvents) {
                        y += "  - localTime: " + Fmt(te.time) + "\n";
                        y += "    event: " + te.event + "\n";
                    }
                }
                // Chain trigger will be appended below for sequences.
                WriteFile(unitDir / "clips" / (clipName + ".yaml"), y, fileCount);
            }

            // State YAML (transitions added below for sequences)
            {
                std::string y;
                y += "class: hkbStateMachineStateInfo\n";
                y += "name: " + stateName + "\n";
                y += "stateId: " + std::to_string(nextStateId) + "\n";
                y += "generator: " + clipName + "\n";
                WriteFile(unitDir / "states" / (stateName + ".yaml"), y, fileCount);
            }

            smStates += "  - " + stateName + "\n";
            built.push_back({stateName, nextStateId, a.event});
            ++nextStateId;

            outAnimNames.push_back(AnimPath(a.animFile));
        }

        if (built.empty()) continue;

        // Wildcard: head event → first state
        {
            std::string transName = "FNIS_BlendIn";
            if (g.head->blendTime >= 0.f) {
                transName = "FNIS_Blend_" + g.head->event;
                emitTransition(transName, Fmt(g.head->blendTime));
            }
            smWildcards += "  - event: " + g.head->event + "\n";
            smWildcards += "    toState: " + built[0].stateName + "\n";
            smWildcards += "    transition: " + transName + "\n";
            smWildcards += "    flags: FLAG_IS_LOCAL_WILDCARD|FLAG_IS_GLOBAL_WILDCARD|FLAG_DISABLE_CONDITION\n";
        }
        outEvents.push_back({ g.head->event, built[0].stateId });

        // Continuation wildcards (also independently triggerable).
        for (std::size_t i = 1; i < built.size(); ++i) {
            smWildcards += "  - event: " + built[i].event + "\n";
            smWildcards += "    toState: " + built[i].stateName + "\n";
            smWildcards += "    transition: FNIS_BlendIn\n";
            smWildcards += "    flags: FLAG_IS_LOCAL_WILDCARD|FLAG_IS_GLOBAL_WILDCARD|FLAG_DISABLE_CONDITION\n";
            outEvents.push_back({ built[i].event, built[i].stateId });
        }

        // Sequence chaining: append end-of-clip triggers + state-local transitions.
        if (built.size() > 1) {
            for (std::size_t i = 0; i + 1 < built.size(); ++i) {
                std::string chainEv = nextChainEvent();

                // Re-read the clip file to append the chain trigger.
                fs::path clipPath = unitDir / "clips" / ("CLIP_" + chain[i]->event + ".yaml");
                std::ifstream fin(LongPath(clipPath), std::ios::binary);
                std::string clipYaml((std::istreambuf_iterator<char>(fin)),
                                      std::istreambuf_iterator<char>());
                fin.close();
                if (clipYaml.find("triggers:") == std::string::npos)
                    clipYaml += "triggers:\n";
                clipYaml += "  - localTime: 0.000000\n";
                clipYaml += "    event: " + chainEv + "\n";
                clipYaml += "    relativeToEndOfClip: true\n";
                clipYaml += "    acyclic: true\n";
                {
                    std::ofstream fout(LongPath(clipPath), std::ios::binary | std::ios::trunc);
                    fout.write(clipYaml.data(), static_cast<std::streamsize>(clipYaml.size()));
                }

                // Re-read the state file to append the chain transition.
                fs::path statePath = unitDir / "states" / (built[i].stateName + ".yaml");
                std::ifstream sin(LongPath(statePath), std::ios::binary);
                std::string stateYaml((std::istreambuf_iterator<char>(sin)),
                                       std::istreambuf_iterator<char>());
                sin.close();
                stateYaml += "transitions:\n";
                stateYaml += "  - event: " + chainEv + "\n";
                stateYaml += "    toState: " + built[i + 1].stateName + "\n";
                stateYaml += "    transition: FNIS_ChainBlend\n";
                stateYaml += "    flags: FLAG_DISABLE_CONDITION\n";
                {
                    std::ofstream sout(LongPath(statePath), std::ios::binary | std::ios::trunc);
                    sout.write(stateYaml.data(), static_cast<std::streamsize>(stateYaml.size()));
                }
            }
        }
    }

    // ── root state machine ──
    {
        std::string y;
        y += "class: hkbStateMachine\n";
        y += "name: FNIS_RootSM\n";
        // No neutral/entry state: FNIS.hkx is pulled into the flattened graph by 0_master's RBG, so
        // every dance state's GLOBAL wildcard is reachable from anywhere and fires its clip directly
        // (enter + select in one hop; landing on the nested dance state activates it and its chain).
        // startStateId just names the default state (0 = first dance); the entry is always a global
        // wildcard to a specific dance, so it's never actually shown. Transitions are ONLY the dance
        // wildcards — no return-to-idle (exit is the mod's / root behavior's job).
        y += "startStateId: 0\n";
        y += "transitions:\n";
        y += smWildcards;
        y += "states:\n";
        y += smStates;
        WriteFile(unitDir / "states" / "FNIS_RootSM.yaml", y, fileCount);
    }

    Say(log, "  " + containerName + ": " + std::to_string(nextStateId) + " clips, " +
        std::to_string(outEvents.size()) + " events");
}

// Emit the 0_master delta: the FNIS state (RBG → FNIS.hkx) inserted into vanilla
// Master_Behavior by the ownership inversion, with one entry wildcard per FNIS event
// planted via the transition inversion. This NEVER edits the vanilla Master_Behavior node
// (which is id-keyed — a name-keyed edit wouldn't merge and would ship broken). Everything
// is authored by NAME on BR-owned nodes; the merge resolves Master_Behavior by name,
// appends the state + its entry wildcards, and the compiler mints the ids.
void EmitMasterDelta(const fs::path&                                 deltaDir,
                     const std::string&                              fnisRef,
                     const std::vector<std::pair<std::string, int>>& events,  // (event, dance stateId)
                     const std::vector<AnimVarDecl>&                 vars,
                     std::size_t&                                    fileCount)
{
    // BehaviorReferenceGenerator → FNIS.hkx (a runtime path string; resolves at load).
    {
        std::string y;
        y += "class: hkbBehaviorReferenceGenerator\n";
        y += "name: CB_FNIS_BFR\n";
        y += "behaviorName: " + fnisRef + "\n";
        WriteFile(deltaDir / "references" / "CB_FNIS_BFR.yaml", y, fileCount);
    }

    // The FNIS state: inserted into Master_Behavior (parents:). Its ONLY job is to reference FNIS.hkx
    // via the RBG so FNIS.hkx is pulled into the flattened graph at char-setup — which puts every
    // dance state's GLOBAL wildcard into the graph's global set, reachable from anywhere. There are
    // deliberately NO entryTransitions: routing every event into this one generic state and hoping
    // the sub-behavior then selects was the bug (it can't — the entry can't carry the nested
    // selection across the RBG, and toNestedStateId crashes there). Instead a dance event trips its
    // OWN global wildcard inside FNIS_RootSM directly, landing on the specific dance state (which
    // activates this state and its chain). Enter + select in one hop, no funnel.
    {
        std::string y;
        y += "class: hkbStateMachineStateInfo\n";
        y += "name: CB_FNIS_State\n";
        y += "stateId: 30000\n";
        y += "generator: CB_FNIS_BFR\n";
        y += "parents:\n  - Master_Behavior\n";
        WriteFile(deltaDir / "states" / "CB_FNIS_State.yaml", y, fileCount);
    }

    // Additive graph data (events + variables added to 0_master's event/variable tables).
    {
        std::string y;
        if (!events.empty()) {
            y += "events:\n";
            for (const auto& [ev, nestedId] : events) {
                (void)nestedId;
                y += "  - name: " + ev + "\n    flags: \"0\"\n";
            }
        }
        if (!vars.empty()) {
            y += "variables:\n";
            for (const auto& v : vars) {
                y += "  - name: " + v.name + "\n";
                y += "    type: VARIABLE_TYPE_" + v.type + "\n";
                y += "    value: " + v.value + "\n";
            }
        }
        if (!y.empty())
            WriteFile(deltaDir / "data" / "additive.yaml", y, fileCount);
    }
}

// Emit the bundle manifest. FNIS.hky edits vanilla 0_master, so it masters Skyrim (only) —
// it needs no intermediate relay, so it does NOT depend on Community Behaviors.hky.
void EmitManifest(const fs::path& bundlePath, std::size_t& fileCount)
{
    std::string y;
    y += "{\n";
    y += "    \"name\": \"FNIS\",\n";
    y += "    \"description\": \"FNIS animations converted to a native Community Behaviors bundle.\",\n";
    y += "    \"masters\": [\"Skyrim\"]\n";
    y += "}\n";
    WriteFile(bundlePath / "manifest.json", y, fileCount);
}

}  // namespace

ConvertResult ConvertFnis(const std::vector<fs::path>&    animationsDirs,
                          const std::string&              actor,
                          const std::vector<std::string>& characterStems,
                          const fs::path&                 bundlePath,
                          const LogFn&                    log)
{
    ConvertResult result;

    // ── scan all directories, merge into one ScanResult ──
    ScanResult scan;
    scan.actor = actor;
    for (const auto& dir : animationsDirs) {
        Say(log, "FNIS converter: scanning " + dir.string());
        std::vector<std::string> parseWarnings;
        ScanResult partial = ScanForLists(dir, actor, &parseWarnings);
        for (auto& w : parseWarnings) result.warnings.push_back(std::move(w));
        for (auto& lf : partial.lists) {
            Say(log, "  " + lf.modName + ": " + std::to_string(lf.anims.size()) + " anims");
            scan.lists.push_back(std::move(lf));
        }
    }

    if (scan.lists.empty()) {
        result.ok = true;
        Say(log, "  No FNIS list files found.");
        return result;
    }
    Say(log, "  Total: " + std::to_string(scan.lists.size()) + " list file(s)");

    // ── group ──
    std::vector<AnimGroup> groups = GroupAnimations(scan);
    if (groups.empty()) {
        result.ok = true;
        Say(log, "  No animation groups after grouping.");
        return result;
    }

    // Collect variables.
    std::vector<AnimVarDecl> allVars;
    for (const auto& lf : scan.lists)
        for (const auto& v : lf.vars)
            allVars.push_back(v);

    // ── emit the single FNIS container at behaviors/community_behaviors/FNIS.hkx ──
    // Direct model: 0_master → FNIS.hkx (no intermediate relay hub). One container holds
    // every group; the old multi-container/switchboard split is retired (it was never
    // finished, and the direct model has no use for it). NOTE: character-focused — the
    // delta targets 0_master's Master_Behavior; non-character actors are not wired yet.
    const std::string behaviorBase = "meshes/actors/" + actor + "/behaviors";
    fs::path fnisUnit = bundlePath / behaviorBase / "community_behaviors" / "FNIS.hkx";
    std::vector<std::pair<std::string, int>> allEvents;   // (event, dance stateId)
    std::vector<std::string>                 allAnimNames;

    EmitContainer(fnisUnit, "FNIS", groups, allEvents, allAnimNames,
                  result.filesWritten, result.warnings, log);

    Say(log, "  " + std::to_string(groups.size()) + " group(s) → community_behaviors/FNIS.hkx (" +
        std::to_string(allEvents.size()) + " entry events)");

    // ── emit 0_master delta (FNIS state inserted into Master_Behavior + entry wildcards) ──
    fs::path deltaDir = bundlePath / behaviorBase / "0_master.hkx";
    EmitMasterDelta(deltaDir, "Behaviors\\community_behaviors\\FNIS.hkx", allEvents, allVars,
                    result.filesWritten);

    // ── emit the bundle manifest (masters Skyrim) ──
    EmitManifest(bundlePath, result.filesWritten);

    // ── emit animationnames ──
    // One roster drop per target character stem. The humanoid character is BOTH defaultmale and
    // defaultfemale projects, so the caller passes both — a female (or male) actor's bind-by-name
    // clip resolution and the derived animationdata BOTH key off these drops; a single-stem drop
    // left the other sex's roster empty (bind-by-name fell back to a foreign index in-game).
    if (!allAnimNames.empty()) {
        std::string body;
        for (const auto& an : allAnimNames)
            body += an + "\r\n";
        for (const auto& stem : characterStems)
            WriteFile(bundlePath / "animationnames" / (stem + ".txt"), body, result.filesWritten);
    }

    result.animCount  = allAnimNames.size();
    result.eventCount = allEvents.size();
    result.varCount   = allVars.size();
    result.ok = true;

    Say(log, "FNIS conversion complete: " + std::to_string(result.filesWritten) + " files, " +
        std::to_string(result.animCount) + " anims, " + std::to_string(result.eventCount) + " events");
    return result;
}

}  // namespace CommunityBehaviors::fnis
