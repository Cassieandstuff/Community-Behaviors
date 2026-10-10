#pragma once
// Ambient shared vocabulary for the CB Havok stack (force-included per TU via the lib PCHs +
// the plugin PCH). The stack was authored against a single flat namespace, so the data-model /
// primitive types (CB::core::common) and the serialization/codec types (CB::core::codec) are
// referenced UNqualified throughout. Forward-declare both namespaces so `using namespace` below
// can't depend on include order (no "namespace does not exist"), then make their names ambient;
// the real definitions arrive through each file's normal includes. Definitions still live in their
// purpose namespaces — this only restores unqualified VISIBILITY of the shared vocabulary.
namespace CB { namespace core { namespace common {} namespace codec {} } }
using namespace CB::core::common;
using namespace CB::core::codec;
