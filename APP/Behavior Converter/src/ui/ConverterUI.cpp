#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "ui/ConverterUI.h"

#include "core/debug/DebugFlags.h"   // CB::debug::kFlags — the canonical plugin debug-flag registry (shared, dependency-free)

#include <sct-utilities/SctUtilities.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace {

std::string ExeDir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return fs::path(std::wstring(buf, n)).parent_path().string();
}

void OpenInExplorer(const std::string& path) {
    if (path.empty()) return;
    ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Bind a std::string to an ImGui InputText via a scratch buffer.
bool InputPath(const char* id, std::string& s) {
    char buf[2048];
    std::snprintf(buf, sizeof buf, "%s", s.c_str());
    if (ImGui::InputText(id, buf, sizeof buf)) { s = buf; return true; }
    return false;
}

std::string Trim(std::string s) {
    const auto notws = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notws));
    s.erase(std::find_if(s.rbegin(), s.rend(), notws).base(), s.end());
    return s;
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// ── Minimal INI bool get/upsert for the plugin's settings.ini ────────────────────────────────
// The Debug tab reads/writes the SAME file the plugin's CSimpleIniA readers use ([section] key=bool).
// A targeted hand-roll (not a full INI lib) so the converter adds no new dependency; it preserves
// every other line and only touches the one key.
bool IniGetBool(const fs::path& ini, const std::string& section, const std::string& key, bool def) {
    std::ifstream f(ini);
    if (!f) return def;
    std::string line, cur;
    while (std::getline(f, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') { cur = t.substr(1, t.size() - 2); continue; }
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        if (IEquals(cur, section) && IEquals(Trim(t.substr(0, eq)), key)) {
            std::string v = Trim(t.substr(eq + 1));
            for (auto& c : v) c = (char)std::tolower((unsigned char)c);
            return v == "1" || v == "true" || v == "yes" || v == "on";
        }
    }
    return def;
}

void IniSetBool(const fs::path& ini, const std::string& section, const std::string& key, bool val) {
    std::vector<std::string> lines;
    { std::ifstream f(ini); std::string l; while (std::getline(f, l)) lines.push_back(l); }
    const std::string valStr = val ? "true" : "false";

    int secStart = -1, secEnd = (int)lines.size();
    for (int i = 0; i < (int)lines.size(); ++i) {
        std::string t = Trim(lines[i]);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            if (secStart >= 0) { secEnd = i; break; }                    // first header AFTER our section
            if (IEquals(t.substr(1, t.size() - 2), section)) secStart = i;
        }
    }
    if (secStart < 0) {                                                   // section absent — append it
        if (!lines.empty() && !Trim(lines.back()).empty()) lines.push_back("");
        lines.push_back("[" + section + "]");
        lines.push_back(key + "=" + valStr);
    } else {
        int keyLine = -1;
        for (int i = secStart + 1; i < secEnd; ++i) {
            std::string t = Trim(lines[i]);
            const auto eq = t.find('=');
            if (eq != std::string::npos && IEquals(Trim(t.substr(0, eq)), key)) { keyLine = i; break; }
        }
        if (keyLine >= 0) lines[keyLine] = key + "=" + valStr;
        else              lines.insert(lines.begin() + secEnd, key + "=" + valStr);
    }
    std::error_code ec;
    fs::create_directories(ini.parent_path(), ec);
    std::ofstream o(ini, std::ios::trunc);
    for (const auto& l : lines) o << l << "\n";
}

}  // namespace

ConverterUI::ConverterUI() {
    // MO2 launches the tool from the game dir, so the VFS Data folder is ./Data. Templates,
    // base, the private staging dir, and the settings ini live beside the exe. The load order
    // the tool manages is NOT beside the exe — it is the ACTIVE one in the Data VFS
    // (<Data>/community_behaviors/loadorder.txt), derived from m_dataDir (see BrLoadOrderPath). The
    // zip destination defaults to Downloads; persisted settings override the Data + zip fields.
    std::error_code ec;
    m_exeDir        = ExeDir();
    const fs::path exe = m_exeDir;
    m_dataDir       = (fs::current_path(ec) / "Data").string();
    m_zipDir        = sct::util::DownloadsFolder();   // point at your MO2 downloads folder
    m_zipName       = "Output_Community Behaviors";        // the .zip / MO2 mod name (editable)
    m_templatesDir  = (exe / "templates").string();
    m_baseDir       = (exe / "base").string();
    m_stagingDir    = (exe / "staging").string();
    LoadSettings();   // override m_dataDir / m_zipDir from sct_converter.ini if present
}

