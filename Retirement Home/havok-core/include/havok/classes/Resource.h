#pragma once
#include "havok/classes/Base.h"   // hkReferencedObject

#include <memory>
#include <string>
#include <vector>

// Havok memory-resource classes — the resource tree a skeleton.hkx (and other content) carries
// alongside its real data. Ported byte-exact from serde-hkx layouts (the SOP reference); the first
// bricks of the skeleton/physics port that unlocks a full skeleton decompile↔compile.

namespace havok {

// hkMemoryResourceHandleExternalLink — size 16, sig 0x3144d17c. An inline array element (no base,
// no vtable): two string pointers. Serialized as {memberName@0, externalId@8}.
class hkMemoryResourceHandleExternalLink : public IHavokObject {
public:
    std::string m_memberName;
    std::string m_externalId;
    HK_CLASS_ID(0x3144d17cu, "hkMemoryResourceHandleExternalLink")
};

// hkMemoryResourceHandle — size 48, sig 0xbffac086. Parent hkResourceHandle (→ hkResourceBase →
// hkReferencedObject) adds no serialized members — m_variant starts at offset 16, right after the
// 16-byte hkReferencedObject base — so we extend hkReferencedObject directly (byte-identical).
// Members: variant pointer (usually null) @16, name string @24, external-link array @32.
class hkMemoryResourceHandle : public hkReferencedObject {
public:
    std::shared_ptr<hkReferencedObject>             m_variant;
    std::string                                     m_name;
    std::vector<hkMemoryResourceHandleExternalLink> m_references;
    HK_CLASS_ID(0xbffac086u, "hkMemoryResourceHandle")
};

// hkMemoryResourceContainer — size 64, sig 0x4762f92a. Base hkResourceContainer (→ hkResourceBase →
// hkReferencedObject) adds no serialized members (m_name @16 is right after the referenced-object
// base). The resource tree node: a name, a SERIALIZE_IGNORED back-pointer to its parent (empty on
// disk, rebuilt at load — not stored), an array of resource handles, and a recursive array of child
// containers.
class hkMemoryResourceContainer : public hkReferencedObject {
public:
    std::string                                            m_name;
    std::vector<std::shared_ptr<hkMemoryResourceHandle>>   m_resourceHandles;
    std::vector<std::shared_ptr<hkMemoryResourceContainer>> m_children;
    HK_CLASS_ID(0x4762f92au, "hkMemoryResourceContainer")
};

} // namespace havok
