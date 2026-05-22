cmake_minimum_required(VERSION 3.20)
project(aiassistant VERSION 1.0.0)
enable_language(RC)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(aiassistant SHARED
    src/main.cpp
    src/version.rc
    src/minhook/buffer.c
    src/minhook/hook.c
    src/minhook/trampoline.c
    src/minhook/hde/hde32.c
    src/minhook/hde/hde64.c
)

target_include_directories(aiassistant PRIVATE
    include
    src/minhook
)

set_target_properties(aiassistant PROPERTIES
    OUTPUT_NAME "aiassistant"
    SUFFIX ".asi"
)

target_compile_definitions(aiassistant PRIVATE
    _WIN32_WINNT=0x0601
    WINVER=0x0601
    PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

target_compile_options(aiassistant PRIVATE /W3)
