# EmbedEngineRegistry.cmake — generate EngineRegistry.gen.h (constexpr engine symbols) from the engine
# symbol registry src/core/hooks/registry/engine.yaml. The yaml is the single hand-edited source; this bakes it
# into the plugin at build time so a misspelled symbol is a compile error, not a runtime lookup miss.
#
# Invoked as a script:  cmake -DREGISTRY_YAML=<file> -DOUT_H=<file> -P EmbedEngineRegistry.cmake
#
# Every symbol is ONE line of a fixed shape:
#   - { name: Ident, kind: func|call|data|tls, se: <pin>, ae: <pin>, vr: <pin>[, cpp: "<C++ name>"] }   # optional note
# where <pin> = <AddressLibraryID> or <AddressLibraryID>+0x<hexOffset> (offsets only on `call` and `tls`), 0 = unavailable.
# `cpp` is read by tools/cbmatch (the symbol reimplemented code references); the codegen ignores it.
# A line-oriented regex parse keeps codegen tool-free (no build-time Python), like EmbedEnums.cmake.

if(NOT DEFINED REGISTRY_YAML OR NOT DEFINED OUT_H)
    message(FATAL_ERROR "EmbedEngineRegistry.cmake: REGISTRY_YAML and OUT_H are required")
endif()

# Split into lines WITHOUT letting list semantics touch the text. file(STRINGS) / a raw newline->';' split
# both leave ';', '\' and '[' ']' live: a ';' splits a line, a trailing '\' escapes the separator, and an
# unbalanced '[' (a note's "[from, to)") opens a bracket group that swallows every later line into one
# list element. So those four are swapped for control-character placeholders (never in the yaml) before
# the split and restored per line.
file(READ "${REGISTRY_YAML}" _text)
string(ASCII 1 _escBs)
string(ASCII 2 _escSc)
string(ASCII 3 _escLb)
string(ASCII 4 _escRb)
string(REPLACE "\r" "" _text "${_text}")
string(REPLACE "\\" "${_escBs}" _text "${_text}")
string(REPLACE ";" "${_escSc}" _text "${_text}")
string(REPLACE "[" "${_escLb}" _text "${_text}")
string(REPLACE "]" "${_escRb}" _text "${_text}")
string(REPLACE "\n" ";" _lines "${_text}")

# Every entry line, counted on the raw text independently of the loop below; the two must agree.
string(REGEX MATCHALL "(^|\n)[ ]*-[ ]*{" _entryStarts "${_text}")
list(LENGTH _entryStarts _expected)

set(_pin "([0-9]+)(\\+0x[0-9a-fA-F]+)?")
set(_entry "^[ ]*-[ ]*{[ ]*name:[ ]*([A-Za-z_][A-Za-z0-9_]*)[ ]*,[ ]*kind:[ ]*(func|call|data|tls)[ ]*,[ ]*se:[ ]*([0-9x+a-fA-F]+)[ ]*,[ ]*ae:[ ]*([0-9x+a-fA-F]+)[ ]*,[ ]*vr:[ ]*([0-9x+a-fA-F]+)[ ]*(,[ ]*(cpp|mangled|this|header):[ ]*\"[^\"]*\")*[ ]*}")

set(_defs "")
set(_all "")
set(_names "")
set(_n 0)

# The schema link shim (OUT_SHIM_ASM / OUT_SHIM_CPP / OUT_IDENTITY_H): every row with `mangled` makes that schema
# declaration usable from the plugin. A func gets a jump stub bound to its decorated name (/alternatename) that
# lands on vanilla's entry (so in the takeover chain when the image runs it) and an Identity<&fn> for Override; a
# const data row gets a copy of vanilla's bytes under its decorated name.
set(_shimStubs "")
set(_shimSlots "")
set(_shimMiss "")
set(_shimAlt "")
set(_shimFuncs "")
set(_shimData "")
set(_shimDataDefs "")
set(_shimHeaders "")
set(_identity "")
set(_nf 0)
set(_nd 0)

