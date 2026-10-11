#pragma once
#include <string>
#include <vector>

// The "Load Order" tab of the SCT Behavior Converter.
//
// Manages the installed Community Behaviors .hky load order. Reads each bundle's
// manifest.json (name / version / author / masters — the plugin's BundleManifest schema,
// src/core/discover/BundleManifest.h) plus the active loadorder.txt, lists the bundles in
// priority order (top = base = loads first; bottom = highest priority = wins, mirroring MO2's
// left pane), supports drag-drop reordering, and writes the order back to loadorder.txt.
//
// It also surfaces the offline mirror of the plugin's manifest validation: a declared
// master that isn't installed, and a bundle ordered before a master it depends on.
//
// Deliberately no nlohmann/json: the converter is kept dependency-lean, so the flat manifest
// is read with a small targeted string scan. The plugin reads the same files with a real JSON parser.
class LoadOrderUI {
public:
    // defaultPluginsDir seeds the first scan (<Data>/community_behaviors/plugins); loadOrderPath is
    // the active <Data>/community_behaviors/loadorder.txt. Both are owned by ConverterUI and passed in
    // each frame; the user can Browse to a different plugins folder after that.
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
