#pragma once
#include "havok/classes/Base.h"
#include "havok/core/HkTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Havok behavior *project* classes — the tiny graph a `*project.hkx` file carries:
//   hkRootLevelContainer -> hkbProjectData -> hkbProjectStringData
// Hand-ported (layouts confirmed against serde-hkx AND byte-exact vanilla
// round-trip). A project is NOT a behavior graph: it never goes through
// BehaviorBuilder; it round-trips straight through PackFileDeserializer /
// PackFileSerializer.

namespace havok {

// hkbProjectStringData — sig 0x076ad60a. x86_64 in-memory size 120.
// The four hkArray<hkStringPtr> tables + four hkStringPtr paths, then rootPath
// (SERIALIZE_IGNORED — occupies its 8-byte pointer slot, written null, never a
// string fixup). characterFilenames is the field BR rewrites/synthesizes.
class hkbProjectStringData : public hkReferencedObject {
public:
    std::vector<std::string> m_animationFilenames;
    std::vector<std::string> m_behaviorFilenames;
    std::vector<std::string> m_characterFilenames;
    std::vector<std::string> m_eventNames;
    std::string              m_animationPath;
    std::string              m_behaviorPath;
    std::string              m_characterPath;
    std::string              m_fullPathToSource;
    std::string              m_rootPath;  // SERIALIZE_IGNORED (null pointer slot)
    HK_CLASS_ID(0x076ad60au, "hkbProjectStringData")
};

// hkbProjectData — sig 0x13a39ba7. x86_64 size 48.
//   worldUpWS        Vector4  @16
//   stringData       ref      @32  -> hkbProjectStringData
//   defaultEventMode enum i8  @40  (hkbTransitionEffect::EventMode; 2 =
//                                    EVENT_MODE_IGNORE_FROM_GENERATOR), pad to 48
class hkbProjectData : public hkReferencedObject {
public:
    Vector4                                m_worldUpWS{ 0.f, 0.f, 1.f, 0.f };
    std::shared_ptr<hkbProjectStringData>  m_stringData;
    std::int8_t                            m_defaultEventMode = 0;
    HK_CLASS_ID(0x13a39ba7u, "hkbProjectData")
};

} // namespace havok
