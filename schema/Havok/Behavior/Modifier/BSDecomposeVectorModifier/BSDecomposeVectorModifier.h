/// @file
/// @brief BSDecomposeVectorModifier: splits a vector into its four components (each bindable on its own).
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @note Member names are the class's reflected names. The `slot N` annotations are vanilla's vtable slots; it keeps
    ///       the base getClassType. The destructor is implicit: the scalar deleting destructor (slot 0) inlines it.
    class BSDecomposeVectorModifier : public hkbModifier
    {
    public:
        hkVector4 m_vector;   ///< +0x50
        hkReal    m_x;        ///< +0x60
        hkReal    m_y;        ///< +0x64
        hkReal    m_z;        ///< +0x68
        hkReal    m_w;        ///< +0x6c

        BSDecomposeVectorModifier();
        BSDecomposeVectorModifier(hkFinishLoadedObjectFlag flag);
        BSDecomposeVectorModifier(const BSDecomposeVectorModifier& mod);

        /// @brief Copies the vector's components into m_x..m_w.
        virtual void     update(const hkbContext& context, hkReal timestep);  ///< slot 5
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
    };
    static_assert(sizeof(BSDecomposeVectorModifier) == 0x70, "BSDecomposeVectorModifier size");
} }
