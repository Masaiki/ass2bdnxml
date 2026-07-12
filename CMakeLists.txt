cmake_minimum_required(VERSION 3.20)
project(ass2bdnxml LANGUAGES C)

set(CMAKE_C_STANDARD 99)

include (TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
if(IS_BIG_ENDIAN)
    add_compile_definitions("BE_ARCH")
else()
    add_compile_definitions("LE_ARCH")
endif()

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
# MinGW provides getopt_long via libmingwex; only MSVC needs the vcpkg port.
if(WIN32 AND NOT MINGW)
    find_package(getopt CONFIG REQUIRED)
endif()

find_package(PkgConfig REQUIRED)
PKG_CHECK_MODULES(LIBASS REQUIRED libass>=0.15.0)

set(SOURCES
    auto_split.c
    ass2bdnxml.c
    palletize.c
    sort.c
    sup.c)

add_executable(ass2bdnxml ${SOURCES})

target_include_directories(ass2bdnxml PRIVATE ${LIBASS_INCLUDE_DIRS})
target_link_libraries(ass2bdnxml PRIVATE ZLIB::ZLIB PNG::PNG ${LIBASS_LINK_LIBRARIES})
if(WIN32 AND NOT MINGW)
    target_link_libraries(ass2bdnxml PRIVATE $<IF:$<TARGET_EXISTS:getopt::getopt_shared>,getopt::getopt_shared,getopt::getopt_static>)
endif()

include(CTest)
if(BUILD_TESTING)
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh)
    if(POWERSHELL_EXECUTABLE)
        add_test(
            NAME issue-6-timing
            COMMAND ${POWERSHELL_EXECUTABLE} -NoProfile -File
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-issue-6-timing.ps1
                -Executable $<TARGET_FILE:ass2bdnxml>)
    endif()
endif()
