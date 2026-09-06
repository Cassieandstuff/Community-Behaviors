#include "ui/LoadOrderUI.h"

#include <sct-utilities/SctUtilities.h>
#include <havok/model/yaml/HkyArchive.h>   // read manifest.json out of a PACKED (.hky file) bundle

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Targeted reader for the flat manifest.json — NOT a general JSON parser. Finds "key",
// then the next ':', then the next "..." string (honoring \" escapes). Safe for the
// converter's own emitted manifests and simple hand-authored ones; returns "" if absent.
std::string JsonStr(const std::string& text, const char* key) {
    const std::string tok = std::string("\"") + key + "\"";
    auto p = text.find(tok);
    if (p == std::string::npos) return {};
    p = text.find(':', p + tok.size());
    if (p == std::string::npos) return {};
    const auto q = text.find('"', p + 1);
    if (q == std::string::npos) return {};
    std::string out;
    for (auto i = q + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size()) { out += text[i + 1]; ++i; continue; }
        if (c == '"') break;
        out += c;
    }
    return out;
}

// Targeted reader for a JSON string-array (e.g. "masters": ["Skyrim", "BFCO"]).
std::vector<std::string> JsonStrArray(const std::string& text, const char* key) {
    std::vector<std::string> out;
    const std::string tok = std::string("\"") + key + "\"";
    auto p = text.find(tok);
    if (p == std::string::npos) return out;
    p = text.find('[', p + tok.size());
    if (p == std::string::npos) return out;
    auto end = text.find(']', p + 1);
    if (end == std::string::npos) end = text.size();
    for (auto i = p + 1; i < end;) {
        const auto q = text.find('"', i);
        if (q == std::string::npos || q >= end) break;
        std::string s;
        auto j = q + 1;
        for (; j < text.size(); ++j) {
            const char c = text[j];
            if (c == '\\' && j + 1 < text.size()) { s += text[j + 1]; ++j; continue; }
            if (c == '"') break;
            s += c;
        }
        if (!s.empty()) out.push_back(std::move(s));
        i = j + 1;
    }
    return out;
}

// Bind a std::string to an ImGui InputText via a scratch buffer.
bool InputPath(const char* id, std::string& s) {
    char buf[2048];
    std::snprintf(buf, sizeof buf, "%s", s.c_str());
    if (ImGui::InputText(id, buf, sizeof buf)) { s = buf; return true; }
    return false;
}

}  // namespace

