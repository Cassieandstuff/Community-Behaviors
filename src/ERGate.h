#pragma once

// ER wildcard gate — the compile-time primitive that lets Engine Relay pin an actor in a reference
// behavior. BR injects a BOOL graph variable (kGateVar) into every graph it compiles and gates every
// GLOBAL wildcard transition (hkbStateMachine::m_wildcardTransitions, in every state machine, every
// graph — actors AND objects) on `!kGateVar`. LOCAL wildcards (FLAG_IS_LOCAL_WILDCARD=2048) are
// deliberately NOT gated: they are the escape hatch a framework-owned graph uses to keep a locked
// actor steerable (an ER hkx navigates via local wildcards while all globals are frozen). So:
//
//   kGateVar == false (default)  ->  every wildcard fires exactly as vanilla (feature is inert).
//   kGateVar == true             ->  no wildcard can fire -> the actor cannot be pulled out of its
//                                    current state by ANY global (combat entries, stagger, movement,
//                                    even death) -> total control.
//
// CONTRACT with Engine Relay (the runtime owner of this bit):
//   • ER flips it per-actor: SetGraphVariableBool(kGateVar, true) to lock, false to release.
//   • It is a single boolean by design. ER does ALL category/priority arbitration between mods on its
//     own side (gameplay priority, cutscenes/OStim queued, etc.) and presents the graph one bit.
//   • ER OWNS THE LIFECYCLE. Because death is gated too, a locked actor cannot die until ER releases
//     — a missed unlock is a soft-lock. That is the accepted trade for total control; ER must always
//     release (e.g. on scene end / state teardown).
//
// The name must match on both sides; ER references this same string.

namespace CB::ergate {

    inline constexpr const char* kGateVar = "BR_ERWildcardLock";

}  // namespace CB::ergate