std::string ConverterUI::BrLoadOrderPath() const {
    return (fs::path(m_dataDir) / "community_behaviors" / "loadorder.txt").string();
}

std::string ConverterUI::BrPluginsDir() const {
    return (fs::path(m_dataDir) / "community_behaviors" / "plugins").string();
}

void ConverterUI::LoadSettings() {
    std::ifstream f(fs::path(m_exeDir) / "sct_converter.ini");
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string val = Trim(line.substr(eq + 1));
        if (key == "data" && !val.empty()) m_dataDir = val;
        else if (key == "zip" && !val.empty()) m_zipDir = val;
        else if (key == "name" && !val.empty()) m_zipName = val;
        else if (key == "mo2") m_mo2Instance = val;   // may be empty (optional)
    }
}

void ConverterUI::SaveSettings() const {
    std::ofstream f(fs::path(m_exeDir) / "sct_converter.ini", std::ios::trunc);
    if (!f) return;
    f << "# SCT Behavior Converter settings\n";
    f << "data=" << m_dataDir  << "\n";
    f << "zip="  << m_zipDir   << "\n";
    f << "name=" << m_zipName  << "\n";
    f << "mo2="  << m_mo2Instance << "\n";
}

ConverterUI::~ConverterUI() {
    m_cancel = true;
    if (m_worker.joinable()) m_worker.join();
    if (m_diffWorker.joinable()) m_diffWorker.join();   // the Diff tab's own worker
    SaveSettings();   // persist Data + zip dirs even if the user set them and closed without converting
}

void ConverterUI::AppendLog(std::string line) {
    std::lock_guard<std::mutex> lk(m_logMx);
    m_log.push_back(std::move(line));
}

void ConverterUI::SetZipMsg(std::string msg) {
    std::lock_guard<std::mutex> lk(m_logMx);   // the worker writes this too; Draw reads under the same lock
    m_zipMsg = std::move(msg);
}

void ConverterUI::StartConvert() {
    if (m_running) return;
    if (m_worker.joinable()) m_worker.join();
    { std::lock_guard<std::mutex> lk(m_logMx); m_log.clear(); m_zipMsg.clear(); }
    SaveSettings();
    m_cancel = false; m_running = true; m_finished = false;
    const bconv::Options opt{ m_dataDir, m_templatesDir, m_baseDir, m_stagingDir, Trim(m_mo2Instance) };
    // Snapshot everything the worker touches so it never races the UI fields.
    const std::string staging = m_stagingDir;
    const std::string zipDir  = m_zipDir;
    const std::string loPath  = BrLoadOrderPath();   // the ACTIVE loadorder.txt in the Data VFS
    // Resolve the zip file name here (base name -> "<name>.zip"): empty falls back to the
    // default; a user-typed ".zip" isn't doubled (case-insensitive check).
    std::string zipFile = Trim(m_zipName);
    if (zipFile.empty()) zipFile = "Output_Community Behaviors";
    {
        std::string ext = zipFile.size() >= 4 ? zipFile.substr(zipFile.size() - 4) : "";
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".zip") zipFile += ".zip";
    }
    m_worker = std::thread([this, opt, staging, zipDir, loPath, zipFile]() {
        std::error_code ec;

        // 1) Wipe staging so a shrinking conversion (a removed mod) can't leave a stale
        //    bundle in the zip. Safe: staging is ours, next to the exe.
        fs::remove_all(staging, ec);
        const fs::path brDir = fs::path(staging) / "community_behaviors";
        fs::create_directories(brDir, ec);

        // 2) Seed the canonical loadorder.txt (kept next to the exe, so it survives the wipe)
        //    into staging so ConvertLoadOrder's preserve-and-append logic finds the user's
        //    tuned order. Without this, every convert would revert to fresh alphabetical —
        //    the exact order-clobber that broke combat before.
        if (fs::exists(loPath, ec))
            fs::copy_file(loPath, brDir / "loadorder.txt",
                          fs::copy_options::overwrite_existing, ec);

        m_result = bconv::ConvertLoadOrder(
            opt, [this](std::string s) { AppendLog(std::move(s)); }, m_cancel);

        if (m_result.ok) {
            // 3) Persist the (preserved + appended) loadorder back to the ACTIVE Data-VFS copy
            //    so the Load Order tab and the next convert see the same order (under MO2 this
            //    write lands in overwrite, which shadows the installed mod's copy — same bytes).
            //    Create community_behaviors/ first in case no BR mod is installed there yet.
            std::error_code ce;
            fs::create_directories(fs::path(loPath).parent_path(), ce);
            fs::copy_file(brDir / "loadorder.txt", loPath,
                          fs::copy_options::overwrite_existing, ce);

            // 4) Always package: zip staging -> <zipDir>/<name>.zip (beside staging, never
            //    inside it). Install it in MO2 as a replace. Full path logged.
            if (zipDir.empty()) {
                SetZipMsg("No zip destination set.");
                AppendLog("Package FAILED: no zip destination set.");
            } else {
                const std::string zip = (fs::path(zipDir) / zipFile).string();
                std::string err;
                const bool ok = sct::util::ZipDir(staging, zip, err);
                SetZipMsg(ok ? ("Packaged -> " + zip) : ("Package failed: " + err));
                AppendLog(ok ? ("Packaged -> " + zip) : ("Package FAILED: " + err));
            }
        }
        m_running  = false;
        m_finished = true;
    });
}

