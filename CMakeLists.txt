cmake_minimum_required(VERSION 3.14)
project(YAP CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES
	${SOURCES}
	src/main.cpp
	src/yap.cpp
	src/extract.cpp
	src/create.cpp
	)

set(HEADERS
	${HEADERS}
	include/yap.h
	)

add_executable(YAP ${SOURCES} ${HEADERS})

find_package(Qt6 COMPONENTS Core REQUIRED)

# argparse
add_subdirectory(external/argparse "${CMAKE_CURRENT_BINARY_DIR}/external/argparse" EXCLUDE_FROM_ALL)
target_include_directories(YAP PRIVATE ${ROOT}\\external\\argparse "${CMAKE_CURRENT_BINARY_DIR}/external/argparse")

# GameDataStream
add_subdirectory(external/GameDataStream "${CMAKE_CURRENT_BINARY_DIR}/external/GameDataStream" EXCLUDE_FROM_ALL)
target_include_directories(YAP PRIVATE ${ROOT}\\external\\GameDataStream "${CMAKE_CURRENT_BINARY_DIR}/external/GameDataStream")

# libdeflate
add_subdirectory(external/libdeflate "${CMAKE_CURRENT_BINARY_DIR}/external/libdeflate" EXCLUDE_FROM_ALL)
target_include_directories(YAP PRIVATE ${ROOT}\\external\\libdeflate "${CMAKE_CURRENT_BINARY_DIR}/external/libdeflate")

# yaml-cpp
add_subdirectory(external/yaml-cpp "${CMAKE_CURRENT_BINARY_DIR}/external/yaml-cpp" EXCLUDE_FROM_ALL)
target_include_directories(YAP PRIVATE ${ROOT}\\external\\yaml-cpp "${CMAKE_CURRENT_BINARY_DIR}/external/yaml-cpp")

# YAP includes
target_include_directories(YAP PRIVATE "${ROOT}/include")

target_link_libraries(YAP PRIVATE argparse GameDataStream libdeflate_static yaml-cpp Qt6::Core)

# VS stuff
set_property(DIRECTORY ${ROOT} PROPERTY VS_STARTUP_PROJECT YAP)
source_group(TREE ${ROOT} FILES ${SOURCES} ${HEADERS})

if (WIN32)
	add_custom_command(TARGET YAP POST_BUILD
		COMMAND Qt6::windeployqt ARGS $<TARGET_FILE:YAP>
	)
endif()
