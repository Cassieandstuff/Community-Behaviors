#include "PCH.h"

/// @file
/// @brief Compiles the whole function palette into the plugin, so a palette that has drifted from schema/Havok (a
///        renamed class, a changed signature) fails this build rather than the first feature that reaches for it.
/// @note Regenerate with tools/cbmatch --emit-palette common/feature-sdk/havok (together with --emit-image, so the
///       palette lists exactly the functions the deployed image installs).

#include <feature-sdk/havok/Palette.h>
