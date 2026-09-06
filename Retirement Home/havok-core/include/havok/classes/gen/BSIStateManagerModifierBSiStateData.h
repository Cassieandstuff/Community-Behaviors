#pragma once
#include "havok/classes/IHavokObject.h"   // fwd: PackFileSerializer/Deserializer, BinaryReader/WriterEx

#include <cstdint>
#include <memory>

// One entry of BSIStateManagerModifier::m_stateData: a state machine plus the
// (StateID -> iStateToSetAs) mapping. Inline array value type (never serialized by
// name — it has no packfile __classnames__ entry), so it is a plain struct with
// Read/Write rather than a registered IHavokObject. size 16 (ptr 8 + int 4 + int 4).

namespace havok {

class hkbStateMachine;

struct BSIStateManagerModifierBSiStateData {
    std::shared_ptr<hkbStateMachine> m_pStateMachine;
    std::int32_t                     m_StateID       = 0;
    std::int32_t                     m_iStateToSetAs = 0;

    void Read(PackFileDeserializer& des, BinaryReaderEx& br);
    void Write(PackFileSerializer& s, BinaryWriterEx& bw) const;
};

} // namespace havok