# _cb_pin(<text> <kind> <name> <runtime> <outVar>) -> "{ id, 0xOFF }", validating the call/offset rule.
function(_cb_pin _text _kind _name _rt _out)
    if(NOT _text MATCHES "^${_pin}$")
        message(FATAL_ERROR "engine.yaml: ${_name}.${_rt} = '${_text}' is not <id> or <id>+0x<off>")
    endif()
    set(_id "${CMAKE_MATCH_1}")
    set(_off "${CMAKE_MATCH_2}")
    if(_off)
        string(SUBSTRING "${_off}" 1 -1 _off)   # drop the '+'
        if(NOT _kind STREQUAL "call" AND NOT _kind STREQUAL "tls")
            message(FATAL_ERROR "engine.yaml: ${_name}.${_rt} has an offset but kind is '${_kind}' (offsets are for call sites and thread_locals)")
        endif()
    else()
        if((_kind STREQUAL "call" OR _kind STREQUAL "tls") AND NOT _id STREQUAL "0")
            message(FATAL_ERROR "engine.yaml: ${_name}.${_rt} is a ${_kind} pin without +0x<offset>")
        endif()
        set(_off "0x0")
    endif()
    set(${_out} "{ ${_id}, ${_off} }" PARENT_SCOPE)
endfunction()