void LoadOrderUI::Rescan(const std::string& pluginsDir, const std::string& loadOrderPath) {
    m_bundles.clear();
    m_warnings.clear();
    m_dragged = -1;

    // 1) Read loadorder.txt: ordered list of bundle stems (bare, later line wins). Comments
    //    ('#' / ';') and blanks skipped. Preserve the file's order as the initial priority.
    std::vector<std::string> order;                 // lowercased stems, in file order
    std::unordered_map<std::string, int> orderIdx;  // stem -> position
    {
        std::ifstream f(loadOrderPath);
        std::string line;
        while (std::getline(f, line)) {
            const std::string t = Trim(line);
            if (t.empty() || t[0] == '#' || t[0] == ';') continue;
            const std::string k = ToLower(t);
            if (!orderIdx.count(k)) { orderIdx[k] = static_cast<int>(order.size()); order.push_back(k); }
        }
    }

    // 2) Scan the plugins dir for *.hky bundles and read each manifest.json. A bundle is EITHER
    //    an unpacked DIRECTORY (<Mod>.hky/manifest.json on disk) OR a packed FILE (<Mod>.hky is a
    //    zip — the converter's ship form — with manifest.json inside it). Both are "present";
    //    only handling directories made every packed bundle look MISSING.
    std::unordered_map<std::string, Bundle> found;  // stem -> bundle

    // Fill a bundle's identity from its manifest.json text (empty = no manifest, keeps the
    // stem-derived defaults). Shared by the unpacked-dir and packed-file scan paths below.
    auto applyManifest = [](Bundle& b, const std::string& mf) {
        if (mf.empty()) return;
        b.hasManifest = true;
        if (std::string n = JsonStr(mf, "name"); !n.empty()) b.name = n;
        b.version = JsonStr(mf, "version");
        b.author  = JsonStr(mf, "author");
        for (auto& m : JsonStrArray(mf, "masters")) b.masters.push_back(ToLower(m));
    };

    std::error_code ec;
    if (fs::is_directory(pluginsDir, ec)) {
        for (fs::directory_iterator it(pluginsDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (ToLower(it->path().extension().string()) != ".hky") continue;
            std::error_code de;
            Bundle b;
            b.stem = it->path().stem().string();
            b.name = b.stem;
            if (it->is_directory(de)) {
                applyManifest(b, ReadFile(it->path() / "manifest.json"));
            } else if (it->is_regular_file(de)) {
                // Packed bundle: a .hky is a zip. Read manifest.json out of the archive (no
                // extraction). Even if the archive can't be opened it is still a real bundle on
                // disk, so it stays present (with stem-only defaults) rather than misreported
                // missing. Skip the master Skyrim.hky — it holds the full vanilla corpus (~18MB),
                // needs no manifest for ordering, and opening it would decompress the whole thing.
                if (ToLower(b.stem) != "skyrim") {
                    std::string herr;
                    if (auto arc = havok::model::HkyArchive::LoadFromFile(it->path().string(), herr))
                        if (auto mf = arc->file("manifest.json")) applyManifest(b, *mf);
                }
            } else {
                continue;
            }
            found.emplace(ToLower(b.stem), std::move(b));
        }
    }

    // 3) Build the display order: loadorder.txt entries first (in file order), then any
    //    on-disk bundle not yet listed (appended, lowest-to-highest by name). A listed
    //    stem with no folder on disk is shown as "missing".
    for (const auto& k : order) {
        auto f = found.find(k);
        if (f != found.end()) { m_bundles.push_back(f->second); found.erase(f); }
        else { Bundle miss; miss.stem = k; miss.name = k; miss.present = false; m_bundles.push_back(std::move(miss)); }
    }
    std::vector<Bundle> rest;
    for (auto& [k, b] : found) rest.push_back(std::move(b));
    std::sort(rest.begin(), rest.end(),
              [](const Bundle& a, const Bundle& b) { return ToLower(a.stem) < ToLower(b.stem); });
    for (auto& b : rest) m_bundles.push_back(std::move(b));

    m_pluginsDir = pluginsDir;
    m_loaded     = true;
    Revalidate();

    int onDisk = 0;
    for (const auto& b : m_bundles) if (b.present) ++onDisk;
    std::ostringstream st;
    st << onDisk << " bundle(s) on disk";
    if (m_bundles.size() != static_cast<std::size_t>(onDisk))
        st << ", " << (m_bundles.size() - onDisk) << " listed-but-missing";
    m_status = st.str();
}

void LoadOrderUI::Revalidate() {
    m_warnings.clear();
    // Position of each present bundle in the current order (index = priority).
    std::unordered_map<std::string, int> pos;
    for (int i = 0; i < static_cast<int>(m_bundles.size()); ++i)
        pos[ToLower(m_bundles[i].stem)] = i;

    for (int i = 0; i < static_cast<int>(m_bundles.size()); ++i) {
        const Bundle& b = m_bundles[i];
        if (!b.present) {
            m_warnings.push_back("'" + b.stem + "' is in loadorder.txt but has no folder on disk.");
            continue;
        }
        for (const std::string& m : b.masters) {
            if (ToLower(b.stem) == m) continue;
            const auto it = pos.find(m);
            if (it == pos.end()) {
                m_warnings.push_back("'" + b.name + "' declares master '" + m +
                                     "' but it is not present (its overrides bind to nothing).");
            } else if (it->second > i) {
                m_warnings.push_back("'" + b.name + "' loads BEFORE its master '" + m +
                                     "' — a master must sit above the bundles that build on it.");
            }
        }
    }
}

bool LoadOrderUI::Save(const std::string& loadOrderPath) {
    // Rewrite loadorder.txt in the current display order. Listed-but-missing entries are
    // preserved (user-owned; harmless), matching the converter's preserve-order contract.
    std::error_code ec;
    fs::create_directories(fs::path(loadOrderPath).parent_path(), ec);   // Data\community_behaviors\ may be new
    std::ofstream f(loadOrderPath, std::ios::binary | std::ios::trunc);
    if (!f) { m_status = "Save FAILED: cannot write " + loadOrderPath; return false; }
    for (const auto& b : m_bundles) f << b.stem << "\n";
    m_status = "Saved loadorder.txt (" + std::to_string(m_bundles.size()) + " entries).";
    return true;
}

void LoadOrderUI::Draw(const std::string& defaultPluginsDir, const std::string& loadOrderPath) {
    if (!m_loaded) Rescan(defaultPluginsDir, loadOrderPath);

    ImGui::TextUnformatted(
        "Order the Community Behaviors .hky bundles the converter bakes into the zip. "
        "Top = base (loads first); bottom = highest priority (wins). Drag a row to reorder.");
    ImGui::Spacing();

    ImGui::PushItemWidth(-220.0f);
    InputPath("##plugins", m_pluginsDir);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Browse##plugins")) {
        std::string p;
        if (sct::ui::PickFolder("Select a community_behaviors/plugins folder", m_pluginsDir.c_str(), p))
            Rescan(p, loadOrderPath);
    }
    ImGui::SameLine(); ImGui::TextUnformatted("plugins folder");

    if (ImGui::Button("Rescan")) Rescan(m_pluginsDir, loadOrderPath);
    ImGui::SameLine();
    if (ImGui::Button("Save load order")) Save(loadOrderPath);
    ImGui::SameLine();
    if (!m_status.empty()) ImGui::TextDisabled("%s", m_status.c_str());

    ImGui::Separator();

    // Reorderable list. Each row is a full-width Selectable carrying a formatted label;
    // dragging a row past its neighbour swaps them (the canonical ImGui reorder pattern).
    ImGui::BeginChild("bundles", ImVec2(0, -140.0f), true);
    for (int n = 0; n < static_cast<int>(m_bundles.size()); ++n) {
        const Bundle& b = m_bundles[n];
        char label[512];
        std::string masters;
        for (const auto& m : b.masters) { if (!masters.empty()) masters += ", "; masters += m; }
        // The trailing "###<stem>" fixes the Selectable's ImGui ID to the bundle, so a row keeps
        // its identity when it changes POSITION mid-drag. Without it the visible "%2d." number is
        // part of the ID, so every swap gave the dragged row a new ID and dropped its active
        // state — which is why a drag only ever moved ONE slot. A stable ID lets one drag glide
        // the row across many positions.
        std::snprintf(label, sizeof label, "%2d.  %-24s  v%-10s  %-16s%s%s###%s",
                      n + 1,
                      b.name.c_str(),
                      b.version.empty() ? "?" : b.version.c_str(),
                      b.author.empty() ? "" : b.author.c_str(),
                      masters.empty() ? "" : ("   masters: " + masters).c_str(),
                      b.present ? "" : "   [MISSING]",
                      b.stem.c_str());

        ImGui::Selectable(label, m_dragged == n);

        if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
            const int nNext = n + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
            if (nNext >= 0 && nNext < static_cast<int>(m_bundles.size())) {
                std::swap(m_bundles[n], m_bundles[nNext]);
                m_dragged = nNext;
                ImGui::ResetMouseDragDelta();
                Revalidate();
            }
        }
    }
    if (!ImGui::IsMouseDown(0)) m_dragged = -1;
    ImGui::EndChild();

    // Master-dependency warnings (the offline mirror of BR's runtime manifest validation).
    ImGui::TextDisabled("Validation");
    ImGui::BeginChild("warnings", ImVec2(0, 0), true);
    if (m_warnings.empty())
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.55f, 1.0f), "No master-order problems.");
    else
        for (const auto& w : m_warnings)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.4f, 1.0f), "! %s", w.c_str());
    ImGui::EndChild();
}
