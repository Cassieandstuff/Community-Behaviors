/// @file
/// @brief BSLimbIKModifier: Bethesda's limb-against-geometry IK — casts along a bone chain and swings the chain away from
///        what it hits.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @brief One BSLimbIKModifier modify's cast session against the world, opened by BSILimbIKModifierSingleton.
    /// @note The exe's class (RTTI); the game's is BSLimbIKModifierUtilityCastInfo. The modifier only deletes it.
    class BSLimbIKModifierCastInfo
    {
    public:
        virtual ~BSLimbIKModifierCastInfo();   ///< slot 0
    };

    /// @brief The game-side caster a BSLimbIKModifier probes the world with (the exe's BSILimbIKModifierSingleton, implemented
    ///        by BSLimbIKModifierUtility over the character's bhkWorld).
    /// @note Method and parameter names provisional; positions and directions are in the game's world units.
    class BSILimbIKModifierSingleton
    {
    public:
        virtual ~BSILimbIKModifierSingleton();   ///< slot 0
        /// @brief Game units per Havok unit (the bones' radius and cast offset are given in Havok units).
        virtual hkReal getWorldScale() = 0;   ///< slot 1
        /// @brief Opens a cast session over the box `aabbMin`..`aabbMax` around the character's world.
        /// @post `castInfoOut` is always set (the caller deletes it), even when this returns false.
        virtual hkBool startCast(BSLimbIKModifierCastInfo*& castInfoOut, const hkbContext& context, const hkVector4& aabbMin,
                                 const hkVector4& aabbMax) = 0;   ///< slot 2
        /// @brief Casts a ray `from`..`to`; on a hit, the hit's fraction along it and the surface normal.
        virtual hkBool castRay(BSLimbIKModifierCastInfo* castInfo, const hkVector4& from, const hkVector4& to, hkReal& hitFractionOut,
                               hkVector4& hitNormalOut) = 0;   ///< slot 3

        /// @note Name provisional (0x1431bd158, the BSTSingletonSDM<BSLimbIKModifierUtility> the game's animation-cache
        ///       startup installs). Null until then.
        static BSILimbIKModifierSingleton* s_instance;
    };

    /// @note Member names are the class's reflected names (schema/gen/BSLimbIKModifier.yaml, 0x78 bytes). The `slot N`
    ///       annotations are vanilla's vtable slots. The destructor is implicit.
    class BSLimbIKModifier : public hkbModifier
    {
    public:
        /// @brief The most the chain is swung (degrees).
        hkReal  m_limitAngleDegrees;   ///< +0x50
        /// @brief The angle the chain is currently swung by (radians), eased toward the target by m_gain.
        hkReal  m_currentAngle;        ///< +0x54
        hkInt16 m_startBoneIndex;      ///< +0x58
        hkInt16 m_endBoneIndex;        ///< +0x5a
        hkReal  m_gain;                ///< +0x5c
        hkReal  m_boneRadius;          ///< +0x60
        hkReal  m_castOffset;          ///< +0x64
        hkReal  m_timeStep;            ///< +0x68, the last update's
        /// @note Reflected as an untyped pointer; the constructors null it, nothing in the class reads it.
        void*   m_pSkeletonMemory;     ///< +0x70

        BSLimbIKModifier();
        /// @note Called by the class's packfile finish function (in the type-registration block).
        BSLimbIKModifier(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: everything but m_pSkeletonMemory.
        BSLimbIKModifier(const BSLimbIKModifier& mod);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                         ///< slot 1
        virtual void        activate(const hkbContext& context);                          ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);           ///< slot 5
        virtual void        deactivate(const hkbContext& context);                        ///< slot 7
        virtual bool        isValid(hkStringPtr& errorString) const;                      ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12
        virtual void        modify(const hkbContext& context, hkbGeneratorOutput& inOut); ///< slot 23
    };
    static_assert(sizeof(BSLimbIKModifier) == 0x78, "BSLimbIKModifier size");
} }
