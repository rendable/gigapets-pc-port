cmake_minimum_required(VERSION 3.15)
project(GigaPetsPC)

set(CMAKE_CXX_STANDARD 20)

# Statically link the MSVC runtime so the exe runs without the Visual C++
# Redistributable. Must be set before FetchContent so raylib matches.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
)
FetchContent_MakeAvailable(raylib)

set(SOURCES
    src/main.cpp
    src/unsp.cpp
    src/unsp_fxxx.cpp
    src/unsp_exxx.cpp
    src/unsp_other.cpp
    src/unsp_extended.cpp
    src/unsp_jumps.cpp
    src/app.rc
)

add_executable(GigaPetsPC WIN32 ${SOURCES})
target_include_directories(GigaPetsPC PRIVATE src)
if(MSVC)
    # WIN32 selects the windowed subsystem; keep the normal CRT startup so the
    # plain int main() is still the entry point.
    set_target_properties(GigaPetsPC PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

target_link_libraries(GigaPetsPC PRIVATE raylib user32)

# main.cpp loads resources/ relative to the exe, so copy it next to the build.
add_custom_command(TARGET GigaPetsPC POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:GigaPetsPC>/resources"
    COMMENT "Copying resources/ next to the built exe"
)
