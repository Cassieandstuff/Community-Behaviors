#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Converter.h"
#include "ui/LoadOrderUI.h"

// The whole tool UI: two persistent folder fields (Data + Zip destination), a Convert
// button that runs the conversion on a worker thread, and a live scrolling log. Convert
// always produces Community Behaviors.zip at the destination — the DynDOLOD-style model:
// convert into a private staging dir next to the exe (auto-cleared so shrinking
// conversions stay clean), zip that, and let MO2 install-as-replace do the clean
// overwrite. Templates ship beside the exe and are hardcoded. Drawn every frame.
class ConverterUI {
public:
    ConverterUI();
    ~ConverterUI();

    void Draw();

private:
    void DrawConverterTab();           // the conversion UI (the "Converter" tab body)
    void RefreshPlan();                // (re)build the pre-conversion LoadOrderPlan from m_profile
    void DrawLoadOrderArranger();      // the arranger where the log used to be (plan mods, drag-reorder)
    void DrawDebugTab();               // the "Debug" tab: plugin debug flags auto-enumerated from DebugFlags.h
    void StartConvert();
    void AppendLog(std::string line);
    void SetZipMsg(std::string msg);   // guarded — the worker also writes it
    void LoadSettings();               // <exe>/sct_converter.ini  (data + zip dirs)
    void SaveSettings() const;

    // The Community Behaviors load order + bundles the tool manages live in the MO2 VFS Data
    // folder (the SAME m_dataDir the Converter tab reads), NOT beside the exe — so the Load
    // Order tab shows the actually-installed bundles and edits the ACTIVE loadorder.txt the
    // runtime reads, and conversion seeds/preserves that same file. Derived from m_dataDir so
    // they always track the (live-editable) Data field.
    std::string BrLoadOrderPath() const;   // <Data>/community_behaviors/loadorder.txt
    std::string BrPluginsDir() const;      // <Data>/community_behaviors/plugins

    // Persistent (sct_converter.ini):
    std::string m_dataDir;   // MO2 VFS Data folder
    std::string m_zipDir;    // where the .zip lands (point at your MO2 downloads)

    // ── Redesigned Converter tab (MO2-profile-only) state ─────────────────────────────────────
    std::string          m_profile;               // MO2 profile folder (or instance root) — the ONE input
    std::string          m_outputDir;             // where the converted set is written (or zipped into)
    bool                 m_autoZip = false;        // zip the output instead of writing the tree directly
    CB::converter::LoadOrderPlan m_plan;                  // pre-conversion plan (built on profile select)
    bool                 m_planPending = false;    // rebuild the plan on the next frame (after a profile change)
    std::string          m_planMsg;               // status line under the profile field

    // Fixed, next to the exe (never shown):
    std::string m_exeDir;
    std::string m_stagingDir;    // <exe>/staging — cleared + rebuilt every convert, then zipped
    std::string m_templatesDir;  // <exe>/schema/templates

    std::thread       m_worker;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_cancel{ false };
    std::atomic<bool> m_finished{ false };
    CB::converter::Result     m_result;

    std::mutex               m_logMx;
    std::vector<std::string> m_log;   // shared with the worker (guarded by m_logMx)
    std::string              m_zipMsg;   // result of the last package step

    LoadOrderUI              m_loadOrder;   // the "Load Order" tab (manages loadorder.txt)
};
