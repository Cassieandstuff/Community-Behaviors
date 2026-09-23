# ── compile ──────────────────────────────────────────────────────────────────
# CB::core compile — build-direction orchestration + one-way DERIVES (no reverse,
# so NOT codecs). First tenant (org-pass firesale): AnimDataFromBehavior — derive a
# project's animationdata (clip list + annotation triggers + durations) FROM an
# in-memory BehaviorData, the runtime+offline shared derive. ExtractAnimClipInfo is
# now schema-native (havok-io SchemaObject graph, no typed hka* classes), so this
# drained cleanly out of the quarantined havok-core. Include root Havok/core/cpp/ ->
# headers resolve as <compile/*.h>.
sct_collect_sources(CB_COMPILE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(compile STATIC ${CB_COMPILE_SOURCES})

target_include_directories(compile
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>   # <compile/*.h>
            $<INSTALL_INTERFACE:include>)

# interface = AnimationData model; havok-model = BehaviorData + defs + AnimDataDeriver (DeriveClipList);
# havok-framing = PackFileDeserializer; havok-io = SchemaObject/MakeSchemaFactory; havok-schema =
# SharedRegistry (the schema-native anim read). havok-model PUBLIC-propagates io/schema/interface, but
# name them so the deps are explicit and survive a havok-model retarget.
target_link_libraries(compile PUBLIC interface havok-io havok-schema havok-framing havok-model codec)

set_target_properties(compile PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(compile PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