void ConverterUI::Draw() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("SCT Behavior Converter", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::BeginTabBar("main", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Converter")) {
            DrawConverterTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Load Order")) {
            // Manage the ACTIVE load order in the MO2 VFS Data folder — the bundles actually
            // installed under <Data>/community_behaviors/plugins and the loadorder.txt the runtime
            // reads — not a tool-private staging copy. Conversion seeds/preserves the same file
            // (see StartConvert), so the tab and the convert step agree.
            m_loadOrder.Draw(BrPluginsDir(), BrLoadOrderPath());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diff")) {
            DrawDiffTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug")) {
            DrawDebugTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void ConverterUI::DrawConverterTab() {
    ImGui::TextUnformatted(
        "Convert a Nemesis/Pandora behavior load order into a Community Behaviors .zip.");
    ImGui::TextDisabled("Run through MO2 so the Data folder is the merged VFS view. "
                        "Install the .zip in MO2 (as a replace when re-running).");
    ImGui::Separator();

    const bool busy = m_running.load();

    ImGui::BeginDisabled(busy);
    ImGui::PushItemWidth(-260.0f);
    InputPath("##data", m_dataDir);
    ImGui::SameLine();
    if (ImGui::Button("Browse##data")) {
        std::string p;
        if (sct::ui::PickFolder("Select the game Data folder (MO2 VFS)", m_dataDir.c_str(), p)) m_dataDir = p;
    }
    ImGui::SameLine(); ImGui::TextUnformatted("Data folder");

    InputPath("##zip", m_zipDir);
    ImGui::SameLine();
    if (ImGui::Button("Browse##zip")) {
        std::string p;
        if (sct::ui::PickFolder("Select where the .zip should land (your MO2 downloads folder)",
                                m_zipDir.c_str(), p)) m_zipDir = p;
    }
    ImGui::SameLine(); ImGui::TextUnformatted("Zip destination");

    InputPath("##zipname", m_zipName);
    ImGui::SameLine(); ImGui::TextDisabled(".zip");
    ImGui::SameLine(); ImGui::TextUnformatted("Zip name (= MO2 mod name)");

    InputPath("##mo2", m_mo2Instance);
    ImGui::SameLine();
    if (ImGui::Button("Browse##mo2")) {
        std::string p;
        if (sct::ui::PickFolder("Select your MO2 instance root (contains mods\\ and profiles\\)",
                                m_mo2Instance.c_str(), p)) m_mo2Instance = p;
    }
    ImGui::SameLine(); ImGui::TextUnformatted("MO2 instance (optional)");
    if (m_mo2Instance.empty()) {
        ImGui::TextDisabled("    Optional: point at your MO2 instance to name bundles after the owning mods");
        ImGui::TextDisabled("    and pull in loose precompiled behaviors (e.g. HorsePower's horse graph). Blank = off.");
    }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (!busy) {
        if (ImGui::Button("Convert", ImVec2(150, 34))) StartConvert();
    } else {
        if (ImGui::Button("Cancel", ImVec2(150, 34))) m_cancel = true;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Converting...");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Open zip folder", ImVec2(160, 34))) OpenInExplorer(m_zipDir);
    ImGui::EndDisabled();

    {
        std::lock_guard<std::mutex> lk(m_logMx);   // m_zipMsg is written by the worker too
        if (!m_zipMsg.empty()) ImGui::TextDisabled("%s", m_zipMsg.c_str());
    }

    if (m_finished.load()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (m_result.ok)
            ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.55f, 1.0f),
                               "OK — %d base graphs, %d mod bundle(s), %d deltas, "
                               "%d set-data, %d anim-data, %d/2 base%s",
                               m_result.graphs, m_result.mods, m_result.deltas,
                               m_result.setMods, m_result.animMods, m_result.baseFiles,
                               m_result.skipped ? " (some skipped)" : "");
        else
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1.0f), "FAILED: %s", m_result.error.c_str());
    }

    ImGui::Separator();
    ImGui::Checkbox("Auto-scroll", &m_autoscroll);
    ImGui::SameLine();
    ImGui::TextDisabled("Community Behaviors.zip: community_behaviors/plugins/Skyrim.hky (master base) + <Mod>.hky per mod (behavior+setdata+animdata) + loadorder.txt");

    ImGui::BeginChild("log", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lk(m_logMx);
        for (const auto& line : m_log) ImGui::TextUnformatted(line.c_str());
    }
    if (m_autoscroll && busy && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 12.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// The domain choices for the Diff tab combo. Index 0 ("auto") lets RunTreeDiff detect from A.
static const char* kDiffDomains[] = { "auto", "behavior", "setdata", "animdata", "skeleton", "character" };

void ConverterUI::StartDiff() {
    if (m_diffRunning) return;
    if (m_diffWorker.joinable()) m_diffWorker.join();
    { std::lock_guard<std::mutex> lk(m_diffLogMx); m_diffLog.clear(); }
    m_diffRunning = true; m_diffFinished = false;

    // Snapshot the UI fields into locals so the worker never races them.
    havok::diff::TreeDiffOptions opts;
    opts.a        = Trim(m_diffA);
    opts.b        = Trim(m_diffB);
    opts.deltaDir = Trim(m_diffOutDir);
    opts.domain   = kDiffDomains[(m_diffDomainIdx >= 0 && m_diffDomainIdx < (int)std::size(kDiffDomains))
                                 ? m_diffDomainIdx : 0];
    opts.skeleton = Trim(m_diffSkeleton);

    m_diffWorker = std::thread([this, opts]() {
        auto logLambda = [this](const std::string& line) {
            std::lock_guard<std::mutex> lk(m_diffLogMx);
            m_diffLog.push_back(line);
        };
        m_diffOutcome = havok::diff::RunTreeDiff(opts, logLambda);
        m_diffRunning  = false;
        m_diffFinished = true;
    });
}

void ConverterUI::DrawDiffTab() {
    ImGui::TextUnformatted(
        "Record-keyed semantic diff of two Havok artifacts (immune to node reordering / id reassignment).");
    ImGui::TextDisabled("Both sides are decompiled from binary and matched by their Class:name editorID, "
                        "then diffed to the field. A delta-only folder + summary.yaml is written.");
    ImGui::Separator();

    const bool busy = m_diffRunning.load();

    ImGui::BeginDisabled(busy);
    ImGui::PushItemWidth(-260.0f);

    InputPath("##diffA", m_diffA);
    ImGui::SameLine();
    if (ImGui::Button("Browse##diffA")) {
        std::string p;
        if (sct::ui::PickFile("Select input A (.hkx or singlefile .txt)", m_diffA.c_str(),
                              "Havok/Text", "*.hkx;*.txt", p)) m_diffA = p;
    }
    ImGui::SameLine(); ImGui::TextUnformatted("Input A");

    InputPath("##diffB", m_diffB);
    ImGui::SameLine();
    if (ImGui::Button("Browse##diffB")) {
        std::string p;
        if (sct::ui::PickFile("Select input B (.hkx or singlefile .txt)", m_diffB.c_str(),
                              "Havok/Text", "*.hkx;*.txt", p)) m_diffB = p;
    }
    ImGui::SameLine(); ImGui::TextUnformatted("Input B");

    InputPath("##diffOut", m_diffOutDir);
    ImGui::SameLine();
    if (ImGui::Button("Browse##diffOut")) {
        std::string p;
        if (sct::ui::PickFolder("Select the delta-folder destination", m_diffOutDir.c_str(), p))
            m_diffOutDir = p;
    }
    ImGui::SameLine(); ImGui::TextUnformatted("Output delta folder");

    InputPath("##diffSkel", m_diffSkeleton);
    ImGui::SameLine();
    if (ImGui::Button("Browse##diffSkel")) {
        std::string p;
        if (sct::ui::PickFile("Select an optional skeleton (.hkx or bones .txt)", m_diffSkeleton.c_str(),
                              "Skeleton/Bones", "*.hkx;*.txt", p)) m_diffSkeleton = p;
    }
    ImGui::SameLine(); ImGui::TextUnformatted("Skeleton (optional)");

    ImGui::Combo("Domain", &m_diffDomainIdx, kDiffDomains, (int)std::size(kDiffDomains));
    ImGui::PopItemWidth();
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (!busy) {
        ImGui::BeginDisabled(Trim(m_diffA).empty() || Trim(m_diffB).empty() || Trim(m_diffOutDir).empty());
        if (ImGui::Button("Run", ImVec2(150, 34))) StartDiff();
        ImGui::EndDisabled();
    } else {
        // No cooperative cancel in RunTreeDiff; the button is a disabled placeholder while busy.
        ImGui::BeginDisabled(true);
        ImGui::Button("Running...", ImVec2(150, 34));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Diffing...");
    }

    if (m_diffFinished.load()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (!m_diffOutcome.ok)
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1.0f), "ERROR: %s", m_diffOutcome.error.c_str());
        else if (m_diffOutcome.anyDifference)
            ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f),
                               "DIFFERENCES — compared %d, only-A %d, only-B %d, differing %d",
                               m_diffOutcome.comparedRecords, m_diffOutcome.onlyA,
                               m_diffOutcome.onlyB, m_diffOutcome.differing);
        else
            ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.55f, 1.0f),
                               "identical — compared %d records, no semantic difference",
                               m_diffOutcome.comparedRecords);
    }
    if (m_diffFinished.load() && m_diffOutcome.ok && !m_diffOutcome.summaryPath.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Open delta folder")) OpenInExplorer(m_diffOutDir);
    }

    ImGui::Separator();
    ImGui::Checkbox("Auto-scroll##diff", &m_diffAutoscroll);

    ImGui::BeginChild("difflog", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lk(m_diffLogMx);
        for (const auto& line : m_diffLog) ImGui::TextUnformatted(line.c_str());
    }
    if (m_diffAutoscroll && busy && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 12.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void ConverterUI::DrawDebugTab() {
    ImGui::TextUnformatted("Runtime debug + diagnostic toggles the Community Behaviors plugin reads at launch.");
    ImGui::TextDisabled("Backed by <Data>/SKSE/Plugins/Community Behaviors/settings.ini and marker files under "
                        "<Data>/community_behaviors/. Changes apply on the NEXT game launch.");
    ImGui::Separator();

    if (m_dataDir.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1.0f), "Set the Data folder on the Converter tab first.");
        return;
    }

    const fs::path dataDir   = m_dataDir;
    const fs::path iniPath   = dataDir / "SKSE" / "Plugins" / "Community Behaviors" / "settings.ini";
    const fs::path markerDir = dataDir / "community_behaviors";

    ImGui::TextDisabled("ini: %s", iniPath.string().c_str());
    ImGui::Spacing();

    // Auto-enumerated from the canonical registry (DebugFlags.h) — add a row there and it shows up here.
    for (const auto& fl : CB::debug::kFlags) {
        const std::string section(fl.section), key(fl.key), id(fl.id);
        const bool        marker = fl.kind == CB::debug::FlagKind::MarkerFile;

        const bool on  = marker ? fs::exists(markerDir / key) : IniGetBool(iniPath, section, key, fl.defOn);
        bool       cur = on;
        const std::string cbLabel = std::string(fl.label) + "##" + id;
        if (ImGui::Checkbox(cbLabel.c_str(), &cur) && cur != on) {
            if (marker) {
                std::error_code ec;
                if (cur) { fs::create_directories(markerDir, ec); std::ofstream(markerDir / key); }
                else       fs::remove(markerDir / key, ec);
            } else {
                IniSetBool(iniPath, section, key, cur);
            }
        }
        if (!fl.help.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", std::string(fl.help).c_str());
        ImGui::SameLine();
        if (marker) ImGui::TextDisabled("[marker: %s]", key.c_str());
        else        ImGui::TextDisabled("[%s / %s%s]", section.c_str(), key.c_str(), fl.defOn ? ", default on" : "");
    }

    ImGui::Separator();
    if (ImGui::Button("Open settings.ini folder")) OpenInExplorer(iniPath.parent_path().string());
    ImGui::SameLine();
    if (ImGui::Button("Open marker folder"))       OpenInExplorer(markerDir.string());
    ImGui::SameLine();
    ImGui::TextDisabled("Writes take effect on relaunch — the running game is unaffected.");
}
