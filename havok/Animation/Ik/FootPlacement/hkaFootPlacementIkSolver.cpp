/// @file
/// @brief hkaFootPlacementIkSolver, reimplemented against vanilla (AE 1.6.1170): the functions matched so far.
#include <Animation/Ik/FootPlacement/hkaFootPlacementIkSolver.h>

namespace CB { namespace havok
{
    hkaFootPlacementIkSolver::hkaFootPlacementIkSolver(const Setup& setup)
    :   m_setup(setup)
    {
        m_currentWeight                = 0.0f;
        m_previousGroundHeightWS       = 0.0f;
        m_previousGroundNormalWS       = setup.m_worldUpDirectionWS;
        m_previousVerticalDisplacement = 0.0f;
        m_isFootLocked                 = false;
        m_footLockedPositionMS.setZero4();
        m_footLockedGroundNormalMS.setZero4();
        m_footUnlockedPositionMS.setZero4();
    }
} }
