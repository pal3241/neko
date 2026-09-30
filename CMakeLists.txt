cmake_minimum_required(VERSION 3.20)
project(NaikoExecutor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── NaikoCore.dll ──────────────────────────────────────────────────────────────
add_library(NaikoCore SHARED
    core/dllmain.cpp
)

target_include_directories(NaikoCore PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/modules
    ${CMAKE_SOURCE_DIR}/offsets
    ${CMAKE_SOURCE_DIR}/updater
)

target_link_libraries(NaikoCore PRIVATE
    winhttp
)

target_compile_definitions(NaikoCore PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# optimise release builds
target_compile_options(NaikoCore PRIVATE
    $<$<CONFIG:Release>:/O2 /GL>
)
target_link_options(NaikoCore PRIVATE
    $<$<CONFIG:Release>:/LTCG>
)

set_target_properties(NaikoCore PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# ── injector.exe ───────────────────────────────────────────────────────────────
add_executable(injector injector/injector.cpp)
target_compile_definitions(injector PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(injector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
