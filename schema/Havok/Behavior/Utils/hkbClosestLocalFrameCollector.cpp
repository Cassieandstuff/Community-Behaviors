/// @file
/// @brief hkbClosestLocalFrameCollector, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Utils/hkbClosestLocalFrameCollector.h>

namespace CB { namespace schema
{
    hkbClosestLocalFrameCollector::hkbClosestLocalFrameCollector(const char* localFrameName)
    :   m_closestDistance(HK_REAL_MAX),
        m_minDistance(0.0f),
        m_localFrameName(localFrameName),
        m_localFrameGroup(HK_NULL)
    {
        m_closestLocalFrame.clear();
        m_handle.clear();
    }

    /// @note A name pattern may lead or end with a single `*` wildcard; an empty name or a lone `*` matches every frame.
    void hkbClosestLocalFrameCollector::addFrame(const hkLocalFrame* localFrame, hkReal distance)
    {
        const char* name = localFrame->getName();
        if (m_localFrameName && m_localFrameName[0] != '\0' && (m_localFrameName[0] != '*' || m_localFrameName[1] != '\0')) {
            if (!name) return;
            const int patternLength = hkString::strLen(m_localFrameName);
            int       cmp;
            if (m_localFrameName[0] == '*' || m_localFrameName[patternLength - 1] == '*') {
                const int nameLength = hkString::strLen(name);
                if (nameLength < patternLength - 1) return;
                if (m_localFrameName[0] == '*') {
                    cmp = hkString::strCmp(name + (nameLength - patternLength + 1), m_localFrameName + 1);
                }
                else {
                    cmp = hkString::strNcmp(name, m_localFrameName, patternLength - 1);
                }
            }
            else {
                cmp = hkString::strCmp(m_localFrameName, name);
            }
            if (cmp != 0) return;
        }
        if (m_localFrameGroup && m_localFrameGroup != localFrame->getGroup()) return;
        if (distance < m_closestDistance && distance >= m_minDistance) {
            m_closestLocalFrame.set(m_handle);
            m_closestDistance           = distance;
            m_closestLocalFrame.m_frame = const_cast<hkLocalFrame*>(localFrame);
        }
    }

    void hkbClosestLocalFrameCollector::setRigidBody(hkpRigidBody* rigidBody)
    {
        m_handle.clear();
        m_handle.m_rigidBody = rigidBody;
    }

    void hkbClosestLocalFrameCollector::setCharacterAndBone(hkbCharacter* character, hkInt16 animationBoneIndex)
    {
        m_handle.clear();
        m_handle.m_character          = character;
        m_handle.m_animationBoneIndex = animationBoneIndex;
    }

    void hkbClosestLocalFrameCollector::setMinDistance(hkReal minDistance)
    {
        m_minDistance = minDistance;
    }

    void hkbClosestLocalFrameCollector::setLocalFrameGroup(const hkLocalFrameGroup* group)
    {
        m_localFrameGroup = group;
    }
} }
