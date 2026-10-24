#pragma once

#include <interface/reflection/HavokSchema.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief The motion-matching BUNDLE generator: databases of clips (dance cards) and a chooser over them -> the
///        behavior unit whose root is a CBPoseSearchGenerator over one hkbClipGenerator per clip, and the roster
///        patches naming those clips. The one implementation hky-tool's generators and external tools (via CB-API's
///        cb::mm) share.

namespace CB::engine::compile {

    /// @brief CBPoseSearchGenerator::childFlags bits.
    inline constexpr std::uint8_t kMmDisableReselection = 1;
    inline constexpr std::uint8_t kMmLooping            = 2;

    /// @brief One clip of a database: it becomes one hkbClipGenerator child.
    struct MmClip {
        std::string  path;            ///< the clip file's path in the bundle (`.hkx`, below MmBundleSpec::rosterRoot)
        std::string  animationName;   ///< its roster entry; empty = MmAnimationName(path, rosterRoot)
        std::uint8_t flags = 0;       ///< the child's childFlags byte (kMmDisableReselection | kMmLooping)
    };

    /// @brief One database: a contiguous range of the generator's children, in `clips` order.
    /// @note  A clip listed by several databases becomes one child PER database occurrence, every one naming the same
    ///        animationName (the roster holds it once).
    struct MmDatabase {
        std::string              name;   ///< byte-exact, unique: chooser rows and mods name the database by it
        std::vector<MmClip>      clips;
        /// @brief Condition expressions evaluated to a float once per search (a literal such as "-0.05" is one);
        ///        empty = 0.
        /// @note  Written as given: the plugin owns the expression language's vocabulary, so it compiles them, not this
        ///        generator.
        std::string              baseCostBias;
        std::string              continuingPoseCostBias;
        std::string              loopingCostBias;
        std::vector<std::string> tags;
    };

    /// @brief A chooser row: when `when` holds, its databases join the searched set.
    struct MmChooserRow {
        std::string              when;   ///< a condition expression, e.g. GraphVar("MM_Gait") == 2; empty always holds
        std::vector<std::string> databases;
    };

    /// @brief A graph variable: `type` a VariableType name (word types only, e.g. "VARIABLE_TYPE_BOOL"), `word` its
    ///        initial 4-byte value (a REAL's float bits).
    struct MmVariable {
        std::string  name;
        std::string  type;
        std::int32_t word = 0;
    };

    struct MmVariableDecl {
        std::string_view name;
        std::string_view type;
    };

    /// @brief The chooser inputs every motion-matching graph declares, in this order, right after the clip and time
    ///        variables. CB's controller writes them by these exact names.
    /// @note  GASP's chooser inputs. MM_Speed2D is game units/s (GASP's Speed2D is cm/s: an importer scales its bounds by
    ///        0.7); MM_TimeToLand is seconds until the predicted path lands (GASP's TrajectoryCollision.TimeToLand), 0 on
    ///        the ground.
    inline constexpr std::array<MmVariableDecl, 15> kMmChooserVariables{ {
        { "MM_MovementMode", "VARIABLE_TYPE_INT32" },
        { "MM_RotationMode", "VARIABLE_TYPE_INT32" },
        { "MM_MovementState", "VARIABLE_TYPE_INT32" },
        { "MM_Gait", "VARIABLE_TYPE_INT32" },
        { "MM_Stance", "VARIABLE_TYPE_INT32" },
        { "MM_IsStarting", "VARIABLE_TYPE_BOOL" },
        { "MM_IsPivoting", "VARIABLE_TYPE_BOOL" },
        { "MM_JustLandedLight", "VARIABLE_TYPE_BOOL" },
        { "MM_JustLandedHeavy", "VARIABLE_TYPE_BOOL" },
        { "MM_ShouldTurnInPlace", "VARIABLE_TYPE_BOOL" },
        { "MM_ShouldSpinTransition", "VARIABLE_TYPE_BOOL" },
        { "MM_JustTraversed", "VARIABLE_TYPE_BOOL" },
        { "MM_DatabaseLOD", "VARIABLE_TYPE_INT32" },
        { "MM_Speed2D", "VARIABLE_TYPE_REAL" },
        { "MM_TimeToLand", "VARIABLE_TYPE_REAL" },
    } };

    struct MmBundleSpec {
        std::string unitPath     = "meshes/actors/character/behaviors/MotionMatching.hkx";
        std::string rosterRoot   = "meshes/actors/character/";   ///< roster entries are clip paths below it
        std::string selectorName = "MM_ClipSelector";
        std::string clipVariable = "MM_Clip";   ///< INT32, bound to the generator's selectedIndex
        std::string timeVariable = "MM_Time";   ///< REAL, bound to every clip's userControlledTimeFraction
        std::vector<MmDatabase>   databases;
        std::vector<MmChooserRow> rows;
        std::vector<std::string>  fallback;     ///< searched when no row holds; empty = every database
        /// @brief {character unit path, its root container's FormID (e.g. "0001x000b")}: one roster patch each.
        std::vector<std::pair<std::string, std::string>> characters;
        std::vector<MmVariable>   extraVariables;   ///< declared after kMmChooserVariables
    };

    /// @brief path -> file text, unit-relative paths prefixed by their unit's path.
    using MmFiles = std::vector<std::pair<std::string, std::string>>;

    /// @brief The roster entry of a clip: its path below `rosterRoot`, backslashed, case kept.
    /// @return "" when `clipPath` is not below `rosterRoot` (path identity) or is not a `.hkx` file.
    std::string MmAnimationName(std::string_view clipPath, std::string_view rosterRoot);

    /// @brief The behavior unit (`unitPath/...`), exactly as CB's decompiler emits the graph: it is built as a
    ///        SchemaObject graph, serialized, decompiled, and parsed back (the parse must serialize to the same bytes).
    /// @return false with `err` set on an invalid spec or a failed stage.
    /// @note  The chooser is always present (an empty one when there are no rows and no fallback), so a mod adds rows
    ///        to it by key. Its fallback is written as given: empty stays empty.
    bool BuildMotionMatchingBehavior(const MmBundleSpec& spec, const schema::SchemaRegistry& reg, MmFiles& out,
                                     std::string& err);

    /// @brief One roster patch unit per character naming every distinct animationName once, in first-use order.
    /// @note  A roster is a union: the patch names only what it adds. Every character gets the same list, since
    ///        characters sharing a graph need identical slot sets.
    bool BuildMotionMatchingRosters(const MmBundleSpec& spec, MmFiles& out, std::string& err);

    /// @brief BuildMotionMatchingBehavior then BuildMotionMatchingRosters: the bundle's generated files, without its
    ///        manifest or clips (the caller owns those).
    bool BuildMotionMatchingBundle(const MmBundleSpec& spec, const schema::SchemaRegistry& reg, MmFiles& out,
                                   std::string& err);

}  // namespace CB::engine::compile
