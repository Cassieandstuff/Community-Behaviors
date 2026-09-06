// build_behavior.cpp — the full havok-core pipeline as a CLI tool.
//
//   authored YAML tree  ──►  Def model  ──►  Tier-A object graph  ──►  .hkx bytes
//   (YamlBehaviorLoader)    (BehaviorData)   (BehaviorBuilder)      (PackFileSerializer)
//
// This is the headline capability: compile a behavior authored as YAML straight
// to a valid Havok packfile, first-party C++, no XML / no .NET / no subprocess.
// Point it at True Flight's source tree to build the real thing:
//
//   build_behavior "<repo>\modules\true-flight\src_behavior\meshes\actors\character\behaviors\trueflight\trueflight.hkx" trueflight.hkx
//
// VS-ONLY BUILD (this TU pulls in YamlBehaviorLoader, the one ryml-dependent unit).
// havok-core's CMake ryml-gates the yaml TU; build this tool inside that build, or
// from a VS x64 dev prompt with vcpkg's ryml on the include/lib path, e.g.:
//
//   cl /nologo /std:c++latest /EHsc /W4 /wd4100 ^
//     /I "<repo>\libs\havok-core\include" ^
//     /I "<vcpkg>\installed\x64-windows-static\include" ^
//     <all .cpp under libs\havok-core\src, INCLUDING src\model\yaml\YamlBehaviorLoader.cpp> ^
//     "<repo>\libs\havok-core\tools\build_behavior.cpp" ^
//     /Fe:build_behavior.exe /link /LIBPATH:"<vcpkg>\installed\x64-windows-static\lib" ryml.lib
//
// (Everything except YamlBehaviorLoader.cpp also compiles standalone with no deps;
//  it is only this loader — and therefore this tool — that needs ryml.)

#include "havok/model/yaml/YamlBehaviorLoader.h"
#include "havok/sct/BehaviorCompiler.h"

#include <cstdio>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: build_behavior <behavior.hkx dir (contains behavior.yaml)> [out.hkx]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const std::string out = argc > 2 ? argv[2] : "out.hkx";

    try {
        // 1) YAML tree -> Def content model.
        const havok::model::BehaviorData data = havok::model::YamlBehaviorLoader::Load(dir);

        // 2) Def model -> Tier-A object graph -> bytes, validate (round-trip), write.
        const havok::sct::CompileResult r =
            havok::sct::CompileBehaviorToFile(data, out, /*validate*/ true);

        if (!r.ok) {
            std::printf("FAIL: %s\n", r.error.c_str());
            return 1;
        }
        std::printf("OK: compiled '%s' -> %s (%zu bytes), validated.\n",
                    dir.c_str(), out.c_str(), r.bytes.size());
        return 0;
    } catch (const std::exception& e) {
        std::printf("ERROR: %s\n", e.what());
        return 1;
    }
}
