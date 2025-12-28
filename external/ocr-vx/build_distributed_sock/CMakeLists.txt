cmake_minimum_required (VERSION 2.6)
project (ocr_vdm_sock)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ".")
#set(CMAKE_C_COMPILER "icc")
#set(CMAKE_CXX_COMPILER "icc")
#find_package(TBB REQUIRED)

find_file(RasPi bcm_host.h PATHS /opt/vc/include/ NO_DEFAULT_PATH)
if(RasPi)
  add_definitions(-DTBB_USE_GCC_BUILTINS=1)
endif()

include_directories (../include/ocr ../include/common ../include/distributed ../include/apps ../include/libs) 
link_directories(${TBB_LIBRARY_DIRS})
add_definitions(-DOCR_USE_SOCK -DOCR_ENABLE_EDT_NAMING)
add_definitions(-DOCR_TYPE_H=vdm.h)
if(UNIX)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
#    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -pg")# option "-pg" generates data for gprof
endif()
#add_definitions(-std=c++11)

file(MAKE_DIRECTORY log)

add_library(ocr_vdm_sock ../src/threadqueue.cpp ../src/distributed/ocr_distributed.cpp ../src/distributed/ocr_distributed_main.cpp ../src/distributed/command_processing.cpp ../src/distributed/communication.cpp ../src/distributed/ocr_api_impl.cpp ../src/ocr_log.cpp)

macro (add_executable _name)
    # invoke built-in add_executable
    _add_executable(${ARGV})
    if (TARGET ${_name})
        target_link_libraries(${_name} ocr_vdm_sock tbb tbbmalloc pthread)
    endif()
endmacro()

add_executable (hello ../src/apps/helloworld.cpp)
add_executable (api_tests ../src/apps/api_tests.cpp)
add_executable (seismic ../src/apps/seismic.cpp)
add_executable (graph500 ../src/apps/graph500.cpp)
add_executable (spmd ../src/libs/spmd.cpp ../src/apps/spmd_test.cpp)
