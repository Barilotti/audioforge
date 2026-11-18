# af_add_module(<name>
#     [SOURCES <files...>]
#     [PUBLIC_DEPS <targets...>]
#     [PRIVATE_DEPS <targets...>])
#
# Creates the library target af_<name> (alias AudioForge::<name>) for the module in src/<name>.
# Headers are included relative to src/, e.g. #include "core/Version.h".
# A module without sources yet is an INTERFACE library, so the dependency graph from the
# architecture document (section 2) exists from the first commit.
#
# Dependency direction is enforced in two ways: targets only link what the matrix allows, and
# tests/architecture/CheckIncludes.cmake rejects forbidden #include lines.

function(af_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
    set(target af_${name})

    if(ARG_SOURCES)
        add_library(${target} STATIC ${ARG_SOURCES})
        target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/src")
        target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
        target_compile_features(${target} PUBLIC cxx_std_20)
        af_set_warnings(${target})
        set_target_properties(${target} PROPERTIES FOLDER "Modules")
    else()
        if(ARG_PRIVATE_DEPS)
            message(FATAL_ERROR "Module ${name} has no sources and cannot have PRIVATE_DEPS")
        endif()
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE "${PROJECT_SOURCE_DIR}/src")
        target_link_libraries(${target} INTERFACE ${ARG_PUBLIC_DEPS})
        target_compile_features(${target} INTERFACE cxx_std_20)
    endif()

    add_library(AudioForge::${name} ALIAS ${target})
endfunction()
