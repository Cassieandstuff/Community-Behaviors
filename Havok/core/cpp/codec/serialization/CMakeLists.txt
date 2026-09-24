# ── codec/serialization ────────────────────────────────────────────────────────
# CB::core serialization — the FOUNDATIONAL codec: the graph ↔ bytes translators the whole
# stack rests on. Org-pass firesale phase 1 lifted the packfile spine out of src/havok-framing +
# src/havok-io into its permanent home here, mirroring Havok's own Common/Serialize (packfile,
# tagfile, … as sibling encodings of one reflected object graph).
#
#   packfile/   the binary packfile de/ser primitives + object base
#     IHavokObject / HavokRegistry            (object base + className→factory infra)
#     BinaryReaderEx / BinaryWriterEx         (byte cursors)
#     PackFileTypes / PackFileDeserializer / PackFileSerializer   (framing)
#   HavokIo       the generic schema-driven object read/write layer over the packfile
#                 (walks a havok-schema ClassSchema → SchemaObject graph and back, byte-identical)
#
# (tagfile/ + yaml/ join as sibling encodings in later phases; the .txt sidecar formats stay in
#  codec/format — those serialize a DIFFERENT, smaller model, not the behavior graph.)
#
# Include ROOT is Havok/core/cpp/, so headers resolve as <codec/serialization/…> and
# <codec/serialization/packfile/…> — the function-folder IS the path namespace (no havok/ prefix).
sct_collect_sources(CB_SERIALIZATION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(serialization STATIC ${CB_SERIALIZATION_SOURCES})

target_include_directories(serialization
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/Havok/core/cpp>   # <codec/serialization/…>
            $<INSTALL_INTERFACE:include>)

# common = the portable HkTypes value types the byte cursors read/write; havok-schema = the
# ClassSchema descriptors HavokIo walks (its public header pulls <havok-schema/HavokSchema.h>).
# havok-schema is a clean ryml-only base — no cycle back through framing/serialization.
target_link_libraries(serialization PUBLIC common havok-schema)

set_target_properties(serialization PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(serialization PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
