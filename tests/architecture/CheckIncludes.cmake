# Enforces the module dependency matrix (architecture document, section 2) on #include lines.
# Usage: cmake -DSOURCE_DIR=<repo>/src -P CheckIncludes.cmake
#
# For each module: the list of include prefixes it must never use.
# "juce" means any JUCE header (<juce_...>), "juce_gui" means JUCE GUI modules only.

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is not set")
endif()

set(upper_layers "serialization/;dsp/;assets/;engine/;analysis/;library/;app/;ui/")

set(forbidden_core          "juce_public;model/;${upper_layers}")
set(forbidden_model         "juce;serialization/;dsp/;assets/;engine/;analysis/;library/;app/;ui/")
set(forbidden_serialization "juce;dsp/;assets/;engine/;analysis/;library/;app/;ui/")
set(forbidden_dsp           "juce_gui;model/;serialization/;assets/;engine/;analysis/;library/;app/;ui/")
set(forbidden_assets        "juce_gui;model/;serialization/;dsp/;engine/;analysis/;library/;app/;ui/")
set(forbidden_engine        "juce_gui;serialization/;analysis/;library/;app/;ui/")
set(forbidden_analysis      "juce_gui;model/;serialization/;engine/;library/;app/;ui/")
set(forbidden_library       "juce_gui;model/;serialization/;dsp/;engine/;analysis/;app/;ui/")
set(forbidden_app           "juce_gui;ui/")
set(forbidden_ui            "")

set(violations "")

foreach(module core model serialization dsp assets engine analysis library app ui)
    file(GLOB_RECURSE files "${SOURCE_DIR}/${module}/*.h" "${SOURCE_DIR}/${module}/*.hpp"
                            "${SOURCE_DIR}/${module}/*.cpp" "${SOURCE_DIR}/${module}/*.in")
    foreach(file IN LISTS files)
        file(STRINGS "${file}" includes REGEX "^[ \t]*#[ \t]*include")
        get_filename_component(ext "${file}" EXT)
        foreach(line IN LISTS includes)
            foreach(rule IN LISTS forbidden_${module})
                set(bad FALSE)
                if(rule STREQUAL "juce")
                    if(line MATCHES "<juce_")
                        set(bad TRUE)
                    endif()
                elseif(rule STREQUAL "juce_public")
                    # core may use JUCE only privately, never in its headers.
                    if(line MATCHES "<juce_" AND (ext STREQUAL ".h" OR ext STREQUAL ".hpp"))
                        set(bad TRUE)
                    endif()
                elseif(rule STREQUAL "juce_gui")
                    if(line MATCHES "<juce_(gui_|graphics|opengl)")
                        set(bad TRUE)
                    endif()
                elseif(line MATCHES "[\"<]${rule}")
                    set(bad TRUE)
                endif()
                if(bad)
                    file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
                    string(STRIP "${line}" line)
                    list(APPEND violations "${rel}: '${line}' is not allowed in module '${module}'")
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " text)
    message(FATAL_ERROR "Module dependency violations:\n  ${text}")
endif()
message(STATUS "Module include rules: OK")
