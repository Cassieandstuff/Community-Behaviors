# dna-probe: the first piece of the experimental MetaHuman work. Opt-in with CB_WITH_METAHUMAN at the top level, and it
# also configures on its own (cmake -S tools/dna-probe), which skips the plugin's dependencies.
cmake_minimum_required(VERSION 3.25)
project(dna-probe C CXX)

include(FetchContent)
set(RL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RL_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(RL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RL_BUILD_WITH_SSE ON CACHE BOOL "" FORCE)
set(RL_BUILD_WITH_AVX ON CACHE BOOL "" FORCE)
FetchContent_Declare(openriglogic
    GIT_REPOSITORY https://github.com/EpicGames/OpenRigLogic.git
    GIT_TAG        d3d329bbd6992ac38adc1c1b24203ceb998f75c0) # 5.8 branch, v13.2.9-hotfix2
FetchContent_MakeAvailable(openriglogic)

add_executable(dna-probe dna-probe.cpp)
target_link_libraries(dna-probe PRIVATE RigLogic::riglogic4)
target_compile_features(dna-probe PRIVATE cxx_std_17)
