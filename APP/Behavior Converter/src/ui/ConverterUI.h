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
// overwrite. Templates + base ship beside the exe and are hardcoded. Drawn every frame.
class ConverterUI {
public:
    ConverterUI();
    ~ConverterUI();

    void Draw();
    bool WantsQuit() const { return false; }

private:
    void DrawConverterTab();           // the conversion UI (the "Converter" tab body)
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
    std::string m_zipName;   // the .zip file name (== the MO2 mod name); ".zip" appended if missing
    std::string m_mo2Instance; // OPTIONAL MO2 instance root (mods/ + profiles/) — when set, bundles
                               // are attributed to owning mods (<modName>.hky, both loose legs grouped)
                               // and the delta load order follows modlist priority; empty = auto/off

    // Fixed, next to the exe (never shown):
    std::string m_exeDir;
    std::string m_stagingDir;    // <exe>/staging — cleared + rebuilt every convert, then zipped
    std::string m_templatesDir;  // <exe>/templates
    std::string m_baseDir;       // <exe>/base (pristine vanilla singlefiles)

    std::thread       m_worker;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_cancel{ false };
    std::atomic<bool> m_finished{ false };
    bconv::Result     m_result;

    std::mutex               m_logMx;
    std::vector<std::string> m_log;   // shared with the worker (guarded by m_logMx)
    bool                     m_autoscroll = true;
    std::string              m_zipMsg;   // result of the last package step

    LoadOrderUI              m_loadOrder;   // the "Load Order" tab (manages loadorder.txt)
};
