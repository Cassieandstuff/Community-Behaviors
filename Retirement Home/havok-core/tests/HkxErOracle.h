#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HkxErOracle — M2 byte-exact verification oracle.
//
// This is a PINNED, VERBATIM copy of Engine Relay's
//   src/SKSE/Engine Relay/cpp/HkxErWriter.cpp  (namespace EngineRelay::HkxErWriter)
// with its single external dependency (the `Registration` struct from
// ERInternal.h) inlined and the PCH include removed, so it compiles
// standalone with no RE::/CommonLibSSE/.NET deps.
//
// WHY: HkxErWriter is a hand-written native Havok packfile writer whose **N=0**
// output is hex-verified to be byte-identical to the real, deployed
// `enginerelay.hkx` shipped by Engine Relay. That makes it a ground-truth oracle
// for havok-core's general-purpose PackFileSerializer: if havok-core builds the
// same switchboard graph and serializes to the same bytes, havok-core emits a
// real, game-valid Havok behavior file. This is the M2 milestone exit.
//
// DO NOT "improve" or refactor this file — its value is that it is a frozen,
// independently-authored reference. If Engine Relay's HkxErWriter changes, re-pin
// from source deliberately.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hkx_oracle {

// Inlined from ERInternal.h — only the fields HkxErWriter reads.
struct Registration {
    std::string modName;
    std::string behaviorPath;
    std::string eventName;
};

// Serialize a complete switchboard HKX into memory (see HkxErWriter.h).
std::vector<std::uint8_t> WriteToMemory(
    const std::vector<Registration>& registrations,
    std::string_view                 graphName = "EngineRelay.hkb");

}  // namespace hkx_oracle
