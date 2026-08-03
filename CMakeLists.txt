cmake_minimum_required(VERSION 3.16)
project(NexusUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "Este proyecto usa Win32 + DirectX11: solo compila en Windows (Visual Studio).")
endif()

# ------------------------------------------------------------
# Descarga Dear ImGui automaticamente (no hace falta instalarlo a mano)
# ------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9
)
FetchContent_MakeAvailable(imgui)

add_executable(NexusUI WIN32
    src/main.cpp
    src/theme.cpp
    src/processes.cpp      
    src/processes.h 
    src/resource.rc # <--- AGREGADO: archivo de procesos
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(NexusUI PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    src
)

target_link_libraries(NexusUI PRIVATE 
    d3d11 
    dxgi 
    d3dcompiler 
    dwmapi
    shell32    # <--- AGREGADO: para SHGetFileInfoW y funciones de shell
    gdi32      # <--- AGREGADO: para operaciones con iconos
    psapi      # <--- AGREGADO: para QueryFullProcessImageNameW
)