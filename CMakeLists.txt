cmake_minimum_required(VERSION 3.20)
project(ClaudeUsageTracker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
endif()

find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base webview)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(ClaudeUsageTracker WIN32
    src/main.cpp
    src/MainFrame.cpp
    src/SettingsDialog.cpp
    src/UsageClient.cpp
    src/SettingsStore.cpp
    src/TrayIcon.cpp
    src/WebSession.cpp
    src/Logger.cpp
)

target_link_libraries(ClaudeUsageTracker PRIVATE
    wx::core
    wx::base
    wx::webview
    nlohmann_json::nlohmann_json
)

# WebView2Loader.dll is loaded dynamically by wx's Edge backend rather than
# statically imported, so vcpkg's automatic post-build DLL copying misses it.
find_file(WEBVIEW2LOADER_DLL WebView2Loader.dll
    PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin")
if(WEBVIEW2LOADER_DLL)
    add_custom_command(TARGET ClaudeUsageTracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WEBVIEW2LOADER_DLL}" "$<TARGET_FILE_DIR:ClaudeUsageTracker>")
endif()
