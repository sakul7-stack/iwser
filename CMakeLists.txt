cmake_minimum_required(VERSION 3.21)

project(iwser VERSION 1.0 LANGUAGES CXX)

# Product/window name shown to users.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# WebView2 SDK (downloaded from NuGet at configure time; it is a plain zip).
# We link WebView2LoaderStatic.lib so the result is a single self-contained
# .exe with no extra DLL to ship. The Chromium/Edge engine itself is the
# shared system WebView2 Runtime, so the binary stays tiny.
# ---------------------------------------------------------------------------
set(WEBVIEW2_VERSION "1.0.2903.40")
set(WEBVIEW2_DIR "${CMAKE_BINARY_DIR}/webview2")

if(NOT EXISTS "${WEBVIEW2_DIR}/build/native/include/WebView2.h")
    set(WEBVIEW2_NUPKG "${CMAKE_BINARY_DIR}/webview2.nupkg")
    message(STATUS "Downloading WebView2 SDK ${WEBVIEW2_VERSION} from NuGet...")
    file(DOWNLOAD
        "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}"
        "${WEBVIEW2_NUPKG}"
        SHOW_PROGRESS
        STATUS _dl_status)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "Failed to download WebView2 SDK: ${_dl_status}")
    endif()
    file(MAKE_DIRECTORY "${WEBVIEW2_DIR}")
    file(ARCHIVE_EXTRACT INPUT "${WEBVIEW2_NUPKG}" DESTINATION "${WEBVIEW2_DIR}")
endif()

set(WEBVIEW2_INCLUDE "${WEBVIEW2_DIR}/build/native/include")
set(WEBVIEW2_LIB "${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib")

add_executable(iwser WIN32
    src/main.cpp
    src/BrowserWindow.cpp
    src/TabManager.cpp
    src/Downloads.cpp
    src/Storage.cpp
    src/Theme.cpp
    src/Extensions.cpp
    src/app.rc
)

target_include_directories(iwser PRIVATE
    "${WEBVIEW2_INCLUDE}"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/assets"
)

target_link_libraries(iwser PRIVATE
    "${WEBVIEW2_LIB}"
    Dwmapi
    Comctl32
    Shlwapi
    Shell32
    Ole32
    Version
    Gdiplus
)

target_compile_definitions(iwser PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(iwser PRIVATE /W3 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
    # We embed our own DPI-aware manifest via app.rc, so disable the
    # linker-generated one (otherwise the two collide: CVT1100 duplicate MANIFEST).
    target_link_options(iwser PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
endif()
