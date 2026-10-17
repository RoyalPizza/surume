cmake_minimum_required(VERSION 3.20)
project(Gitaluna CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# All binaries go to bin/ regardless of config
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY          "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG    "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE  "${CMAKE_SOURCE_DIR}/bin")

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third-party/imgui")

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)

set(APP_SOURCES
    src/main.cpp
    src/App.cpp
    src/Git.cpp
    src/Ui.cpp
    src/Log.cpp
    src/platform_win32.cpp
    src/Persistence.cpp
    "${CMAKE_SOURCE_DIR}/third-party/tomlc17/src/tomlc17.c"
)

set(COMMON_INCLUDES
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third-party/tomlc17/src"
)

set(COMMON_LIBS d3d11 dxgi ole32 uuid)

# Suppress MSVC warnings from third-party C code we don't control
if(MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/third-party/tomlc17/src/tomlc17.c"
        PROPERTIES COMPILE_FLAGS "/W0 /wd4996 /D_CRT_SECURE_NO_WARNINGS"
    )
endif()

# --- Release target ---
add_executable(gitaluna WIN32 ${APP_SOURCES} ${IMGUI_SOURCES})
target_include_directories(gitaluna PRIVATE ${COMMON_INCLUDES})
target_link_libraries(gitaluna PRIVATE ${COMMON_LIBS})
target_compile_definitions(gitaluna PRIVATE UNICODE _UNICODE NDEBUG)
if(MSVC)
    target_compile_options(gitaluna PRIVATE /O2 /W3 /MP /MD)
endif()

# --- Debug target ---
add_executable(gitaluna_debug WIN32 ${APP_SOURCES} ${IMGUI_SOURCES})
target_include_directories(gitaluna_debug PRIVATE ${COMMON_INCLUDES})
target_link_libraries(gitaluna_debug PRIVATE ${COMMON_LIBS})
target_compile_definitions(gitaluna_debug PRIVATE UNICODE _UNICODE GITALUNA_DEBUG _DEBUG)
if(MSVC)
    target_compile_options(gitaluna_debug PRIVATE /Od /Zi /RTC1 /W3 /MP /MDd)
    target_link_options(gitaluna_debug PRIVATE /DEBUG)
endif()
