/// @file
/// @brief BSEventOnFalseToTrueModifier: raises up to three events, each when its bound bool turns true.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note Member names are the class's reflected names; the m_bSlotNActivatedLastFrame flags are reflected too,
    ///       flagged serialize-ignored. Those three are plain `bool`, not hkBool: update stores an `a && b` into them
    ///       through a 32-bit 1/0 (`mov eax,1`), where an hkBool takes a byte (`mov al,1`). The destructor is implicit: vanilla's stamps no vtable; the deleting destructor
    ///       (slot 0) calls it out of line.
    class BSEventOnFalseToTrueModifier : public hkbModifier
    {
    public:
        hkBool           m_bEnableEvent1;              ///< +0x50
        hkBool           m_bVariableToTest1;           ///< +0x51
        hkbEventProperty m_EventToSend1;               ///< +0x58
        hkBool           m_bEnableEvent2;              ///< +0x68
        hkBool           m_bVariableToTest2;           ///< +0x69
        hkbEventProperty m_EventToSend2;               ///< +0x70
        hkBool           m_bEnableEvent3;              ///< +0x80
        hkBool           m_bVariableToTest3;           ///< +0x81
        hkbEventProperty m_EventToSend3;               ///< +0x88
        bool             m_bSlot1ActivatedLastFrame;   ///< +0x98
        bool             m_bSlot2ActivatedLastFrame;   ///< +0x99
        bool             m_bSlot3ActivatedLastFrame;   ///< +0x9a

        BSEventOnFalseToTrueModifier();
        /// @note Called by the class's packfile finish function (in the type-registration block).
        BSEventOnFalseToTrueModifier(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: the settings, with the last-frame flags cleared.
        BSEventOnFalseToTrueModifier(const BSEventOnFalseToTrueModifier& mod);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                ///< slot 1
        /// @brief Raises each enabled slot's event on the frame its variable turns true.
        virtual void        update(const hkbContext& context, hkReal timestep);  ///< slot 5
        /// @note Returns whether `errorString` is empty, so an earlier message also fails it.
        virtual bool        isValid(hkStringPtr& errorString) const;             ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
    };
    static_assert(sizeof(BSEventOnFalseToTrueModifier) == 0xa0, "BSEventOnFalseToTrueModifier size");
} }
