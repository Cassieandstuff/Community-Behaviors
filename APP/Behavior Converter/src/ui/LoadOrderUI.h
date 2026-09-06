#pragma once
#include <string>
#include <vector>

// The "Load Order" tab of the SCT Behavior Converter.
//
// Manages the Community Behaviors .hky load order the converter bakes into its output zip.
// Reads each bundle's manifest.json (name / version / author / masters — the
// BundleManifest schema authored in the BR plugin, src/SKSE/Community Behaviors/hpp/
// BundleManifest.h) plus the canonical loadorder.txt, lists the bundles in priority
// order (top = base = loads first; bottom = highest priority = wins, mirroring MO2's
// left pane), supports drag-drop reordering, and writes the order back to loadorder.txt.
//
// It also surfaces the offline mirror of BR's runtime manifest validation: a declared
// master that isn't installed, and a bundle ordered before a master it depends on.
// (Node-level override/clash detection — the deep "does B overwrite A" — is a later pass.)
//
// Deliberately no nlohmann/json: the converter is kept dependency-lean (see its
// CMakeLists), so the flat manifest is read with a small targeted string scan. BR itself
// reads the same files with a real JSON parser.
class LoadOrderUI {
public:
    // defaultPluginsDir seeds the first scan (the converter's staging output,
    // <exe>/staging/community_behaviors/plugins); loadOrderPath is the canonical
    // <exe>/loadorder.txt. Both are owned by ConverterUI and passed in each frame; the
    // user can Browse to a different plugins folder (e.g. an installed mod) after that.
    void Draw(const std::string& defaultPluginsDir, const std::string& loadOrderPath);

private:
    struct Bundle {
        std::string              stem;              // folder name without .hky — the loadorder handle
        std::string              name;              // manifest name (defaults to stem)
        std::string              version;
        std::string              author;
        std::vector<std::string> masters;           // declared master stems (lowercased)
        bool                     present     = true; // false = in loadorder.txt but no folder on disk
        bool                     hasManifest = false;
    };

    void Rescan(const std::string& pluginsDir, const std::string& loadOrderPath);
    bool Save(const std::string& loadOrderPath);    // rewrite loadorder.txt in current order
    void Revalidate();                              // recompute m_warnings from m_bundles order

    std::string              m_pluginsDir;          // resolved scan dir (Browse can move it)
    bool                     m_loaded    = false;   // first-frame auto-scan done
    std::vector<Bundle>      m_bundles;             // in current (editable) order
    std::vector<std::string> m_warnings;            // master-dependency notices
    std::string              m_status;
    int                      m_dragged   = -1;      // row being dragged (-1 = none)
};
