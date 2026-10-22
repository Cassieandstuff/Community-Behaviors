/// @file
/// @brief hkbModifier's default virtuals, reimplemented against vanilla (AE 1.6.1170).
/// @note Header-inline COMDAT copies in vanilla (0x140accc20, 0x140accc30); defined here out of line.
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note The default marks the modifier (bit 1 of m_userData) as having no modify of its own, as hkbNode::update
    ///       does with bit 0.
    void hkbModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        m_userData |= 2;
    }

    void hkbModifier::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
    }
} }
