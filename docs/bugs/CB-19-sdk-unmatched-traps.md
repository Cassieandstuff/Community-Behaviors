# CB-19: six SDK functions trap if called

**Status:** OPEN
**First seen:** 2026-11-01   **Area:** runtime (SDK export)

## Symptom
`src/platform/hooks/takeover/UnboundTraps.cpp` still traps six functions with no body and no exe binding: 2 `blend`, 2 `handleEndian`, `sampleDynamicScalarsInternal`, `hkbBoneWeightArray::getClassType`. None has fired in play so far.

## Root cause
The export has neither a shipped body nor a pin for them.

## Ruled out
Nothing yet.

## Disposition
Pin each to the exe in the SDK and re-export.
