/// @file
/// @brief hkbAttributeModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Attribute/hkbAttributeModifierUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Utils/hkbSymbolIdMap.h>

namespace CB { namespace schema
{
    namespace
    {
        HK_FORCE_INLINE int convertId(const hkbSymbolIdMap* fromMap, const hkbSymbolIdMap* toMap, int id)
        {
            if (fromMap != toMap) {
                if (fromMap) id = fromMap->getExternal(id);
                if (id != -1 && toMap) id = toMap->getInternal(id);
            }
            return id;
        }
    }

    void hkbAttributeModifierUtils::modify(const hkbAttributeModifier& mod, hkbGeneratorOutput& output, const hkbSymbolIdMap* fromMap, const hkbSymbolIdMap* toMap)
    {
        if (output.isValid(hkbGeneratorOutput::TRACK_ATTRIBUTES)) {
            hkbGeneratorOutput::TrackHeader& header     = output.getTrackHeader(hkbGeneratorOutput::TRACK_ATTRIBUTES);
            hkReal*                          attributes = static_cast<hkReal*>(output.getTrackData(hkbGeneratorOutput::TRACK_ATTRIBUTES));

            for (int i = 0; i < mod.m_assignments.getSize(); i++) {
                const hkbAttributeModifier::Assignment& a = mod.m_assignments[i];
                int id = a.m_attributeIndex;
                if (id != -1) {
                    id = convertId(fromMap, toMap, id);
                    if (id >= 0 && id < header.m_numData) attributes[id] = a.m_attributeValue;
                }
            }
        }
    }
} }
