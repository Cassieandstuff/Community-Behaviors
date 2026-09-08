# ── havok-skeleton ─────────────────────────────────────────────────────────────
# The schema-driven SKELETON codec (data-driven Havok stack). A skeleton .hkx is a generic Havok
# packfile, so reading/writing it is a first-class member of the stack — NOT a tool-scoped import.
# Lifted schema-native out of the quarantined havok-core (havok::sct) so the whole skeleton path is
# data-driven end to end and havok-core-free:
#
#   skeleton.hkx ─ SkeletonImport (LoadSkeletonsFromHkx / ReadSkeletonPhysics) ─▶ SkeletonData
#        ▲  (read: deserialize via havok-io SchemaObject graph → plain model)
#
# NO typed hka*/hkp* classes and NO havok-core dependency — the packfile is walked through the schema
# object model (havok-io) driven by the Havok/ class descriptors (havok-schema). The typed havok-core
# reader/compiler stays as the OFFLINE correctness oracle only.
#
# CB source module. Public headers on the path as <havok/skeleton/*.h>.

sct_collect_sources(HAVOK_SKELETON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(havok-skeleton STATIC ${HAVOK_SKELETON_SOURCES})

target_include_directories(havok-skeleton
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>   # <havok/skeleton/*.h>
           $<INSTALL_INTERFACE:include>)

# havok-io brings the generic SchemaObject deserializer + MakeSchemaFactory, and PUBLIC-links
# havok-framing (HkTypes, PackFileDeserializer/BinaryReaderEx) + havok-schema (SharedRegistry).
target_link_libraries(havok-skeleton PUBLIC havok-io)

set_target_properties(havok-skeleton PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(havok-skeleton PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
