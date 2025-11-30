if(NOT CRLIBM_LIBRARY OR NOT CRLIBM_HEADER_DIR)
    message(FATAL_ERROR "CRLIBM_LIBRARY or CRLIBM_HEADER_DIR not defined")
endif()

find_program(CRLIBM_MAKE_EXECUTABLE NAMES gmake make)
if(NOT CRLIBM_MAKE_EXECUTABLE)
    message(FATAL_ERROR "crlibm support requested but no make executable was found")
endif()

add_custom_command(
    OUTPUT ${CRLIBM_LIBRARY}
    COMMAND ${CMAKE_COMMAND} -E env ARCH=x86 ${CRLIBM_MAKE_EXECUTABLE} configure all check install
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building crlibm-1.0beta4 for x86"
    VERBATIM
)

add_custom_target(crlibm_build DEPENDS ${CRLIBM_LIBRARY})

add_library(crlibm INTERFACE)

target_link_libraries(crlibm INTERFACE ${CRLIBM_LIBRARY})
target_include_directories(crlibm INTERFACE ${CRLIBM_HEADER_DIR})

add_dependencies(crlibm crlibm_build)
