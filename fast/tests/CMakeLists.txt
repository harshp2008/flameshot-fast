# Standalone test project: compiles the ScreenCast grabber sources directly so
# the tests do not need the rest of Flameshot. Built by fast/scripts/test.sh.
cmake_minimum_required(VERSION 3.22)
project(flameshot_fast_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui DBus Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

set(SRC ${CMAKE_CURRENT_LIST_DIR}/../../src/utils)

add_library(screencast_under_test STATIC
    ${SRC}/screencastgrabber.cpp
    ${SRC}/screencastgrabber.h
    ${SRC}/request.cpp
    ${SRC}/request.h)
target_include_directories(screencast_under_test PUBLIC ${SRC})
target_compile_definitions(screencast_under_test PUBLIC FLAMESHOT_HAVE_PIPEWIRE=1)
target_link_libraries(screencast_under_test PUBLIC
    Qt6::Core Qt6::Gui Qt6::DBus PkgConfig::PIPEWIRE)

add_executable(tst_pure tst_pure.cpp)
target_link_libraries(tst_pure PRIVATE screencast_under_test Qt6::Test)

add_executable(flow_test flow_test.cpp)
target_link_libraries(flow_test PRIVATE screencast_under_test)

# Test producer: a PipeWire video source used with a private PipeWire daemon
add_executable(pw_source pw_source.cpp)
target_link_libraries(pw_source PRIVATE PkgConfig::PIPEWIRE)
