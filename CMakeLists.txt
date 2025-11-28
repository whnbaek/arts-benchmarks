cmake_minimum_required(VERSION 3.16)

project(arts_benchmarks LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to 'Release' as none was specified")
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

if(NOT DEFINED CMAKE_INSTALL_PREFIX OR CMAKE_INSTALL_PREFIX STREQUAL "" OR CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/.install" CACHE PATH "Install path prefix" FORCE)
	set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT FALSE)
endif()

option(ARTS_AUTO_INIT_SUBMODULE
	"Automatically initialize the external/arts submodule when it is missing"
	ON
)

set(ARTS_SUBMODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/arts")
set(ARTS_SUBMODULE_ENTRY "${ARTS_SUBMODULE_PATH}/CMakeLists.txt")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ensure the ARTS submodule is checked out before attempting to add it.
if(NOT EXISTS "${ARTS_SUBMODULE_ENTRY}")
	if(NOT ARTS_AUTO_INIT_SUBMODULE)
		message(FATAL_ERROR
			"arts submodule not found at ${ARTS_SUBMODULE_PATH}. "
			"Run 'git submodule update --init --recursive external/arts' "
			"or configure with -DARTS_AUTO_INIT_SUBMODULE=ON.")
	endif()

	find_program(GIT_EXECUTABLE git)
	if(NOT GIT_EXECUTABLE)
		message(FATAL_ERROR
			"git executable not found. Cannot initialize the arts submodule automatically.")
	endif()

	message(STATUS "arts submodule missing; initializing via git submodule update ...")
	execute_process(
		COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive external/arts
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		RESULT_VARIABLE ARTS_SUBMODULE_RESULT
		OUTPUT_VARIABLE ARTS_SUBMODULE_STDOUT
		ERROR_VARIABLE ARTS_SUBMODULE_STDERR
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_STRIP_TRAILING_WHITESPACE
	)

	if(NOT ARTS_SUBMODULE_RESULT EQUAL 0)
		message(FATAL_ERROR
			"Failed to initialize ARTS submodule (exit code ${ARTS_SUBMODULE_RESULT}).\n"
			"stdout: ${ARTS_SUBMODULE_STDOUT}\n"
			"stderr: ${ARTS_SUBMODULE_STDERR}")
	endif()

	message(STATUS "ARTS submodule initialized successfully.")
endif()

if(NOT EXISTS "${ARTS_SUBMODULE_ENTRY}")
	message(FATAL_ERROR
		"ARTS submodule still missing at ${ARTS_SUBMODULE_PATH} after initialization attempt.")
endif()

# Build the arts project directly from this superbuild.
add_subdirectory(${ARTS_SUBMODULE_PATH} ${CMAKE_BINARY_DIR}/external/arts)

add_subdirectory(ocr)
add_subdirectory(apps)
