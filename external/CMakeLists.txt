# ==============================================================================
# Git Submodule Initialization
# ==============================================================================
# Check for .git file/directory to determine if submodule is initialized.
# When a submodule is initialized, git creates a .git file (or directory) in it.

set(EXTERNAL_SUBMODULES
    "arts"
    "ocr"
    "ocr-apps"
)

# Check which submodules need initialization
set(SUBMODULES_TO_INIT "")
foreach(SUBMODULE ${EXTERNAL_SUBMODULES})
    set(SUBMODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${SUBMODULE}")
    set(SUBMODULE_GIT "${SUBMODULE_PATH}/.git")

    if(NOT EXISTS "${SUBMODULE_GIT}")
        list(APPEND SUBMODULES_TO_INIT "external/${SUBMODULE}")
        message(STATUS "Submodule '${SUBMODULE}' not initialized (missing ${SUBMODULE_GIT})")
    endif()
endforeach()

# Initialize any missing submodules
if(SUBMODULES_TO_INIT)
    find_program(GIT_EXECUTABLE git)
    if(NOT GIT_EXECUTABLE)
        message(FATAL_ERROR
            "git executable not found. Cannot initialize submodules automatically.\n"
            "Please run: git submodule update --init --recursive")
    endif()

    message(STATUS "Initializing submodules: ${SUBMODULES_TO_INIT}")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive ${SUBMODULES_TO_INIT}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE SUBMODULE_RESULT
        OUTPUT_VARIABLE SUBMODULE_STDOUT
        ERROR_VARIABLE SUBMODULE_STDERR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    if(NOT SUBMODULE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Failed to initialize submodules (exit code ${SUBMODULE_RESULT}).\n"
            "stdout: ${SUBMODULE_STDOUT}\n"
            "stderr: ${SUBMODULE_STDERR}\n"
            "Please try manually: git submodule update --init --recursive")
    endif()

    message(STATUS "Submodules initialized successfully.")
endif()

# Verify all submodules are now present
foreach(SUBMODULE ${EXTERNAL_SUBMODULES})
    set(SUBMODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${SUBMODULE}")
    set(SUBMODULE_GIT "${SUBMODULE_PATH}/.git")

    if(NOT EXISTS "${SUBMODULE_GIT}")
        message(FATAL_ERROR
            "Submodule '${SUBMODULE}' still missing at ${SUBMODULE_PATH} after initialization.\n"
            "Please try manually: git submodule update --init --recursive external/${SUBMODULE}")
    endif()
endforeach()

# ==============================================================================
# Add Subdirectories
# ==============================================================================
add_subdirectory(arts ${CMAKE_BINARY_DIR}/external/arts)
