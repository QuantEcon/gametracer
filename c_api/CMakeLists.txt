cmake_minimum_required(VERSION 3.15)
project(gametracer_c_api LANGUAGES CXX)

include(GNUInstallDirs)

# C++11 requirement (Phase B)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Upstream core uses GNU-style VLAs; enable GNU extensions => gnu++11 on GCC/Clang.
set(CMAKE_CXX_EXTENSIONS ON)

add_library(gametracer SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/gametracer_c_api.cpp

    # Core sources live at repo root; referenced via ../...
    ${CMAKE_CURRENT_SOURCE_DIR}/../cmatrix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../gnm.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../gnmgame.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../ipa.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../nfgame.cc
)

target_include_directories(gametracer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Ensure Windows exports are emitted (header checks GAMETRACER_BUILDING_DLL)
target_compile_definitions(gametracer PRIVATE GAMETRACER_BUILDING_DLL=1)

# Force the installed filename to be libgametracer.* on ALL platforms (incl Windows DLL)
set_target_properties(gametracer PROPERTIES
    OUTPUT_NAME "gametracer"
    PREFIX "lib"
)

# Visibility hygiene on non-Windows (exports still controlled by GAMETRACER_API)
if(NOT WIN32)
    target_compile_options(gametracer PRIVATE -fvisibility=hidden)
endif()

# Some platforms/toolchains may require explicit libm
if(UNIX AND NOT APPLE)
    target_link_libraries(gametracer PRIVATE m)
endif()

# Install: .so/.dylib -> lib, .dll -> bin
install(TARGETS gametracer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install the one public header
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/gametracer_c_api.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install the license file from repo root
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../COPYING
    DESTINATION share/licenses/gametracer
)
