#pragma once

/// @file
/// @brief The reader tripwire ([Animation64] bReaderWatch, off by default): hardware watchpoints on the 16-bit binding
///        index of the clips most recently activated, logging every instruction that reads or writes it.
/// @note  Up to four at once (the debug registers), re-armed on every thread twice a second, rotating to the newest
///        clips. Each instruction address is logged once with its module: the exe (code not yet converted), no module
///        (the takeover image, CB's matched copy of vanilla), or the plugin (converted code, counted, not logged).
///        A data watchpoint traps after the access, so the address logged is the next instruction's.
/// @warning A debug-only probe: it overwrites every thread's debug registers (so it fights a debugger's hardware
///          breakpoints), and a clip freed while watched can report an access to whatever reuses its memory.

namespace CB::feature::widen {

    /// @brief Offers a clip just activated to the watch; starts the watch on the first call when the flag is on.
    void OfferToWatch(const void* clip);

}  // namespace CB::feature::widen
