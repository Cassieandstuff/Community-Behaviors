/// @file
/// @brief BSSpeedSamplerModifier: looks up the blend parameter that makes the current locomotion move at a goal speed.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @brief The game's speed-sampler database seen from the behavior runtime (RTTI `BSISpeedSamplerDB`; the game's
    ///        BSSpeedSamplerDBManager implements it over Meshes\SpeedDataSingleFile.txt and the per-project .spd files).
    /// @note Only the slots the runtime calls are declared; the method name is provisional.
    class BSISpeedSamplerDB
    {
    public:
        virtual ~BSISpeedSamplerDB();   ///< slot 0
        /// @brief The blend parameter at which `state`'s locomotion, heading `direction`, moves at `goalSpeed`.
        /// @return `goalSpeed` itself on any miss (project, state, direction or a speed past the table).
        virtual hkReal getSpeedOut(const hkbContext& context, int state, hkReal direction, hkReal goalSpeed) = 0;  ///< slot 1

        /// @brief The database the modifiers query: set (and cleared) by the game's animation-cache startup.
        /// @note Name provisional (0x1431bd160). Null until the game installs it.
        static BSISpeedSamplerDB* s_instance;
    };

    /// @note Member names are the class's reflected names. The `slot N` annotations are vanilla's vtable slots; it keeps
    ///       the base getClassType. The destructor is implicit: the scalar deleting destructor (slot 0) inlines it.
    class BSSpeedSamplerModifier : public hkbModifier
    {
    public:
        /// @brief The locomotion state the table is keyed by (the graph's iState).
        int    m_state;       ///< +0x50
        hkReal m_direction;   ///< +0x54
        hkReal m_goalSpeed;   ///< +0x58
        hkReal m_speedOut;    ///< +0x5c

        BSSpeedSamplerModifier();
        BSSpeedSamplerModifier(hkFinishLoadedObjectFlag flag);
        BSSpeedSamplerModifier(const BSSpeedSamplerModifier& mod);

        /// @brief m_speedOut = the database's answer for (m_state, m_direction, m_goalSpeed); m_goalSpeed without one.
        virtual void     update(const hkbContext& context, hkReal timestep);  ///< slot 5
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
    };
    static_assert(sizeof(BSSpeedSamplerModifier) == 0x60, "BSSpeedSamplerModifier size");
} }
