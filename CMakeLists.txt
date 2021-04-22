cmake_minimum_required(VERSION 2.8)
project(net)

# version
execute_process(
    OUTPUT_VARIABLE GIT_BRANCH
    COMMAND git symbolic-ref --short -q HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
#string(TIMESTAMP TODAY "%Y%m%d%H%I")
#set(BUILD_VERSION "${TODAY}")
#configure_file(
#	"${CMAKE_CURRENT_SOURCE_DIR}/version/version_config.h.in"
#	"${CMAKE_CURRENT_SOURCE_DIR}/version/version_config.h"
#)

set(CMAKE_CXX_STANDARD 17)

# option
set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
set(LIBPROTOBUF_PLATFORM "ubuntu")
option(COMPILE_PROTOBUF "Compile protobuf" OFF)

# def
add_definitions(-DASIO_STANDALONE)
add_definitions(-DASIO_NO_DEPRECATED)
add_definitions(-DUSE_OPENSSL)
add_definitions(-D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING)

# platform
if(UNIX)
    include(cmake/linux.cmake)
	# 打开ASAN内存检测
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libasan")
	#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall -Wextra")
        MESSAGE(STATUS "Open memory check")
else()
    include(cmake/windows.cmake)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# include
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/3rd")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/net")


#include_directories("${MYSQL_INCLUDE_DIR}")       

#add_definitions(-DLUAPP_SCRIPT_PATH="${PROJECT_SOURCE_DIR}/script")                       

# sub
#add_subdirectory(3rd)
add_subdirectory(demo)
