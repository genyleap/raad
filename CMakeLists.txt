# Copyright (C) 2026 Genyleap LLC.
# SPDX-License-Identifier: LicenseRef-Geny-Commercial OR MIT

cmake_minimum_required(VERSION 3.28)

# -------------------------
# Project Definition
# -------------------------
set(PROJECT_NAME "Raad")
set(PROJECT_REAL_NAME "Raad")
set(APP_NAME "${PROJECT_REAL_NAME}")

set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION} LANGUAGES CXX)

if(POLICY CMP0155)
    cmake_policy(SET CMP0155 NEW)
endif()

set(RAAD_VENDOR "Genyleap")
set(RAAD_PACKAGE_CONTACT "info@genyleap.com")
set(RAAD_PACKAGE_SUMMARY "RAAD Internet Download Manager")
set(RAAD_COPYRIGHT "Copyright 2026 by Genyleap LLC")
set(RAAD_BUNDLE_IDENTIFIER "com.genyleap.raad")
set(RAAD_MACOS_ICON_FILE "Raad.icns")
set(RAAD_MACOS_ICON_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/Raad.icns")
set(RAAD_WINDOWS_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/Raad.ico")
set(RAAD_LINUX_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/Raad.png")
set(RAAD_MACOS_DEPLOYMENT_TARGET "15.1")
set(RAAD_MACOS_INFO_PLIST_IN "${CMAKE_CURRENT_SOURCE_DIR}/properties/macOS/Info.plist.in")
set(RAAD_MACOS_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/generated/Info.plist")
set(RAAD_WINDOWS_RC_IN "${CMAKE_CURRENT_SOURCE_DIR}/properties/Windows/app.rc")
set(RAAD_WINDOWS_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/raad_app.rc")
set(RAAD_RC_FILE_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(RAAD_RC_PRODUCT_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")

if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${RAAD_MACOS_DEPLOYMENT_TARGET}" CACHE STRING "Minimum macOS version")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(TO_NATIVE_PATH "${RAAD_WINDOWS_ICON_ICO}" RAAD_WINDOWS_ICON_ICO_RC)
configure_file("${RAAD_MACOS_INFO_PLIST_IN}" "${RAAD_MACOS_INFO_PLIST}" @ONLY)
configure_file("${RAAD_WINDOWS_RC_IN}" "${RAAD_WINDOWS_RC}" @ONLY)

# -------------------------
# macOS SDK (needed for Homebrew LLVM)
# -------------------------
if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE RAAD_MACOS_SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(RAAD_MACOS_SDK)
        set(CMAKE_OSX_SYSROOT "${RAAD_MACOS_SDK}" CACHE PATH "macOS SDK" FORCE)
    endif()
endif()

# -------------------------
# Modules (C++20) setup
# -------------------------
option(RAAD_USE_MODULES "Enable C++20 modules" ON)
option(RAAD_LLVM_AUTO_SCAN "Auto-detect LLVM/clang for modules" ON)
set(RAAD_MODULE_CACHE "${CMAKE_BINARY_DIR}/raad-module-cache")
file(MAKE_DIRECTORY "${RAAD_MODULE_CACHE}")

set(RAAD_LLVM_HINTS "")
if(RAAD_LLVM_AUTO_SCAN)
    if(DEFINED ENV{LLVM_ROOT})
        list(APPEND RAAD_LLVM_HINTS "$ENV{LLVM_ROOT}/bin")
    endif()
    if(DEFINED ENV{LLVM_HOME})
        list(APPEND RAAD_LLVM_HINTS "$ENV{LLVM_HOME}/bin")
    endif()
    if(DEFINED ENV{LLVM_DIR})
        list(APPEND RAAD_LLVM_HINTS "$ENV{LLVM_DIR}/bin")
    endif()
    find_program(RAAD_BREW brew HINTS /opt/homebrew/bin /usr/local/bin)
    if(RAAD_BREW)
        execute_process(
            COMMAND "${RAAD_BREW}" --prefix llvm
            OUTPUT_VARIABLE RAAD_BREW_LLVM_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(RAAD_BREW_LLVM_PREFIX)
            list(APPEND RAAD_LLVM_HINTS "${RAAD_BREW_LLVM_PREFIX}/bin")
        endif()
    endif()
endif()

find_program(RAAD_CLANG
    NAMES clang
    HINTS ${RAAD_LLVM_HINTS}
)
find_program(RAAD_CLANGXX
    NAMES clang++ clang-cl
    HINTS ${RAAD_LLVM_HINTS}
)
find_program(RAAD_CLANG_SCAN_DEPS
    NAMES clang-scan-deps
    HINTS ${RAAD_LLVM_HINTS}
)
if(NOT RAAD_CLANG_SCAN_DEPS)
    set(RAAD_CXX_DIR "")
    if(RAAD_CXX_COMPILER_HINT)
        get_filename_component(RAAD_CXX_DIR "${RAAD_CXX_COMPILER_HINT}" DIRECTORY)
    elseif(CMAKE_CXX_COMPILER)
        get_filename_component(RAAD_CXX_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif()
    if(RAAD_CXX_DIR)
        find_program(RAAD_CLANG_SCAN_DEPS
            NAMES clang-scan-deps
            HINTS "${RAAD_CXX_DIR}"
        )
    endif()
endif()

set(RAAD_CXX_COMPILER_HINT "")
if(DEFINED CACHE{CMAKE_CXX_COMPILER})
    get_property(RAAD_CXX_COMPILER_HINT CACHE CMAKE_CXX_COMPILER PROPERTY VALUE)
elseif(DEFINED CMAKE_CXX_COMPILER)
    set(RAAD_CXX_COMPILER_HINT "${CMAKE_CXX_COMPILER}")
endif()

set(RAAD_CLANGXX_IS_APPLE_PATH FALSE)
if(RAAD_CLANGXX MATCHES "^/usr/bin/clang\\+\\+$" OR
   RAAD_CLANGXX MATCHES "XcodeDefault\\.xctoolchain")
    set(RAAD_CLANGXX_IS_APPLE_PATH TRUE)
endif()

if(RAAD_USE_MODULES AND RAAD_CXX_COMPILER_HINT)
    if((RAAD_CXX_COMPILER_HINT MATCHES "^/usr/bin/clang\\+\\+$") OR
       (RAAD_CXX_COMPILER_HINT MATCHES "XcodeDefault\\.xctoolchain"))
        if(RAAD_CLANG AND RAAD_CLANGXX AND RAAD_CLANG_SCAN_DEPS AND NOT RAAD_CLANGXX_IS_APPLE_PATH)
            message(STATUS "Switching to LLVM clang from PATH for C++20 modules")
            set(CMAKE_C_COMPILER "${RAAD_CLANG}" CACHE FILEPATH "" FORCE)
            set(CMAKE_CXX_COMPILER "${RAAD_CLANGXX}" CACHE FILEPATH "" FORCE)
            set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
                "${RAAD_CLANG_SCAN_DEPS}"
                CACHE FILEPATH "clang-scan-deps for C++20 modules" FORCE
            )
        endif()
    elseif(RAAD_CXX_COMPILER_HINT MATCHES "g\\+\\+|gcc")
        if(RAAD_CLANG AND RAAD_CLANGXX AND RAAD_CLANG_SCAN_DEPS AND NOT RAAD_CLANGXX_IS_APPLE_PATH)
            message(STATUS "Switching to LLVM clang from PATH for C++20 modules")
            set(CMAKE_C_COMPILER "${RAAD_CLANG}" CACHE FILEPATH "" FORCE)
            set(CMAKE_CXX_COMPILER "${RAAD_CLANGXX}" CACHE FILEPATH "" FORCE)
            set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
                "${RAAD_CLANG_SCAN_DEPS}"
                CACHE FILEPATH "clang-scan-deps for C++20 modules" FORCE
            )
        endif()
    endif()
endif()
if(RAAD_CLANG_SCAN_DEPS AND NOT DEFINED CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
    set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
        "${RAAD_CLANG_SCAN_DEPS}"
        CACHE FILEPATH "clang-scan-deps for C++20 modules"
    )
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable global scanning so try_compile does not require module scanning.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
if(NOT "cppm" IN_LIST CMAKE_AUTOMOC_EXTENSIONS)
    list(APPEND CMAKE_AUTOMOC_EXTENSIONS "cppm")
endif()

if(RAAD_USE_MODULES)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        set(RAAD_APPLECLANG_PATH FALSE)
        if(CMAKE_CXX_COMPILER MATCHES "^/usr/bin/clang\\+\\+$" OR
           CMAKE_CXX_COMPILER MATCHES "XcodeDefault\\.xctoolchain")
            set(RAAD_APPLECLANG_PATH TRUE)
        endif()
        if(RAAD_APPLECLANG_PATH)
            if(RAAD_CLANGXX AND NOT RAAD_CLANGXX_IS_APPLE_PATH)
                message(FATAL_ERROR
                    "AppleClang is locked in this build directory (CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}). "
                    "LLVM clang is available on PATH, but CMake cannot switch compilers in-place. "
                    "Delete the build directory and reconfigure, or set -DRAAD_USE_MODULES=OFF."
                )
            else()
                message(FATAL_ERROR
                    "AppleClang does not provide clang-scan-deps required for C++20 modules. "
                    "Install LLVM clang and ensure it is on PATH, or set -DRAAD_USE_MODULES=OFF."
                )
            endif()
        elseif(NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
            message(FATAL_ERROR
                "AppleClang detected but clang-scan-deps was not found. "
                "Install LLVM and ensure clang-scan-deps is on PATH, or set "
                "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/path/to/clang-scan-deps."
            )
        else()
            message(STATUS "AppleClang detected with clang-scan-deps set; proceeding.")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR
            "GCC is not supported for C++20 modules in this project. "
            "Use Clang (with clang-scan-deps) or MSVC, or set -DRAAD_USE_MODULES=OFF."
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
        message(FATAL_ERROR
            "C++20 modules require clang-scan-deps. Install LLVM and ensure it is on PATH "
            "or configure with -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/path/to/clang-scan-deps."
        )
    endif()
endif()

# -------------------------
# Qt discovery helpers (cross-platform)
# -------------------------
option(RAAD_QT_AUTO_SCAN "Auto-detect Qt installation prefix" ON)

if(Qt6_DIR AND NOT EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    message(STATUS "Ignoring invalid Qt6_DIR='${Qt6_DIR}' (Qt6Config.cmake not found).")
    unset(Qt6_DIR CACHE)
    set(Qt6_DIR "")
endif()

function(_raad_add_qt_prefix _prefix)
    if(EXISTS "${_prefix}/lib/cmake/Qt6/Qt6Config.cmake")
        list(APPEND CMAKE_PREFIX_PATH "${_prefix}")
        if(NOT Qt6_DIR)
            set(Qt6_DIR "${_prefix}/lib/cmake/Qt6" CACHE PATH "Qt6 config directory" FORCE)
        endif()
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    endif()
endfunction()
if(NOT DEFINED Qt6_DIR)
    if(DEFINED ENV{Qt6_DIR})
        set(Qt6_DIR "$ENV{Qt6_DIR}")
    elseif(DEFINED ENV{Qt_DIR})
        set(Qt6_DIR "$ENV{Qt_DIR}")
    endif()
endif()

if(DEFINED ENV{QTDIR})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()
if(DEFINED ENV{QT_DIR})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
endif()
if(DEFINED ENV{Qt6_ROOT})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{Qt6_ROOT}")
endif()
if(DEFINED ENV{QT_ROOT})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
endif()
if(DEFINED ENV{CMAKE_PREFIX_PATH})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CMAKE_PREFIX_PATH}")
endif()

if(NOT Qt6_DIR)
    find_program(QT_QTPATHS_EXECUTABLE NAMES qtpaths qtpaths6)
    if(QT_QTPATHS_EXECUTABLE)
        execute_process(
            COMMAND "${QT_QTPATHS_EXECUTABLE}" --query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE _qt_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_qt_prefix)
            _raad_add_qt_prefix("${_qt_prefix}")
        endif()
    endif()

    find_program(QT_QMAKE_EXECUTABLE NAMES qmake qmake6)
    if(QT_QMAKE_EXECUTABLE)
        get_filename_component(_qt_bin_dir "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
        get_filename_component(_qt_prefix "${_qt_bin_dir}" DIRECTORY)
        _raad_add_qt_prefix("${_qt_prefix}")
    endif()
endif()

if(RAAD_QT_AUTO_SCAN AND NOT Qt6_DIR)
    set(_qt_candidates "")
    set(_raad_home "${CMAKE_USER_HOME_DIRECTORY}")
    if(NOT _raad_home AND DEFINED ENV{HOME})
        set(_raad_home "$ENV{HOME}")
    endif()
    if(APPLE)
        file(GLOB _qt_candidates
            "${_raad_home}/Qt/*/macos"
            "${_raad_home}/Qt/*/clang_64"
            "/opt/Qt/*/macos"
            "/usr/local/Qt/*/macos"
        )
    elseif(WIN32)
        file(GLOB _qt_candidates
            "C:/Qt/*/msvc*_64"
            "C:/Qt/*/mingw*_64"
            "C:/Qt/*/clang_64"
        )
    else()
        file(GLOB _qt_candidates
            "${CMAKE_USER_HOME_DIRECTORY}/Qt/*/gcc_64"
            "${CMAKE_USER_HOME_DIRECTORY}/Qt/*/linux_gcc_64"
            "${CMAKE_USER_HOME_DIRECTORY}/Qt/*/clang_64"
            "/opt/Qt/*/gcc_64"
            "/opt/Qt/*/clang_64"
        )
    endif()
    foreach(_qt_prefix IN LISTS _qt_candidates)
        _raad_add_qt_prefix("${_qt_prefix}")
    endforeach()
endif()

list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
if(CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "Qt search prefixes" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2 Network Concurrent Gui)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(${APP_NAME}
    WIN32
    MACOSX_BUNDLE
    src/core/main.cpp
)

add_executable(RaadUpdaterHelper
    src/updaterhelper/main.cpp
)
target_link_libraries(RaadUpdaterHelper PRIVATE Qt6::Core)
set_target_properties(RaadUpdaterHelper PROPERTIES
    OUTPUT_NAME "RaadUpdaterHelper"
)

# ---- QML Files ----
set(qml_core
        "ui/Core/FontSystem.qml"
        "ui/Core/AppGlobals.qml"
        "ui/Core/Colors.qml"
        "ui/Core/Metrics.qml"
        "ui/Core/Typography.qml"
        "ui/Core/Theme.qml"
        "ui/Core/Animations.qml"
        "ui/Core/Elevation.qml"
        "ui/Core/Shadow.qml"
)
set(qml_controls
        "ui/Controls/Avatar.qml"
        "ui/Controls/AlphaGlass.qml"
        "ui/Controls/Card.qml"
        "ui/Controls/HorizontalLine.qml"
        "ui/Controls/HorizontalSpacer.qml"
        "ui/Controls/VerticalLine.qml"
        "ui/Controls/VerticalSpacer.qml"
        "ui/Controls/Text.qml"
        "ui/Controls/Button.qml"
        "ui/Controls/ActionButton.qml"
        "ui/Controls/IconButton.qml"
        "ui/Controls/MenuButton.qml"
        "ui/Controls/PillButton.qml"
        "ui/Controls/Label.qml"
        "ui/Controls/Text.qml"
        "ui/Controls/TextField.qml"
        "ui/Controls/TextArea.qml"
        "ui/Controls/ComboBox.qml"
        "ui/Controls/CheckBox.qml"
        "ui/Controls/Dialog.qml"
        "ui/Controls/SpinBox.qml"
        "ui/Controls/ProgressBar.qml"
        "ui/Controls/AppMenuBar.qml"
        "ui/Controls/AppMenuBarItem.qml"
        "ui/Controls/AppMenu.qml"
        "ui/Controls/AppMenuItem.qml"
        "ui/Controls/AppMenuSeparator.qml"
        "ui/Controls/AppMenuTrigger.qml"
        "ui/Controls/CommandActionButton.qml"
        "ui/Controls/CommandAddUrlButton.qml"
        "ui/Controls/SidebarTreeItem.qml"
        "ui/Controls/LayoutButton.qml"
        "ui/Controls/Switch.qml"
        "ui/Controls/MiniButton.qml"
        "ui/Controls/TabBar.qml"
        "ui/Controls/ToolTip.qml"
        "ui/Controls/TabBarButton.qml"
        "ui/Controls/TabButton.qml"
        "ui/Controls/TabButtonMini.qml"
        "ui/Controls/Menu.qml"
        "ui/Controls/MenuItem.qml"
        "ui/Controls/MenuSeparator.qml"
        "ui/Controls/BlurPopupSurface.qml"
        "ui/Controls/FootMenu.qml"
        "ui/Controls/FootButton.qml"
        "ui/Controls/GroupBox.qml"
        "ui/Controls/Image.qml"
        "ui/Controls/DownloadDelegate.qml"
        "ui/Controls/DownloadListView.qml"
        "ui/Controls/DownloadDetailsDialog.qml"
        "ui/Controls/DownloadDetailsWindow.qml"
        "ui/Controls/AddDownloadDialog.qml"
        "ui/Controls/HorizontalLine.qml"
        "ui/Controls/HorizontalSpacer.qml"
        "ui/Controls/VerticalLine.qml"
        "ui/Controls/VerticalSpacer.qml"
)
set(qml_layout
        "ui/Layout/Footer.qml"
        "ui/Layout/Header.qml"
        "ui/Layout/MoreLink.qml"
        "ui/Layout/Setting.qml"
)

# set(qml_dashboard)

set(qml_main ui/Main.qml)

set(qml_files
        ${qml_main}
        ${qml_core}
        ${qml_controls}
        ${qml_layout}
        # ${qml_dashboard}
)

function(raad_filter_existing out_var)
    set(_filtered "")
    foreach(_rel IN LISTS ARGN)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_rel}")
            list(APPEND _filtered "${_rel}")
        else()
            message(STATUS "Skipping missing UI file: ${_rel}")
        endif()
    endforeach()
    set(${out_var} "${_filtered}" PARENT_SCOPE)
endfunction()

raad_filter_existing(qml_files_existing ${qml_files})

# ---- Mark singletons ----
set(qml_singletons
        ui/Core/AppGlobals.qml
        ui/Core/Metrics.qml
        ui/Core/Typography.qml
        ui/Core/Theme.qml
        ui/Core/Animations.qml
        ui/Core/FontSystem.qml
        ui/Core/Colors.qml
)

raad_filter_existing(qml_singletons_existing ${qml_singletons})
set_source_files_properties(${qml_singletons_existing} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(qml_resource_files
       "Raad.png"
       "resources/fonts/Inter-Medium.ttf"
       "resources/fonts/IRANSansX-Regular.ttf"
       "resources/fonts/Font Awesome 6 Brands-Regular-400.otf"
       "resources/fonts/Font Awesome 6 Pro-Regular-400.otf"
       "resources/fonts/Font Awesome 6 Pro-Solid-900.otf"
       "resources/fonts/Font Awesome 6 Pro-Thin-100.otf"
       "resources/fonts/Font Awesome 6 Pro-Light-300.otf"
       "resources/fonts/Font Awesome 6 Duotone-Light-300.otf"

       "resources/image/avatar.svg"
)
raad_filter_existing(qml_resource_files_existing ${qml_resource_files})

qt_add_resources(${APP_NAME} RaadAssets
       PREFIX /
       FILES
       ${qml_resource_files_existing}
)

set(RAAD_MODULE_IFS
    src/core/downloadertask.cppm
    src/core/downloadmanager.cppm
    src/core/downloadmodel.cppm
    src/utils/download_utils.cppm
    src/utils/category_utils.cppm
    src/utils/version_utils.cppm
    src/services/power_monitor.cppm
    src/services/update_client.cppm
)

set(RAAD_IMPL_SOURCES
    src/core/downloadertask.cpp
    src/core/downloadmanager.cpp
    src/core/downloadmodel.cpp
    src/utils/download_utils.cpp
    src/utils/category_utils.cpp
    src/utils/version_utils.cpp
    src/services/power_monitor.cpp
    src/services/update_client.cpp
)

set(qml_module_resources
        ui/Core/qmldir
        ui/Controls/qmldir
        ui/Layout/qmldir
        ui/Dashboard/qmldir
)
raad_filter_existing(qml_module_resources_existing ${qml_module_resources})

qt_add_qml_module(${APP_NAME}
        URI Raad
        VERSION 1.0
        QML_FILES
        ${qml_files_existing}
        RESOURCES
        ${qml_module_resources_existing}

        SOURCES
        ${RAAD_IMPL_SOURCES}
        QML_FILES ui/utils.js
)

if(RAAD_USE_MODULES)
    target_sources(${APP_NAME}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES ${RAAD_MODULE_IFS}
    )
    set_property(TARGET ${APP_NAME} PROPERTY CXX_SCAN_FOR_MODULES ON)
else()
    message(FATAL_ERROR
        "RAAD_USE_MODULES=OFF is not supported in this source tree because backend sources import project modules. "
        "Build with RAAD_USE_MODULES=ON and a compiler that supports module dependency scanning."
    )
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(APPLE)
    set_target_properties(${APP_NAME} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER ${RAAD_BUNDLE_IDENTIFIER}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE "Raad.icns"
        MACOSX_BUNDLE_INFO_PLIST "${RAAD_MACOS_INFO_PLIST}"
    )
    if(EXISTS "${RAAD_MACOS_ICON_ICNS}")
        set_source_files_properties("${RAAD_MACOS_ICON_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${APP_NAME} PRIVATE "${RAAD_MACOS_ICON_ICNS}")
    endif()
elseif(WIN32)
    set_target_properties(${APP_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    if(EXISTS "${RAAD_WINDOWS_RC}")
        target_sources(${APP_NAME} PRIVATE "${RAAD_WINDOWS_RC}")
    endif()
endif()

target_link_libraries(${APP_NAME}
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Concurrent
)

add_dependencies(${APP_NAME} RaadUpdaterHelper)
add_custom_command(TARGET RaadUpdaterHelper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:RaadUpdaterHelper>
            $<TARGET_FILE_DIR:${APP_NAME}>/$<TARGET_FILE_NAME:RaadUpdaterHelper>
)

if(WIN32 AND MSVC)
    target_link_options(RaadUpdaterHelper PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    )
endif()

target_compile_definitions(${APP_NAME}
    PRIVATE APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(${APP_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

option(RAAD_BUILD_TESTS "Build backend tests" ON)
if(RAAD_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Core Test Gui)

    qt_add_executable(raad_backend_tests
        tests/backend_tests.cpp
    )

    if(RAAD_USE_MODULES)
        target_sources(raad_backend_tests
            PUBLIC
            FILE_SET CXX_MODULES TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
            FILES ${RAAD_MODULE_IFS}
        )
    endif()

    target_sources(raad_backend_tests
        PRIVATE
        ${RAAD_IMPL_SOURCES}
    )

    if(RAAD_USE_MODULES)
        set_property(TARGET raad_backend_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    endif()

    target_link_libraries(raad_backend_tests
        PRIVATE Qt6::Test Qt6::Network Qt6::Concurrent Qt6::Gui
    )

    target_include_directories(raad_backend_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME raad_backend_tests COMMAND raad_backend_tests)
endif()


include(GNUInstallDirs)
if(APPLE)
    install(TARGETS ${APP_NAME}
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES $<TARGET_FILE:RaadUpdaterHelper>
        DESTINATION ${APP_NAME}.app/Contents/MacOS
    )
elseif(WIN32)
    install(TARGETS ${APP_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(TARGETS RaadUpdaterHelper
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    install(TARGETS ${APP_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(TARGETS RaadUpdaterHelper
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    if(EXISTS "${RAAD_LINUX_ICON_PNG}")
        install(FILES "${RAAD_LINUX_ICON_PNG}"
            DESTINATION share/icons/hicolor/512x512/apps
            RENAME raad.png
        )
    endif()
endif()

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "${RAAD_VENDOR}")
set(CPACK_PACKAGE_CONTACT "${RAAD_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${RAAD_PACKAGE_SUMMARY}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "${APP_NAME}")
    set(CPACK_NSIS_PACKAGE_NAME "${APP_NAME}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${APP_NAME}")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
