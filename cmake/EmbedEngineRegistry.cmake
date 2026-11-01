# EmbedEngineRegistry.cmake — generate EngineRegistry.gen.h (constexpr engine symbols) from the engine
# symbol registry src/platform/hooks/registry/engine.yaml. The yaml is the single hand-edited source; this bakes it
# into the plugin at build time so a misspelled symbol is a compile error, not a runtime lookup miss.
#
# Invoked as a script:  cmake -DREGISTRY_YAML=<file> -DOUT_H=<file> -P EmbedEngineRegistry.cmake
#
# Every symbol is ONE line of a fixed shape:
#   - { name: Ident, kind: func|call|code|data|tls, se: <pin>, ae: <pin>, vr: <pin>[, cpp: "<C++ name>"] }   # optional note
# where <pin> = <AddressLibraryID> or <AddressLibraryID>+0x<hexOffset> (offsets only on `call`, `code` and `tls`), 0 = unavailable.
# `cpp` is the symbol reimplemented code references (cbmatch's key; matching now lives in OPEN-HAVOK); the codegen ignores it.
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
string(ASCII 5 _sdkMark)
# SDK_ROOT (the CB SDK submodule): its registry rows (Tools/cbmatch/registry.yaml, generated/ExeClasses.yaml) follow
# engine.yaml's, each line marked. They own every SDK declaration: an engine.yaml row still spelled in CB::havok is
# skipped, and an SDK row whose name engine.yaml already has yields to it. Their `header` paths are matched to the
# SDK tree by file name (the converted rows keep the old havok/ paths).
if(DEFINED SDK_ROOT)
    foreach(_sdkFile "${SDK_ROOT}/Tools/cbmatch/registry.yaml" "${SDK_ROOT}/Tools/Reconstruction/generated/ExeClasses.yaml" "${SDK_ROOT}/Tools/Reconstruction/generated/Defined.yaml" "${SDK_ROOT}/Tools/Reconstruction/generated/Referenced.yaml")
        file(READ "${_sdkFile}" _sdkText)
        string(REGEX REPLACE "(^|\n)([ ]*-[ ]*{)" "\\1${_sdkMark}\\2" _sdkText "${_sdkText}")
        string(APPEND _text "\n${_sdkText}")
    endforeach()
    foreach(_root Source Bethesda)
        file(GLOB_RECURSE _sdkHeaders RELATIVE "${SDK_ROOT}/${_root}" "${SDK_ROOT}/${_root}/*.h")
        foreach(_h IN LISTS _sdkHeaders)
            get_filename_component(_hn "${_h}" NAME)
            if(NOT DEFINED _sdkHeader_${_hn})
                set(_sdkHeader_${_hn} "${_h}")
            endif()
        endforeach()
    endforeach()
endif()
string(REPLACE "\r" "" _text "${_text}")
string(REPLACE "\\" "${_escBs}" _text "${_text}")
string(REPLACE ";" "${_escSc}" _text "${_text}")
string(REPLACE "[" "${_escLb}" _text "${_text}")
string(REPLACE "]" "${_escRb}" _text "${_text}")
string(REPLACE "\n" ";" _lines "${_text}")

# Every entry line, counted on the raw text independently of the loop below; the two must agree.
string(REGEX MATCHALL "(^|\n)${_sdkMark}?[ ]*-[ ]*{" _entryStarts "${_text}")
list(LENGTH _entryStarts _expected)

set(_pin "([0-9]+)(\\+0x[0-9a-fA-F]+)?")
set(_entry "^[ ]*-[ ]*{[ ]*name:[ ]*([A-Za-z_][A-Za-z0-9_]*)[ ]*,[ ]*kind:[ ]*(func|call|code|data|tls)[ ]*,[ ]*se:[ ]*([0-9x+a-fA-F]+)[ ]*,[ ]*ae:[ ]*([0-9x+a-fA-F]+)[ ]*,[ ]*vr:[ ]*([0-9x+a-fA-F]+)[ ]*(,[ ]*(cpp|mangled|this|header|sig):[ ]*\"[^\"]*\")*[ ]*}")

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
set(_implExterns "")
set(_shimMangled "")
set(_implTable "")
set(_stubTable "")
set(_refDefs "")
set(_refData "")
set(_nr 0)
set(_identityHeaders "")
set(_dataHeaders "")
set(_identity "")
set(_keys "")
set(_nk 0)
set(_globals "")
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
        if(NOT _kind STREQUAL "call" AND NOT _kind STREQUAL "code" AND NOT _kind STREQUAL "tls" AND NOT _kind STREQUAL "data")
            message(FATAL_ERROR "engine.yaml: ${_name}.${_rt} has an offset but kind is '${_kind}' (offsets are for call sites, patched instructions, thread_locals and data inside a table)")
        endif()
    else()
        if((_kind STREQUAL "call" OR _kind STREQUAL "code" OR _kind STREQUAL "tls") AND NOT _id STREQUAL "0")
            message(FATAL_ERROR "engine.yaml: ${_name}.${_rt} is a ${_kind} pin without +0x<offset>")
        endif()
        set(_off "0x0")
    endif()
    set(${_out} "{ ${_id}, ${_off} }" PARENT_SCOPE)
