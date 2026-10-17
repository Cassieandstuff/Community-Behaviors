/// @file
/// @brief bhkCharacterController and bhkCharacterControllerCinfo (their destructors), reimplemented against vanilla
///        (AE 1.6.1170).
/// @note Bethesda's engine code: built with C++ exceptions on and /Ob2 (each destructor is inlined into its deleting
///       one), which the manifest sets on this unit.
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>

namespace CB { namespace schema
{
    bhkCharacterControllerCinfo::~bhkCharacterControllerCinfo()
    {
        m_stateManager = HK_NULL;
        for (int i = 0; i < 2; ++i) m_shapes[i] = HK_NULL;
    }

    bhkCharacterController::~bhkCharacterController()
    {
        MemoryContextScope context(0x36);
        ClearDamageImpacts();
        m_bumpedBody                = HK_NULL;
        m_bumpedCharCollisionObject = HK_NULL;
        m_supportBody               = HK_NULL;
        if (m_orientationCtrl->IsOwnedByController()) delete m_orientationCtrl;
    }
} }
