cmake_minimum_required(VERSION 3.24)
project(MiniBar VERSION 1.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CRT statica: eseguibile singolo, nessun VC Redist (vincolo portable).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Si ottimizza per dimensione (/O1), non per velocita': togliamo il /O2 che
# CMake mette di default in Release, altrimenti il compilatore avvisa.
string(REPLACE "/O2" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

add_executable(minibar WIN32
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/smtc.cpp
    src/render.cpp
    src/window.cpp
    src/appbar.cpp
    src/foreground.cpp
    src/player.cpp
    src/startup.cpp
    src/volume.cpp
    src/minibar.rc
)

target_compile_definitions(minibar PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

target_compile_options(minibar PRIVATE
    /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus
    $<$<CONFIG:Release>:/O1 /Oi /Gy /Gw /GL /Zc:inline>
)

target_link_options(minibar PRIVATE
    /MANIFEST:NO   # il manifest Per-Monitor V2 arriva da minibar.rc
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO>
)

# Tutto dal Windows SDK: nessuna dipendenza esterna.
target_link_libraries(minibar PRIVATE
    d2d1 dwrite windowscodecs
    windowsapp shlwapi shell32 user32 gdi32 ole32 dwmapi advapi32 version
)

# L'app è portable: l'eseguibile finisce accanto all'INI, sotto dist\.
set_target_properties(minibar PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist"
)