foreach(_l IN LISTS _lines)
    if(NOT _l MATCHES "^[ ]*-[ ]*{")
        continue()
    endif()
    string(REPLACE "${_escBs}" "\\" _l "${_l}")
    string(REPLACE "${_escSc}" ";" _l "${_l}")
    string(REPLACE "${_escLb}" "[" _l "${_l}")
    string(REPLACE "${_escRb}" "]" _l "${_l}")
    if(NOT _l MATCHES "${_entry}")
        message(FATAL_ERROR "engine.yaml: malformed symbol line:\n  ${_l}")
    endif()
    set(_name "${CMAKE_MATCH_1}")
    set(_kind "${CMAKE_MATCH_2}")
    set(_se "${CMAKE_MATCH_3}")
    set(_ae "${CMAKE_MATCH_4}")
    set(_vr "${CMAKE_MATCH_5}")
    if(_name IN_LIST _names)
        message(FATAL_ERROR "engine.yaml: duplicate symbol '${_name}'")
    endif()
    list(APPEND _names "${_name}")
    _cb_pin("${_se}" "${_kind}" "${_name}" se _sePin)
    _cb_pin("${_ae}" "${_kind}" "${_name}" ae _aePin)
    _cb_pin("${_vr}" "${_kind}" "${_name}" vr _vrPin)
    if(_sePin MATCHES "^{ 0," AND _aePin MATCHES "^{ 0," AND _vrPin MATCHES "^{ 0,")
        message(FATAL_ERROR "engine.yaml: ${_name} has no pin on any runtime")
    endif()
    if(_kind STREQUAL "func")
        set(_k "Kind::Func")
    elseif(_kind STREQUAL "call")
        set(_k "Kind::Call")
    elseif(_kind STREQUAL "tls")
        set(_k "Kind::Tls")
    else()
        set(_k "Kind::Data")
    endif()
    string(APPEND _defs "inline constexpr Symbol ${_name}{ \"${_name}\", ${_k}, ${_sePin}, ${_aePin}, ${_vrPin} };\n")
    string(APPEND _all "    &${_name},\n")
    math(EXPR _n "${_n}+1")

    string(REGEX MATCH "{[^#]*}" _fields "${_l}")
    foreach(_field cpp mangled this header)
        set(_${_field} "")
        if(_fields MATCHES "${_field}:[ ]*\"([^\"]*)\"")
            set(_${_field} "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(NOT _mangled)
        continue()
    endif()
    if(NOT _cpp OR NOT _header)
        message(FATAL_ERROR "engine.yaml: ${_name} has `mangled` but no `cpp` and `header` (the shim needs the declaration)")
    endif()
    if(NOT "${_header}" IN_LIST _shimHeaders)
        list(APPEND _shimHeaders "${_header}")
    endif()
    if(_kind STREQUAL "func")
        math(EXPR _off "${_nf}*8")
        string(APPEND _shimStubs "cb_shim_${_nf} PROC PUBLIC\n    jmp QWORD PTR [cb_shim_slots + ${_off}]\ncb_shim_${_nf} ENDP\n")
        string(APPEND _shimMiss "cb_shim_miss_${_nf} PROC\n    mov eax, ${_nf}\n    jmp cb_shim_miss_common\ncb_shim_miss_${_nf} ENDP\n")
        if(_nf EQUAL 0)
            string(APPEND _shimSlots "cb_shim_slots QWORD cb_shim_miss_${_nf}\n")
        else()
            string(APPEND _shimSlots "              QWORD cb_shim_miss_${_nf}\n")
        endif()
        string(APPEND _shimAlt "#pragma comment(linker, \"/alternatename:${_mangled}=cb_shim_${_nf}\")\n")
        string(APPEND _shimFuncs "    &engine::${_name},\n")
        # The class owning the member: `this` when the image passes a base subobject, else the cpp name's scope.
        string(REGEX REPLACE "::[A-Za-z_~][A-Za-z0-9_]*$" "" _scope "${_cpp}")
        if(_this)
            set(_thisType "::${_this}")
        elseif(_scope STREQUAL "CB::schema")
            set(_thisType "void")
        else()
            set(_thisType "::${_scope}")
        endif()
        string(APPEND _identity "template <>\nstruct Identity<&::${_cpp}> {\n    static constexpr const char* mangled = \"${_mangled}\";\n    static constexpr const char* name    = \"${_cpp}\";\n    using This                           = ${_thisType};\n};\n\n")
        math(EXPR _nf "${_nf}+1")
    elseif(_kind STREQUAL "data")
        if(NOT _mangled MATCHES "B$")
            message(FATAL_ERROR "engine.yaml: ${_name} is mutable data: the shim copies const globals only (a mutable one needs an accessor)")
        endif()
        string(APPEND _shimDataDefs "alignas(alignof(decltype(::${_cpp}))) unsigned char cb_data_${_nd}[sizeof(::${_cpp})];\n")
        string(APPEND _shimAlt "#pragma comment(linker, \"/alternatename:${_mangled}=cb_data_${_nd}\")\n")
        string(APPEND _shimData "    { &engine::${_name}, cb_data_${_nd}, sizeof(::${_cpp}) },\n")
        math(EXPR _nd "${_nd}+1")
    else()
        message(FATAL_ERROR "engine.yaml: ${_name} has `mangled` but kind '${_kind}' (only func and data are shimmed)")
    endif()
endforeach()

if(_n EQUAL 0)
    message(FATAL_ERROR "engine.yaml: no symbols parsed from ${REGISTRY_YAML}")
endif()
if(NOT _n EQUAL _expected)
    message(FATAL_ERROR "engine.yaml: parsed ${_n} symbols but the file has ${_expected} '- {' entry lines")
endif()

# _cb_write(<file> <text>): write only on change, so an untouched registry rebuilds nothing.
function(_cb_write _file _text)
    if(EXISTS "${_file}")
        file(READ "${_file}" _old)
        if(_old STREQUAL _text)
            return()
        endif()
    endif()
    file(WRITE "${_file}" "${_text}")
endfunction()

set(_body
"// GENERATED by cmake/EmbedEngineRegistry.cmake from src/core/hooks/registry/engine.yaml -- DO NOT EDIT.
// The yaml is the single source; regenerated at build whenever it changes. Resolved via
// <core/hooks/registry/Registry.h>.
#pragma once

#include <core/hooks/registry/Symbol.h>

#include <cstddef>

namespace CB::core::hooks::engine {

${_defs}
inline constexpr const Symbol* kAll[] = {
${_all}};

inline constexpr std::size_t kCount = ${_n};

} // namespace CB::core::hooks::engine
")
_cb_write("${OUT_H}" "${_body}")

if(NOT DEFINED OUT_SHIM_ASM OR NOT DEFINED OUT_SHIM_CPP OR NOT DEFINED OUT_IDENTITY_H)
    return()
endif()

set(_includes "")
foreach(_h IN LISTS _shimHeaders)
    string(APPEND _includes "#include <${_h}>
")
endforeach()
if(_nf EQUAL 0)
    set(_shimSlots "cb_shim_slots QWORD 0
")
endif()
set(_nfa ${_nf})
if(_nfa EQUAL 0)
    set(_nfa 1)
endif()
set(_nda ${_nd})
if(_nda EQUAL 0)
    set(_nda 1)
    set(_shimData "    { nullptr, nullptr, 0 },
")
endif()

_cb_write("${OUT_SHIM_ASM}"
"; GENERATED by cmake/EmbedEngineRegistry.cmake from src/core/hooks/registry/engine.yaml -- DO NOT EDIT.
; The schema link shim: one jump stub per shimmed function. A slot starts at its miss stub, which resolves the
; address once (cb_shim_resolve) and jumps on with the caller's arguments untouched.
EXTERN cb_shim_resolve:PROC
.data
ALIGN 8
PUBLIC cb_shim_slots
${_shimSlots}
.code
${_shimStubs}
${_shimMiss}
; eax = slot. Saves the four argument registers and xmm0-3 around the resolve call, then tail-jumps to the target.
cb_shim_miss_common PROC FRAME
    push rcx
    .pushreg rcx
    push rdx
    .pushreg rdx
    push r8
    .pushreg r8
    push r9
    .pushreg r9
    sub rsp, 104
    .allocstack 104
    .endprolog
    movdqu XMMWORD PTR [rsp + 32], xmm0
    movdqu XMMWORD PTR [rsp + 48], xmm1
    movdqu XMMWORD PTR [rsp + 64], xmm2
    movdqu XMMWORD PTR [rsp + 80], xmm3
    mov ecx, eax
    call cb_shim_resolve
    movdqu xmm0, XMMWORD PTR [rsp + 32]
    movdqu xmm1, XMMWORD PTR [rsp + 48]
    movdqu xmm2, XMMWORD PTR [rsp + 64]
    movdqu xmm3, XMMWORD PTR [rsp + 80]
    add rsp, 104
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp rax
cb_shim_miss_common ENDP
END
")

_cb_write("${OUT_SHIM_CPP}"
"// GENERATED by cmake/EmbedEngineRegistry.cmake from src/core/hooks/registry/engine.yaml -- DO NOT EDIT.
// The schema link shim's C++ half: decorated-name bindings, the per-slot symbols, and the const data copies.
#include <core/hooks/registry/Registry.h>
#include <core/hooks/takeover/SchemaShim.h>

${_includes}
#include <cstddef>
#include <cstdint>
#include <cstring>

${_shimAlt}
extern \"C\" {
extern std::uintptr_t cb_shim_slots[${_nfa}];
${_shimDataDefs}}

namespace CB::core::hooks::detail {

const engine::Symbol* const kFuncs[${_nfa}] = {
${_shimFuncs}};
inline constexpr std::size_t kFuncCount = ${_nf};

const ShimData kData[${_nda}] = {
${_shimData}};
inline constexpr std::size_t kDataCount = ${_nd};

} // namespace CB::core::hooks::detail

extern \"C\" std::uintptr_t cb_shim_resolve(unsigned slot)
{
    return CB::core::hooks::detail::ResolveShimSlot(slot, CB::core::hooks::detail::kFuncs, CB::core::hooks::detail::kFuncCount, cb_shim_slots);
}

void CB::core::hooks::InstallSchemaShim()
{
    detail::CopyShimData(detail::kData, detail::kDataCount);
}
")

_cb_write("${OUT_IDENTITY_H}"
"// GENERATED by cmake/EmbedEngineRegistry.cmake from src/core/hooks/registry/engine.yaml -- DO NOT EDIT.
// Identity<&fn> for every shimmed schema function: what Override<&fn> needs beyond the declaration's own type.
#pragma once

${_includes}
namespace CB::core::hooks {

template <auto F>
struct Identity;

${_identity}} // namespace CB::core::hooks
")
