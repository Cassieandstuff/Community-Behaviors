#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "ui/ConverterUI.h"

#include "core/debug/DebugFlags.h"   // CB::core::debug::kFlags — the canonical plugin debug-flag registry (shared, dependency-free)

#include <sct-utilities/SctUtilities.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

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
    LoadPandoraOrderFile();   // <exe>/pandora_order.txt -> m_pandoraModOrder (Pandora Order tab override)
    if (m_outputDir.empty()) m_outputDir = m_zipDir;   // sensible default: the downloads/mods folder
    m_planPending = true;   // build the plan on the first frame if a profile was restored from settings
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
        else if (key == "pandora") m_singleBundle = (val == "1" || val == "true" || val == "yes" || val == "on");
        else if (key == "profile" && !val.empty()) m_profile = val;
        else if (key == "output"  && !val.empty()) m_outputDir = val;
        else if (key == "autozip") m_autoZip = (val == "1" || val == "true" || val == "yes" || val == "on");
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
    f << "pandora=" << (m_singleBundle ? "true" : "false") << "\n";
    f << "profile=" << m_profile   << "\n";
    f << "output="  << m_outputDir << "\n";
    f << "autozip=" << (m_autoZip ? "true" : "false") << "\n";
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

    // MO2-profile only: derive the instance root from the profile (a profile dir carries modlist.txt;
    // otherwise the field already IS an instance root). dataDir = the game Data from the profile ini.
    const std::string prof = Trim(m_profile);
    std::string instance = prof;
    { std::error_code ec;
      if (fs::is_regular_file(fs::path(prof) / "modlist.txt", ec))
          instance = fs::path(prof).parent_path().parent_path().string(); }

    const bool        autoZip = m_autoZip;
    const std::string outDir  = Trim(m_outputDir);
    const std::string convOut = autoZip ? m_stagingDir : outDir;      // the converter writes its tree here
    // dataDir = where the base Skyrim.hky + existing bundles are read. Prefer the VFS ./Data (has the full
    // mod overlay) when the tool is launched THROUGH MO2 and it actually contains the base; otherwise use
    // the mod folder the plan resolved as shipping the base (standalone run).
    std::string dataDir = m_plan.ok ? m_plan.gameDataDir : std::string();
    { std::error_code ec;
      if (fs::is_regular_file(fs::path(m_dataDir) / "community_behaviors" / "plugins" / "Skyrim.hky", ec))
          dataDir = m_dataDir; }

    // per-mod bundles (singleBundle=false) — the mockup's "N behavior bundles" model.
    bconv::Options opt{ dataDir, m_templatesDir, m_baseDir, convOut, instance, /*singleBundle*/ false };

    const std::string zipName = "Output_Community Behaviors.zip";

    m_worker = std::thread([this, opt, autoZip, outDir, convOut, zipName]() {
        std::error_code ec;
        if (autoZip) fs::remove_all(convOut, ec);                     // clean staging for a fresh zip
        fs::create_directories(fs::path(convOut) / "community_behaviors", ec);

        m_result = bconv::ConvertLoadOrder(
            opt, [this](std::string s) { AppendLog(std::move(s)); }, m_cancel);

        if (m_result.ok && autoZip) {
            if (outDir.empty()) { SetZipMsg("No output folder set."); AppendLog("Package FAILED: no output folder."); }
            else {
                const std::string zip = (fs::path(outDir) / zipName).string();
                std::string err;
                const bool ok = sct::util::ZipDir(convOut, zip, err);
                SetZipMsg(ok ? ("Packaged -> " + zip) : ("Package failed: " + err));
                AppendLog(ok ? ("Packaged -> " + zip) : ("Package FAILED: " + err));
            }
        } else if (m_result.ok) {
            SetZipMsg("Wrote converted set -> " + convOut);
            AppendLog("Wrote converted set -> " + convOut);
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
        if (ImGui::BeginTabItem("Pandora Order")) {
            DrawPandoraTab();
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

void ConverterUI::RefreshPlan() {
    m_planMsg.clear();
    const std::string prof = Trim(m_profile);
    if (prof.empty()) { m_plan = {}; m_planMsg = "Select an MO2 profile (or instance root) above."; return; }
    m_plan = bconv::BuildLoadOrderPlan(prof, [](std::string) {});
    if (!m_plan.ok) { m_planMsg = "Plan: " + m_plan.error; return; }
    int withCodes = 0; for (const auto& m : m_plan.mods) if (!m.codes.empty()) ++withCodes;
    m_planMsg = "auto-detected — " + std::to_string(m_plan.mods.size()) + " behavior mod(s), " +
                std::to_string(withCodes) + " with codes · master tables resolved (self=0, Skyrim=1)";
}

void ConverterUI::DrawLoadOrderArranger() {
    if (!m_plan.ok) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextDisabled("Select an MO2 profile above to catalogue its behavior mods and resolve master tables.");
        return;
    }
    ImGui::TextDisabled("Load order — drag to reorder · priority 1 = winner · masters resolved from shared mod-codes");
    const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("loarr", 4, tf)) {
        ImGui::TableSetupColumn("#",       ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("Mod");
        ImGui::TableSetupColumn("Codes",   ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Masters", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(m_plan.mods.size()); ++i) {
            auto& m = m_plan.mods[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string rid = std::to_string(i + 1) + "##row" + std::to_string(i);
            ImGui::Selectable(rid.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                ImGui::SetDragDropPayload("CB_LOROW", &i, sizeof(int));
                ImGui::TextUnformatted(m.modName.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("CB_LOROW")) {
                    const int src = *static_cast<const int*>(pl->Data);
                    if (src >= 0 && src < static_cast<int>(m_plan.mods.size()) && src != i) {
                        bconv::PlanMod moved = m_plan.mods[src];
                        m_plan.mods.erase(m_plan.mods.begin() + src);
                        m_plan.mods.insert(m_plan.mods.begin() + i, std::move(moved));
                        for (int k = 0; k < static_cast<int>(m_plan.mods.size()); ++k) m_plan.mods[k].priority = k + 1;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(m.modName.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string codes; for (std::size_t c = 0; c < m.codes.size(); ++c) { if (c) codes += "  "; codes += m.codes[c]; }
            ImGui::TextColored(ImVec4(0.34f, 0.78f, 0.83f, 1.0f), "%s", codes.c_str());
            ImGui::TableSetColumnIndex(3);
            // masters[0] = Skyrim (index 1); [1+] = cross-bundle (index 2+). self is implicit index 0.
            std::string mstr = "self";
            for (std::size_t k = 0; k < m.masters.size(); ++k) { mstr += " · "; mstr += m.masters[k]; }
            ImGui::TextDisabled("%s", mstr.c_str());
        }
        ImGui::EndTable();
    }
}

void ConverterUI::DrawConverterTab() {
    const bool busy = m_running.load();

    // (Re)build the plan after the profile field settles (not on every keystroke).
    if (m_planPending && !busy && !ImGui::IsAnyItemActive()) { m_planPending = false; RefreshPlan(); }

    // ── MO2 Profile ──────────────────────────────────────────────────────────────────────────
    ImGui::TextDisabled("MO2 PROFILE");
    ImGui::BeginDisabled(busy);
    ImGui::PushItemWidth(-120.0f);
    if (InputPath("##profile", m_profile)) m_planPending = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Change…", ImVec2(110, 0))) {
        std::string p;
        if (sct::ui::PickFolder("Select your MO2 profile folder (contains modlist.txt) or instance root",
                                m_profile.c_str(), p)) { m_profile = p; RefreshPlan(); }
    }
    ImGui::EndDisabled();
    if (!m_planMsg.empty())
        ImGui::TextColored(m_plan.ok ? ImVec4(0.45f, 0.82f, 0.5f, 1.0f) : ImVec4(0.95f, 0.8f, 0.45f, 1.0f),
                           "%s", m_planMsg.c_str());
    if (m_plan.ok) {
        ImGui::TextDisabled("mods: %s", m_plan.modsDir.c_str());
        if (!m_plan.gameDataDir.empty()) ImGui::TextDisabled("game data: %s", m_plan.gameDataDir.c_str());
    }
    ImGui::Separator();

    // ── main area: the load-order arranger (idle) or the conversion log (busy/finished) ────────
    const float footer = ImGui::GetFrameHeightWithSpacing() * 4.4f + 22.0f;
    float mainH = ImGui::GetContentRegionAvail().y - footer;
    if (mainH < 80.0f) mainH = 80.0f;
    ImGui::BeginChild("mainArea", ImVec2(0, mainH), false);
    if (busy || m_finished.load()) {
        std::lock_guard<std::mutex> lk(m_logMx);
        for (const auto& line : m_log) ImGui::TextUnformatted(line.c_str());
        if (m_autoscroll && busy && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 12.0f) ImGui::SetScrollHereY(1.0f);
    } else {
        DrawLoadOrderArranger();
    }
    ImGui::EndChild();

    // ── footer: output folder + toggles + convert (bottom) ─────────────────────────────────────
    ImGui::Separator();
    ImGui::TextDisabled("OUTPUT FOLDER");
    ImGui::BeginDisabled(busy);
    ImGui::PushItemWidth(-120.0f);
    InputPath("##out", m_outputDir);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Browse…##out", ImVec2(110, 0))) {
        std::string p;
        if (sct::ui::PickFolder("Select the output folder (your MO2 mods folder, as a new mod)",
                                m_outputDir.c_str(), p)) m_outputDir = p;
    }
    ImGui::Checkbox("Export loadorder.txt with conversion", &m_exportLoadOrder);
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::Checkbox("Zip output automatically", &m_autoZip);
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (!busy) {
        const bool ready = m_plan.ok && !Trim(m_outputDir).empty();
        ImGui::BeginDisabled(!ready);
        const char* label = !m_plan.ok ? "Convert  (select an MO2 profile first)"
                          : Trim(m_outputDir).empty() ? "Convert  (choose an output folder)"
                          : "Convert load order  →  .hky bundles";
        if (ImGui::Button(label, ImVec2(-1, 40))) StartConvert();
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Cancel", ImVec2(-1, 40))) m_cancel = true;
    }
    if (m_finished.load()) {
        if (m_result.ok)
            ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.55f, 1.0f), "OK — %d mod bundle(s), %d deltas, %d set-data%s",
                               m_result.mods, m_result.deltas, m_result.setMods, m_result.skipped ? " (some skipped)" : "");
        else
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1.0f), "FAILED: %s", m_result.error.c_str());
    }
    {
        std::lock_guard<std::mutex> lk(m_logMx);
        if (!m_zipMsg.empty()) ImGui::TextDisabled("%s", m_zipMsg.c_str());
    }
}

// ── Pandora Order tab ─────────────────────────────────────────────────────────────────────────

void ConverterUI::LoadPandoraOrderFile() {
    m_pandoraModOrder.clear();
    std::ifstream f(fs::path(m_exeDir) / "pandora_order.txt");
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        const std::string t = Trim(line);
        if (!t.empty() && t[0] != '#') m_pandoraModOrder.push_back(t);
    }
}

void ConverterUI::SavePandoraOrderFile() {
    std::ofstream f(fs::path(m_exeDir) / "pandora_order.txt", std::ios::trunc);
    if (!f) { m_pandoraMsg = "Save failed: cannot write pandora_order.txt"; return; }
    f << "# Pandora Order — TOP = winner (Pandora priority 1). Edited in the Behavior Converter;\n";
    f << "# overrides Pandora's ActiveMods.json on the next MO2-profile convert. Delete to reset.\n";
    for (const auto& m : m_pandoraModOrder) f << m << "\n";
    m_pandoraMsg = "Saved " + std::to_string(m_pandoraModOrder.size()) + " mod(s) to pandora_order.txt";
}

void ConverterUI::RefreshPandora() {
    const bconv::PandoraAnalysis a = bconv::AnalyzePandoraOrder(m_dataDir, Trim(m_mo2Instance));
    m_pandoraFromPandora    = a.fromPandora;
    m_pandoraConflictGraphs = (int)a.conflicts.size();
    m_pandoraLoaded         = true;

    std::map<std::string, std::vector<std::string>> codeConf;   // code -> ["graph (win X)", …]
    std::set<std::string>                           conflictCodes;
    for (const auto& c : a.conflicts)
        for (const auto& cd : c.codes) {
            conflictCodes.insert(cd);
            codeConf[cd].push_back(c.graph + " (win " + c.winner + ")");
        }

    // Group codes by owning mod, in display order (first occurrence fixes the mod's position).
    std::vector<PandoraModRow> rows;
    std::map<std::string, int> rowIndex;
    for (const auto& ci : a.codes) {
        const std::string mod = ci.owningMod.empty() ? ci.code : ci.owningMod;
        auto it = rowIndex.find(mod);
        if (it == rowIndex.end()) { rowIndex[mod] = (int)rows.size(); rows.push_back(PandoraModRow{ mod, {}, false, {} }); }
        PandoraModRow& row = rows[rowIndex[mod]];
        row.codes.push_back(ci.code);
        if (conflictCodes.count(ci.code)) row.conflict = true;
    }
    for (auto& r : rows) {
        std::set<std::string> g;
        for (const auto& c : r.codes) for (const auto& gc : codeConf[c]) g.insert(gc);
        std::string tip;
        for (const auto& s : g) { if (!tip.empty()) tip += "\n"; tip += s; }
        r.tip = tip;
    }

    // Apply the persisted override: mods in saved order first (stable), unknown mods keep display order.
    if (!m_pandoraModOrder.empty()) {
        std::map<std::string, int> want;
        for (int i = 0; i < (int)m_pandoraModOrder.size(); ++i) want[m_pandoraModOrder[i]] = i;
        std::stable_sort(rows.begin(), rows.end(), [&](const PandoraModRow& x, const PandoraModRow& y) {
            const int rx = want.count(x.mod) ? want[x.mod] : 1000000;
            const int ry = want.count(y.mod) ? want[y.mod] : 1000000;
            return rx < ry;
        });
    }
    m_pandoraRows = std::move(rows);
    m_pandoraMsg  = (m_pandoraFromPandora ? "Order seeded from Pandora ActiveMods.json"
                                          : "No ActiveMods.json — scan order") +
                    std::string("; ") + std::to_string(m_pandoraRows.size()) + " mod(s), " +
                    std::to_string(m_pandoraConflictGraphs) + " conflicting graph(s).";
}

void ConverterUI::DrawPandoraTab() {
    ImGui::TextUnformatted("Behavior load order for the merged Pandora.hky (MO2-profile mode).");
    ImGui::TextDisabled("Seeded from Pandora's own order (Pandora_Engine/ActiveMods.json). Reorder mods "
                        "(TOP = winner); Save persists it and overrides Pandora on the next convert.");
    ImGui::Separator();

    if (!m_pandoraLoaded) RefreshPandora();

    if (ImGui::Button("Refresh")) RefreshPandora();
    ImGui::SameLine();
    if (ImGui::Button("Save order")) {
        m_pandoraModOrder.clear();
        for (const auto& r : m_pandoraRows) m_pandoraModOrder.push_back(r.mod);
        SavePandoraOrderFile();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Pandora")) {
        m_pandoraModOrder.clear();
        SavePandoraOrderFile();
        RefreshPandora();
        m_pandoraMsg = "Reset to Pandora's order.";
    }
    if (!m_pandoraMsg.empty()) ImGui::TextDisabled("%s", m_pandoraMsg.c_str());
    if (!m_singleBundle)
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f),
                           "Applies only in MO2-profile mode (enable it on the Converter tab).");

    ImGui::Separator();
    ImGui::TextDisabled("TOP = highest priority (wins conflicts). (!) = touches a graph another mod also edits.");

    ImGui::BeginChild("pandora_list", ImVec2(0, 0), true);
    int swapA = -1, swapB = -1;   // apply after the loop so we don't mutate mid-iteration
    for (int i = 0; i < (int)m_pandoraRows.size(); ++i) {
        PandoraModRow& r = m_pandoraRows[i];
        ImGui::PushID(i);
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("up", ImGuiDir_Up)) { swapA = i; swapB = i - 1; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i == (int)m_pandoraRows.size() - 1);
        if (ImGui::ArrowButton("down", ImGuiDir_Down)) { swapA = i; swapB = i + 1; }
        ImGui::EndDisabled();
        ImGui::SameLine();

        const std::string label = std::to_string(i + 1) + ". " + r.mod + "  (" +
                                  std::to_string(r.codes.size()) + (r.codes.size() == 1 ? " code" : " codes") +
                                  ")###" + r.mod;
        const bool open = ImGui::TreeNode(label.c_str());
        if (r.conflict) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "(!)");
            if (ImGui::IsItemHovered() && !r.tip.empty()) ImGui::SetTooltip("%s", r.tip.c_str());
        }
        if (open) {
            for (const auto& c : r.codes) ImGui::BulletText("%s", c.c_str());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (swapA >= 0 && swapB >= 0 && swapB < (int)m_pandoraRows.size())
        std::swap(m_pandoraRows[swapA], m_pandoraRows[swapB]);
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
    for (const auto& fl : CB::core::debug::kFlags) {
        const std::string section(fl.section), key(fl.key), id(fl.id);
        const bool        marker = fl.kind == CB::core::debug::FlagKind::MarkerFile;

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
