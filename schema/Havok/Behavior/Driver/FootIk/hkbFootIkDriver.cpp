/// @file
/// @brief hkbFootIkDriver, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>

#include <Animation/Ik/FootPlacement/hkaFootPlacementIkSolver.h>

namespace CB { namespace schema
{
    hkbFootIkDriver::hkbFootIkDriver()
    :   m_vec20(hkVector4::getZero()),
        m_rotation30(hkQuaternion::getIdentity()),
        m_worldFromModelFeedback(0.0f),
        m_real44(0.0f),
        m_isSetUp(false),
        m_isGroundPositionValid(false),
        m_bool4A(false),
        m_real4C(0.0f)
    {
    }

    hkbFootIkDriver::~hkbFootIkDriver()
    {
        for (int i = 0; i < m_internalLegData.getSize(); i++) {
            delete m_internalLegData[i].m_footIkSolver;
        }
    }

    void hkbFootIkDriver::reset()
    {
        for (int i = 0; i < m_internalLegData.getSize(); i++) {
            if (m_internalLegData[i].m_footIkSolver) {
                delete m_internalLegData[i].m_footIkSolver;
                m_internalLegData[i].m_footIkSolver = HK_NULL;
                m_internalLegData[i].m_groundPosition.setZero4();
                m_internalLegData[i].m_verticalError = 0.0f;
                m_internalLegData[i].m_hitSomething  = false;
                m_internalLegData[i].m_isPlantedMS   = false;
            }
        }
        m_worldFromModelFeedback = 0.0f;
        m_real44                 = 0.0f;
        m_isSetUp                = false;
        m_isGroundPositionValid  = false;
        m_real4C                 = 0.0f;
    }
} }
