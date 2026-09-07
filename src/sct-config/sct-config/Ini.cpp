#include <sct-config/SctConfig.h>

namespace sct::config {

IniBatch LoadIni(const ScanSpec& spec) {
    ScanSpec s = spec;
    if (s.extensions.empty()) s.extensions = { ".ini" };

    IniBatch batch;
    for (Source& src : Discover(s)) {
        auto ini = std::make_shared<CSimpleIniA>();
        ini->SetUnicode();   // UTF-8 + BOM handling
        const SI_Error rc = ini->LoadFile(src.path.string().c_str());
        if (rc < 0) {
            batch.errors.push_back({ src, "SimpleIni: load failed" });
            continue;
        }
        batch.docs.push_back(IniDoc{ std::move(src), std::move(ini) });
    }
    return batch;
}

}  // namespace sct::config