endfunction()

set(_skipped 0)
set(_seenMangled "")
foreach(_l IN LISTS _lines)
    set(_fromSdk OFF)
    if(_l MATCHES "^${_sdkMark}")
        set(_fromSdk ON)
        string(SUBSTRING "${_l}" 1 -1 _l)
    endif()
    if(NOT _l MATCHES "^[ ]*-[ ]*{")
        continue()
    endif()
    if(DEFINED SDK_ROOT AND NOT _fromSdk AND (_l MATCHES "@havok@CB@@" OR _l MATCHES "CB::havok"))
        math(EXPR _skipped "${_skipped}+1")
        continue()
    endif()
    if(_fromSdk)
        string(REPLACE "CB::havok::" "" _l "${_l}")
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
    if(_fromSdk AND _l MATCHES "mangled:[ ]*\"([^\"]*)\"")
        if("${CMAKE_MATCH_1}" IN_LIST _seenMangled)
            math(EXPR _skipped "${_skipped}+1")
            continue()
        endif()
    endif()
    if(_l MATCHES "mangled:[ ]*\"([^\"]*)\"")
        list(APPEND _seenMangled "${CMAKE_MATCH_1}")
    endif()
    if(_name IN_LIST _names)
        if(_fromSdk)
            math(EXPR _skipped "${_skipped}+1")
            continue()
        endif()
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
    elseif(_kind STREQUAL "code")
        set(_k "Kind::Code")
    elseif(_kind STREQUAL "tls")
        set(_k "Kind::Tls")
    else()
        set(_k "Kind::Data")
    endif()
    string(APPEND _defs "inline constexpr Symbol ${_name}{ \"${_name}\", ${_k}, ${_sePin}, ${_aePin}, ${_vrPin} };\n")
    string(APPEND _all "    &${_name},\n")
    math(EXPR _n "${_n}+1")

    string(REGEX MATCH "{[^#]*}" _fields "${_l}")
    foreach(_field cpp mangled this header sig)
        set(_${_field} "")
        if(_fields MATCHES "[{ ,]${_field}:[ ]*\"([^\"]*)\"")
            set(_${_field} "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(NOT _mangled)
        continue()
    endif()
    # A compiler-internal function (__global_delete, ...) is the plugin's own CRT's: binding it to the exe's collides
    # with the compiler's weak default.
    if(_mangled MATCHES "^\\?__")
        continue()
    endif()
    # An SDK row binds the call and the takeover only: its converted `sig` predates the SDK's declarations, so an
    # Identity for Override comes from an engine.yaml row naming the declaration a feature overrides. A data row keeps
    # its header (the GlobalIdentity / const copy need the type).
    if(_fromSdk AND _kind STREQUAL "func")
        set(_header "")
    endif()
    set(_globalIdentity ON)
    if(_fromSdk)
        set(_globalIdentity OFF)
    endif()
    if(_fromSdk AND _header)
        string(REPLACE "|" ";" _hs "${_header}")
        set(_header "")
        foreach(_h IN LISTS _hs)
            get_filename_component(_hn "${_h}" NAME)
            if(NOT DEFINED _sdkHeader_${_hn})
                set(_header "")
                break()
            endif()
            list(APPEND _header "${_sdkHeader_${_hn}}")
        endforeach()
        list(JOIN _header "|" _header)
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
        string(APPEND _shimMangled "    \"${_mangled}\",\n")
        # The function's own decorated name: the SDK's compiled definition when the plugin has one, else (through
        # the alternatename above) this stub. The takeover's `dll` source jumps vanilla to the former.
        string(LENGTH "${_mangled}" _ml)
        if(_ml LESS 240)
            string(APPEND _implExterns "EXTERN ${_mangled}:PROC\n")
            string(APPEND _implTable "    QWORD ${_mangled}\n")
        else()
            string(APPEND _implTable "    QWORD 0\n")
        endif()
        string(APPEND _stubTable "    QWORD cb_shim_${_nf}\n")
        math(EXPR _nf "${_nf}+1")
        # Identity<&fn> (Override's) for a declaration a feature can name: `header` declares it, `sig` is its exact
        # member-pointer type (so an overloaded name resolves), `this` the base subobject the image passes when a
        # non-primary base introduced the virtual. Without `header` the row only binds the call.
        if(NOT _header)
            continue()
        endif()
        # A hand row may have no `cpp`, or keep the decorated name there (cbmatch's key for an overload): spell the C++
        # name from the decoration (`?name@Class@havok@CB@@…` → CB::havok::Class::name). A special or templated name
        # can't be spelled: no Identity.
        if(NOT _cpp OR _cpp MATCHES "^\\?")
            set(_cpp "${_mangled}")
            if(_cpp MATCHES "^\\?\\?" OR _cpp MATCHES "\\$" OR NOT _cpp MATCHES "^\\?([^@]+(@[^@]+)*)@@")
                continue()
            endif()
            string(REPLACE "@" ";" _parts "${CMAKE_MATCH_1}")
            list(REVERSE _parts)
            list(JOIN _parts "::" _cpp)
        endif()
        string(REPLACE "|" ";" _fheaders "${_header}")
        foreach(_h IN LISTS _fheaders)
            if(NOT "${_h}" IN_LIST _identityHeaders)
                list(APPEND _identityHeaders "${_h}")
            endif()
        endforeach()
        set(_owner "")
        if(_sig MATCHES "\\(([^()]*)::\\*\\)")
            set(_owner "${CMAKE_MATCH_1}")
        elseif(NOT _sig)
            string(REGEX REPLACE "::[A-Za-z_~][A-Za-z0-9_]*$" "" _owner "${_cpp}")
            if(_owner STREQUAL _cpp OR _owner MATCHES "^CB::platform::")   # unscoped, or a namespace: a free function
                set(_owner "")
            endif()
        endif()
        if(_this)
            set(_thisType "::${_this}")
        elseif(_owner)
            set(_thisType "::${_owner}")
        else()
            set(_thisType "void")
        endif()
        if(_sig)
            set(_key "static_cast<${_sig}>(&::${_cpp})")
        else()
            set(_key "&::${_cpp}")
        endif()
        # A protected or private member (its decorated name's access code after the scope: A-F, I-M) is named from
        # the SDK's extension-access specialization below; any other directly, since MSVC loses a member pointer's
        # identity through a variable when its class has several bases.
        if(_mangled MATCHES "^\\?[^?][^@]*(@[^@]+)*@@[A-FI-M]")
            string(APPEND _keys "    static constexpr auto k${_nk} = ${_key};\n")
            set(_key "kKeys::k${_nk}")
            math(EXPR _nk "${_nk}+1")
        endif()
        string(APPEND _identity "template <>\nstruct Identity<${_key}> {\n    static constexpr const char* mangled = \"${_mangled}\";\n    static constexpr const char* name    = \"${_cpp}\";\n    static constexpr const engine::Symbol& symbol = engine::${_name};\n    using This                           = ${_thisType};\n};\n\n")
    elseif(_kind STREQUAL "data")
        # HK_EXE_GLOBAL (the SDK built HK_EXE_GLOBALS_BY_REFERENCE) declares the global as a reference: the row's
        # decorated name with its type wrapped in an lvalue reference (AE<cv> <type> E<cv>). Its storage is a slot the
        # shim fills with the exe's address at load. An array (P[AB] without E) or a vtable (??_7) is not one.
        if(NOT _mangled MATCHES "^\\?.*@@[0-3]P[AB][^E]" AND _mangled MATCHES "^(\\?[^?].*@@[0-3])(.*)$")
            set(_refHead "${CMAKE_MATCH_1}")
            set(_refRest "${CMAKE_MATCH_2}")
            string(REGEX MATCH ".$" _cv "${_refRest}")
            if(_refRest MATCHES "^[PQRS]E")
                string(REGEX REPLACE "E.$" "" _refType "${_refRest}")
            else()
                string(REGEX REPLACE ".$" "" _refType "${_refRest}")
            endif()
            string(APPEND _refDefs "void* cb_ref_${_nr} = nullptr;\n")
            string(APPEND _shimAlt "#pragma comment(linker, \"/alternatename:${_refHead}AE${_cv}${_refType}E${_cv}=cb_ref_${_nr}\")\n")
            string(APPEND _refData "    { &engine::${_name}, &cb_ref_${_nr} },\n")
            math(EXPR _nr "${_nr}+1")
        endif()
        # A const global is copied at load. A mutable one stays unbound (a direct use fails to link) and gets
        # GlobalIdentity<&global> instead, for Live<&global>() to read the exe's variable through its pin.
        if(NOT _header OR NOT _cpp)
            continue()
        endif()
        # `header` may name several, `|`-separated: the declaring header, then whatever completes the type.
        string(REPLACE "|" ";" _headers "${_header}")
        if(NOT _mangled MATCHES "B$" AND _mangled MATCHES "^\\?")
            # A global the SDK declares is an HK_EXE_GLOBAL reference, bound above: code reads it by name, with no
            # Live<&global> (a reference's address is no constant).
            list(GET _headers 0 _h0)
            if(EXISTS "${SDK_ROOT}/Source/${_h0}" OR EXISTS "${SDK_ROOT}/Bethesda/${_h0}")
                set(_globalIdentity OFF)
            endif()
            if(NOT _globalIdentity)
                continue()
            endif()
            foreach(_h IN LISTS _headers)
                if(NOT "${_h}" IN_LIST _identityHeaders)
                    list(APPEND _identityHeaders "${_h}")
                endif()
            endforeach()
            set(_gkey "&::${_cpp}")
            if(_mangled MATCHES "^\\?[^?][^@]*(@[^@]+)*@@[01]")
                string(APPEND _keys "    static constexpr auto k${_nk} = &::${_cpp};\n")
                set(_gkey "kKeys::k${_nk}")
                math(EXPR _nk "${_nk}+1")
            endif()
            string(APPEND _globals "template <>
struct GlobalIdentity<${_gkey}> {
    static constexpr const engine::Symbol& symbol = engine::${_name};
};

")
            continue()
        endif()
        foreach(_h IN LISTS _headers)
            if(NOT "${_h}" IN_LIST _dataHeaders)
                list(APPEND _dataHeaders "${_h}")
            endif()
        endforeach()
        string(APPEND _shimDataDefs "alignas(alignof(decltype(::${_cpp}))) unsigned char cb_data_${_nd}[sizeof(::${_cpp})];\n")
        string(APPEND _shimAlt "#pragma comment(linker, \"/alternatename:${_mangled}=cb_data_${_nd}\")\n")
        string(APPEND _shimData "    { &engine::${_name}, cb_data_${_nd}, sizeof(::${_cpp}) },\n")
        math(EXPR _nd "${_nd}+1")
    elseif(_kind STREQUAL "tls")
        # A thread_local can't be aliased across modules: the plugin's own copy stands (MemoryContextScope's tag).
    else()
        message(FATAL_ERROR "engine.yaml: ${_name} has `mangled` but kind '${_kind}' (only func, data and tls are)")
    endif()
endforeach()

if(_n EQUAL 0)
    message(FATAL_ERROR "engine.yaml: no symbols parsed from ${REGISTRY_YAML}")
endif()
math(EXPR _counted "${_n}+${_skipped}")
if(NOT _counted EQUAL _expected)
    message(FATAL_ERROR "engine.yaml: parsed ${_counted} symbols but the files have ${_expected} '- {' entry lines")
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
"// GENERATED by cmake/EmbedEngineRegistry.cmake from src/platform/hooks/registry/engine.yaml -- DO NOT EDIT.
// The yaml is the single source; regenerated at build whenever it changes. Resolved via
// <platform/hooks/registry/Registry.h>.
#pragma once

#include <platform/hooks/registry/Symbol.h>

#include <cstddef>

namespace CB::platform::hooks::engine {

${_defs}
inline constexpr const Symbol* kAll[] = {
${_all}};

inline constexpr std::size_t kCount = ${_n};

} // namespace CB::platform::hooks::engine
")
_cb_write("${OUT_H}" "${_body}")

if(NOT DEFINED OUT_SHIM_ASM OR NOT DEFINED OUT_SHIM_CPP OR NOT DEFINED OUT_IDENTITY_H)
    return()
endif()

list(SORT _identityHeaders)
list(SORT _dataHeaders)
set(_identityIncludes "")
foreach(_h IN LISTS _identityHeaders)
    string(APPEND _identityIncludes "#include <${_h}>\n")
endforeach()
set(_dataIncludes "")
if(_dataHeaders)
    set(_dataIncludes "#include <Common/Base/hkBase.h>
")
endif()
foreach(_h IN LISTS _dataHeaders)
    string(APPEND _dataIncludes "#include <${_h}>\n")
endforeach()
if(_nf EQUAL 0)
    set(_shimSlots "cb_shim_slots QWORD 0
")
endif()
set(_nfa ${_nf})
if(_nfa EQUAL 0)
    set(_nfa 1)
endif()
set(_nra ${_nr})
if(_nra EQUAL 0)
    set(_nra 1)
    set(_refData "    { nullptr, nullptr },
")
endif()
if(_nf EQUAL 0)
    set(_implTable "    QWORD 0
")
    set(_stubTable "    QWORD 0
")
endif()
set(_nda ${_nd})
if(_nda EQUAL 0)
    set(_nda 1)
    set(_shimData "    { nullptr, nullptr, 0 },
")
endif()

_cb_write("${OUT_SHIM_ASM}"
"; GENERATED by cmake/EmbedEngineRegistry.cmake from src/platform/hooks/registry/engine.yaml -- DO NOT EDIT.
; The schema link shim: one jump stub per shimmed function. A slot starts at its miss stub, which resolves the
; address once (cb_shim_resolve) and jumps on with the caller's arguments untouched.
EXTERN cb_shim_resolve:PROC
${_implExterns}
.data
ALIGN 8
PUBLIC cb_shim_slots
${_shimSlots}
; Per function slot: its decorated name as the link resolved it (the plugin's compiled definition, or the stub).
ALIGN 8
PUBLIC cb_impl_table
cb_impl_table LABEL QWORD
${_implTable}
PUBLIC cb_stub_table
cb_stub_table LABEL QWORD
${_stubTable}
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
"// GENERATED by cmake/EmbedEngineRegistry.cmake from src/platform/hooks/registry/engine.yaml -- DO NOT EDIT.
// The schema link shim's C++ half: decorated-name bindings, the per-slot symbols, and the const data copies.
#include <platform/hooks/registry/Registry.h>
#include <platform/hooks/takeover/SchemaShim.h>

${_dataIncludes}
#include <cstddef>
#include <cstdint>
#include <cstring>

${_shimAlt}
extern \"C\" {
extern std::uintptr_t cb_shim_slots[${_nfa}];
${_shimDataDefs}${_refDefs}}

namespace CB::platform::hooks::detail {

const engine::Symbol* const kFuncs[${_nfa}] = {
${_shimFuncs}};
inline constexpr std::size_t kFuncCount = ${_nf};

const ShimData kData[${_nda}] = {
${_shimData}};
inline constexpr std::size_t kDataCount = ${_nd};

const ShimRef kRefs[${_nra}] = {
${_refData}};
inline constexpr std::size_t kRefCount = ${_nr};

const char* const kFuncMangled[${_nfa}] = {
${_shimMangled}};

const engine::Symbol* const* ShimFunctions() { return kFuncs; }
const char* const*           ShimFunctionMangled() { return kFuncMangled; }
std::size_t                 ShimFunctionCount() { return kFuncCount; }

} // namespace CB::platform::hooks::detail

extern \"C\" std::uintptr_t cb_shim_resolve(unsigned slot)
{
    return CB::platform::hooks::detail::ResolveShimSlot(slot, CB::platform::hooks::detail::kFuncs, CB::platform::hooks::detail::kFuncCount, cb_shim_slots);
}

void CB::platform::hooks::InstallSchemaShim()
{
    detail::CopyShimData(detail::kData, detail::kDataCount);
    detail::BindShimRefs(detail::kRefs, detail::kRefCount);
}
")

_cb_write("${OUT_IDENTITY_H}"
"// GENERATED by cmake/EmbedEngineRegistry.cmake from src/platform/hooks/registry/engine.yaml -- DO NOT EDIT.
// Identity<&fn> for every declared function engine.yaml binds (havok/ and platform/game): what Override<&fn> needs
// beyond the declaration's own type: its decorated name (the takeover cell's key), its registry pin (the entry
// detour's address otherwise) and the `this` the image passes.
#pragma once

#include <platform/hooks/registry/EngineRegistry.gen.h>

#include <Common/Base/Config/hkExtensionAccess.h>
${_identityIncludes}
namespace CB::platform::hooks { struct IdentityKeys; }

/// @brief Every key below, named from a specialization of the SDK's extension-access family: a hooked member may be
///        protected or private (MSVC access-checks even an explicit specialization's template arguments).
template <>
struct hkExtensionAccess<CB::platform::hooks::IdentityKeys> {
    static constexpr int kNone = 0;
${_keys}};

namespace CB::platform::hooks {

using kKeys = ::hkExtensionAccess<IdentityKeys>;

template <auto F>
struct Identity;

template <auto P>
struct GlobalIdentity;

${_identity}${_globals}} // namespace CB::platform::hooks
")
