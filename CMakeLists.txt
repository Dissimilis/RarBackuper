cmake_minimum_required(VERSION 3.20)
project(RarBackuper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CORE_SOURCES
    src/core/Text.cpp
    src/core/ArchiveName.cpp
    src/core/ExcludeRules.cpp
    src/core/RarCommandLine.cpp
    src/core/RarOutputParser.cpp
    src/core/RarExitCodes.cpp
    src/core/SettingsModel.cpp
    src/core/DetectorCatalog.cpp
)

set(ENGINE_SOURCES
    src/engine/RarDiscovery.cpp
    src/engine/RarRunner.cpp
    src/engine/MetaCollector.cpp
    src/engine/Settings.cpp
    src/engine/detectors/SystemInfo.cpp
    src/engine/detectors/DriveInventory.cpp
    src/engine/detectors/Bookmarks.cpp
    src/engine/detectors/ImportantStuff.cpp
)

set(GUI_SOURCES
    src/win/App.cpp
    src/win/MainWindow.cpp
    src/win/ExcludeDialog.cpp
    src/win/Logger.cpp
    src/cli/CliMain.cpp
)

add_executable(RarBackuper WIN32
    ${GUI_SOURCES}
    ${ENGINE_SOURCES}
    ${CORE_SOURCES}
    res/app.rc
)
target_include_directories(RarBackuper PRIVATE src third_party)
target_compile_definitions(RarBackuper PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(RarBackuper PRIVATE comctl32 shell32 ole32 oleaut32 uuid advapi32 shlwapi wbemuuid)

if(MINGW)
    target_link_options(RarBackuper PRIVATE -static -municode $<$<CONFIG:Release>:-s>)
endif()
if(MSVC)
    target_link_options(RarBackuper PRIVATE /ENTRY:wWinMainCRTStartup)
endif()

# The distributable app exe lands in <repo>/dist; the bundled WinRAR-x64
# folder is copied beside it (when present) so dist\ is ready to ship/run.
set_target_properties(RarBackuper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist)
if(EXISTS ${CMAKE_SOURCE_DIR}/WinRAR-x64/Rar.exe)
    add_custom_command(TARGET RarBackuper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                ${CMAKE_SOURCE_DIR}/WinRAR-x64 ${CMAKE_SOURCE_DIR}/dist/WinRAR-x64
        COMMENT "Copying bundled WinRAR-x64 into dist/")
endif()

add_executable(rarbackuper_tests
    tests/test_main.cpp
    tests/test_archive_name.cpp
    tests/test_exclude_rules.cpp
    tests/test_command_line.cpp
    tests/test_output_parser.cpp
    tests/test_exit_codes.cpp
    tests/test_settings_model.cpp
    tests/test_detector_catalog.cpp
    tests/test_discovery_settings.cpp
    tests/test_integration_rar.cpp
    ${CORE_SOURCES}
    ${ENGINE_SOURCES}
)
target_include_directories(rarbackuper_tests PRIVATE src third_party)
target_compile_definitions(rarbackuper_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(rarbackuper_tests PRIVATE shlwapi ole32 oleaut32 uuid advapi32 wbemuuid)

if(MINGW)
    target_link_options(rarbackuper_tests PRIVATE -static)
endif()
