cmake_minimum_required(VERSION 3.15)
project(RewardTesterPlugin VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(RS_KIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(RS_FRAMEWORK_ROOT "" CACHE PATH "Root of a GigaLearnCPP checkout (provides RLGymCPP, RocketSim and nlohmann/json)")
set(BAKKESMOD_SDK_DIR "$ENV{APPDATA}/bakkesmod/bakkesmod/bakkesmodsdk" CACHE PATH "BakkesMod SDK folder")

if(NOT RS_FRAMEWORK_ROOT)
    message(FATAL_ERROR "Set RS_FRAMEWORK_ROOT to the root of a GigaLearnCPP checkout")
endif()

include_directories(
    "${BAKKESMOD_SDK_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RS_KIT_ROOT}"
    "${RS_FRAMEWORK_ROOT}/libsrc/json"
    "${RS_FRAMEWORK_ROOT}/RLGymCPP/src"
    "${RS_FRAMEWORK_ROOT}/RLGymCPP/src/RLGymCPP/Gamestates"
    "${RS_FRAMEWORK_ROOT}/RLGymCPP/src/RLGymCPP/BasicTypes"
    "${RS_FRAMEWORK_ROOT}/RLGymCPP/RocketSim/src"
    "${RS_FRAMEWORK_ROOT}/src/public"
)

link_directories("${BAKKESMOD_SDK_DIR}/lib")

add_library(RewardTesterPlugin SHARED
    RewardTesterPlugin.cpp
    RewardTesterPlugin.h
    RewardClient.cpp
    RewardClient.h
    ${RS_KIT_ROOT}/rlpr/RLPRExporter.cpp
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_additions.cpp
    imgui/imgui_rangeslider.cpp
    imgui/imgui_searchablecombo.cpp
    imgui/imgui_timeline.cpp
)

set_source_files_properties(
    ${RS_KIT_ROOT}/rlpr/RLPRExporter.cpp
    PROPERTIES COMPILE_DEFINITIONS RLPR_IMPORT_ONLY
)

target_link_libraries(RewardTesterPlugin PRIVATE
    pluginsdk
    ws2_32
)

target_compile_definitions(RewardTesterPlugin PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
