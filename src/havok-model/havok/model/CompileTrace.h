#pragma once
// CompileTrace — a name-annotated, greppable trace of the behavior compiler's activity.
//
// The compiler resolves symbolic names (variable / event / node / animation) to indices and then
// DISCARDS the names (Stage-4 clears a binding's `variable` once `variableIndex` is set), so a dump
// taken after compile is just integers. This taps the compile MID-FLIGHT, while the names are still in
// memory, and emits one line per event so a bug of the recurring "a reference resolved to the wrong
// slot" class becomes `grep`-able instead of an in-game repro.
//
// ZERO COST WHEN OFF: the sink is null by default; `Enabled()` is a single pointer test. Callers guard
// any non-trivial formatting with `if (trace::Enabled())`. Behavior compile runs at startup on every
// user's machine and in the offline regen, so this must add nothing unless explicitly switched on
// (runtime: a DebugFlag routes it to the SKSE log; offline: the CLI routes it to a file/stdout).
//
// Record format (stable, greppable):
//   [CC] <phase> <unit> <class>#<name> <field> <action> <detail>
// e.g.
//   [CC] merge 1hm_behavior hkbStateMachineStateInfo#attackState transitions compose 3-changers
//   [CC] bind  1hm_behavior BSIsActiveModifier#BFCO_IsBlockingModf bIsActive0 parsed var='BFCO_IsBlocking' idx=109
//   [CC] vars  1hm_behavior - - - table idx=109 name='BFCO_IsBlocking'
//   [CC] sym   1hm_behavior - - variableID resolve 'BFCO_IsBlocking' -> 109
// Any field that doesn't apply is emitted as "-".

#include <functional>
#include <string>
#include <string_view>

namespace havok::model::trace {

using Sink = std::function<void(std::string_view line)>;

// Install (or clear, with {}) the line sink. Not synchronized against concurrent compiles.
void SetSink(Sink sink);

// True when a sink is installed. Guard heavy formatting with this (it's a plain pointer test).
bool Enabled() noexcept;

// Emit one already-formatted line (no-op when disabled). Prefer Rec() for the standard shape.
void Line(std::string_view line);

// Build + emit a standard record. No-op (and no formatting) when disabled. Empty args render as "-".
void Rec(std::string_view phase, std::string_view unit, std::string_view cls,
         std::string_view name, std::string_view field, std::string_view action,
         std::string_view detail = {});

// Optional source-side filter: when set (non-empty), a record is emitted only if its unit, class, or
// name CONTAINS `substr` (case-sensitive). Keeps the full multi-thousand-node compile greppable. Clear
// with {}. Applies to Rec(); Line() is always emitted (caller-formatted).
void SetFilter(std::string substr);

} // namespace havok::model::trace

namespace havok::model {

struct BehaviorData;   // fwd

// Walk a merged BehaviorData and emit the two records the reference-integrity hunt needs, with the
// symbolic NAMES still present (this runs on the merged model, pre-compile, before Stage-4 clears them):
//   * the variable table   ("vars" phase): idx=<i> name='<var>'   (graphData->variables, in order)
//   * every node binding    ("bind" phase): <class>#<node> <memberPath> var='<name>' idx=<variableIndex>
// So `grep BFCO_IsBlocking` shows both the table slot the name occupies AND the index each binding to it
// actually carries — a mismatch is the bug. No-op when trace is disabled. `unit` labels the records.
void TraceGraph(const BehaviorData& bd, std::string_view unit);

} // namespace havok::model
