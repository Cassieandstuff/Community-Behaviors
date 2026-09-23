#pragma once
// Forwarding shim — HkTypes (the portable Havok math value types: Vector4/Quaternion/QSTransform)
// was promoted to the base value-type tier at <common/HkTypes.h> (org-pass). This shim keeps the old
// <havok/core/HkTypes.h> path compiling for the quarantined havok-core typed classes (havok/classes/**,
// slated for deletion) and framing siblings, so their includes don't churn. New code includes
// <common/HkTypes.h> directly. Remove this shim when havok-core is cut.
#include <common/HkTypes.h>
