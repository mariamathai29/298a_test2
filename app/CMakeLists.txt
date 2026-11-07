cmake_minimum_required(VERSION 3.22)
project(AdaptiveFilterExplorer VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Third-party code: all under permissive licences (see third_party/licenses).
#   Dear ImGui (MIT), stb_image_write (MIT or public domain), nlohmann/json (MIT),
#   SDL2 (zlib), DejaVu fonts.
# Offline, point FETCHCONTENT_SOURCE_DIR_<NAME> at copies already fetched.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.tar.gz
    URL_HASH SHA256=3872a5f90df78fced023c1945f4466b654fd74573370b77b17742149763a7a7c)
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
foreach(dep imgui stb json)
    FetchContent_GetProperties(${dep})
    if(NOT ${dep}_POPULATED)
        FetchContent_Populate(${dep})
    endif()
endforeach()

find_package(SDL2 REQUIRED)

add_executable(afx_embed tools/embed.cpp)
set(FONT_FILES DejaVuSans DejaVuSans-Bold DejaVuSerif DejaVuSerif-Bold DejaVuSansMono DejaVuSansMono-Bold)
set(FONT_PATHS)
foreach(f ${FONT_FILES})
    list(APPEND FONT_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/${f}.ttf)
endforeach()
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Fonts.cpp
    COMMAND afx_embed ${CMAKE_CURRENT_BINARY_DIR}/Fonts.cpp ${FONT_PATHS}
    DEPENDS afx_embed ${FONT_PATHS}
    COMMENT "Embedding the fonts")

# Core: no GUI.
add_library(afx_core STATIC
    Source/Signals.cpp
    Source/Adaptive.cpp
    Source/Chip.cpp
    Source/Scene.cpp
    Source/Simulator.cpp
    Source/Sonify.cpp
    Source/Project.cpp
    Source/Log.cpp)
target_include_directories(afx_core PUBLIC Source)
target_include_directories(afx_core SYSTEM PUBLIC ${stb_SOURCE_DIR} ${json_SOURCE_DIR}/include)
target_compile_options(afx_core PRIVATE -Wall -Wextra -Wshadow -Wno-missing-field-initializers)
set_source_files_properties(Source/Adaptive.cpp Source/Chip.cpp Source/Simulator.cpp Source/Signals.cpp Source/Sonify.cpp PROPERTIES COMPILE_OPTIONS "-O3")

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC SDL2::SDL2)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

add_library(afx_ui STATIC
    Source/Ui.cpp
    Source/Panels.cpp
    Source/Diagram.cpp
    Source/Plots.cpp
    Source/Tune.cpp
    Source/Views.cpp
    Source/FileDialog.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/Fonts.cpp)
target_link_libraries(afx_ui PUBLIC afx_core imgui)
target_compile_options(afx_ui PRIVATE -Wall -Wextra -Wshadow -Wno-missing-field-initializers)

add_executable(AdaptiveFilterExplorer Source/Main.cpp)
target_link_libraries(AdaptiveFilterExplorer PRIVATE afx_ui)
target_compile_options(AdaptiveFilterExplorer PRIVATE -Wall -Wextra -Wshadow)

add_executable(afx_selftest Source/SelfTest.cpp)
target_link_libraries(afx_selftest PRIVATE afx_core)
target_compile_options(afx_selftest PRIVATE -Wall -Wextra -Wshadow)

# The chip's test vectors: the bit-exact model's stimulus and expected frames, for tb/chip_tb.py to replay
# through lms_model.py and the RTL / routed netlist.
add_executable(afx_chipvec Source/ChipVectors.cpp)
target_link_libraries(afx_chipvec PRIVATE afx_core)
target_compile_options(afx_chipvec PRIVATE -Wall -Wextra -Wshadow)

add_executable(afx_shot Source/Shot.cpp)
target_link_libraries(afx_shot PRIVATE afx_ui)
target_compile_options(afx_shot PRIVATE -Wall -Wextra -Wshadow)

# The promotional video: the interface driven by a script, off-screen, with the sonification (see promo/make_promo.sh).
add_executable(afx_promo Source/Promo.cpp)
target_link_libraries(afx_promo PRIVATE afx_ui)
target_compile_options(afx_promo PRIVATE -Wall -Wextra -Wshadow -Wno-missing-field-initializers)

include(GNUInstallDirs)
install(TARGETS AdaptiveFilterExplorer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY third_party/licenses DESTINATION ${CMAKE_INSTALL_DOCDIR})
