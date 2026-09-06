#pragma once

// Debug probe (opt-in via marker file): vtable-hooks BSSynchronizedClipGenerator::Activate
// and logs every paired/synchronized clip as it fires — name, sync prefix, and lead-vs-support
// side. This is the diagnostic for the paired-animation breakage (dialogue idle T-pose, horse
// mount jitter): the log names the exact synchronized clip that activates during a repro (and
// which side of the pairing it is), turning "paired animations are broken" into a specific node.
//
// OFF unless Data\community_behaviors\syncprobe.enable exists — no overhead / no log spam otherwise.

namespace CB::syncprobe {
    // Install the Activate hook. Returns false if the vtable can't be resolved. Safe to call
    // once at plugin load; the hook lives for the process lifetime.
    bool Install();
}
