cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0074 NEW)

project(SpeedyNote VERSION 1.0.2 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Build Options
# ============================================================================
option(ENABLE_CONTROLLER_SUPPORT "Enable SDL2 game controller support" OFF)
option(ENABLE_DEBUG_OUTPUT "Enable verbose debug output (qDebug prints)" OFF)

message(STATUS "Controller support: ${ENABLE_CONTROLLER_SUPPORT}")
if(ENABLE_DEBUG_OUTPUT)
    add_compile_definitions(SPEEDYNOTE_DEBUG)
    message(STATUS "Debug output: ENABLED")
else()
    message(STATUS "Debug output: DISABLED")
endif()

# ============================================================================
# Qt Automatic Features (must be set before finding Qt)
# ============================================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================================
# Platform Detection and Configuration
# ============================================================================

if(WIN32)
    # =========================================================================
    # Windows Configuration
    # =========================================================================
    message(STATUS "🪟 Configuring for Windows")
    
    # Detect architecture and set toolchain paths
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        set(QT_PATH "C:/msys64/clangarm64" CACHE PATH "Path to Qt installation")
        set(SDL2_ROOT "C:/msys64/clangarm64" CACHE PATH "Path to SDL2")
        message(STATUS "Detected ARM64 Windows - using clangarm64 toolchain")
    else()
        set(QT_PATH "C:/msys64/clang64" CACHE PATH "Path to Qt installation")
        set(SDL2_ROOT "C:/msys64/clang64" CACHE PATH "Path to SDL2")
        message(STATUS "Detected x86_64 Windows - using clang64 toolchain")
endif()

    # CMake paths
    set(CMAKE_PREFIX_PATH "${QT_PATH}/lib/cmake" "${SDL2_ROOT}/lib/cmake")
    include_directories(${CMAKE_CURRENT_BINARY_DIR})

    # Find dependencies
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Test)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)
    
    if(ENABLE_CONTROLLER_SUPPORT)
        find_package(SDL2 REQUIRED CONFIG)
        unset(SDL2main CACHE)
        include_directories("${SDL2_ROOT}/include")
        add_compile_definitions(SDL_MAIN_HANDLED SPEEDYNOTE_CONTROLLER_SUPPORT)
    endif()

    # CPU optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        message(STATUS "🚀 Optimizing for ARM64 Windows (Snapdragon)")
        add_compile_options(-mcpu=cortex-a75 -march=armv8.2-a+fp16+rcpc+dotprod
            -O3 -ffast-math -ftree-vectorize -fvectorize -flto=thin
            -fomit-frame-pointer -funroll-loops -DNDEBUG)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=thin")
    else()
        message(STATUS "🚀 Optimizing for x86_64 Windows (Nehalem+)")
        add_compile_options(-march=nehalem -msse4.2 -O3 -ffast-math -DNDEBUG)
    endif()

    # Windows resources
    enable_language(RC)
    set(WIN_RESOURCES app_icon.rc)

elseif(APPLE)
    # =========================================================================
    # macOS Configuration
    # =========================================================================
    message(STATUS "🍎 Configuring for macOS")
    
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
    
    # Homebrew paths
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(HOMEBREW_PREFIX "/opt/homebrew")
        message(STATUS "Detected Apple Silicon - using ${HOMEBREW_PREFIX}")
    else()
        set(HOMEBREW_PREFIX "/usr/local")
        message(STATUS "Detected Intel Mac - using ${HOMEBREW_PREFIX}")
    endif()
    
    set(CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}/opt/qt@6")
    include_directories(${CMAKE_CURRENT_BINARY_DIR} "${HOMEBREW_PREFIX}/include")
    
    # Find dependencies
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Test)
    find_package(PkgConfig REQUIRED)
    
    # Poppler (manual config - no .pc file on macOS)
    set(POPPLER_PREFIX "/opt/poppler-qt6")
    include_directories("${POPPLER_PREFIX}/include/poppler/qt6")
    link_directories("${POPPLER_PREFIX}/lib")
    set(POPPLER_LIBRARIES "poppler-qt6")
    
    if(ENABLE_CONTROLLER_SUPPORT)
        list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}/opt/sdl2")
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        add_compile_definitions(SPEEDYNOTE_CONTROLLER_SUPPORT)
    endif()
    
    # CPU optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        message(STATUS "🚀 Optimizing for Apple Silicon")
        add_compile_options(-mcpu=apple-m1 -O3 -ffast-math -funroll-loops
            -fomit-frame-pointer -DNDEBUG)
    else()
        message(STATUS "🚀 Optimizing for Intel Mac (Nehalem+)")
        add_compile_options(-march=nehalem -mtune=nehalem -msse4.2 -mpopcnt
            -O3 -ffast-math -funroll-loops -fomit-frame-pointer -DNDEBUG)
    endif()
    
    set(WIN_RESOURCES "")

elseif(ANDROID)
    # =========================================================================
    # Android Configuration
    # =========================================================================
    message(STATUS "🤖 Configuring for Android")
    
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network)
    
    # MuPDF paths (cross-compiled separately)
    if(NOT MUPDF_INCLUDE_DIR)
        set(MUPDF_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/android/mupdf-build/include")
    endif()
    if(NOT MUPDF_LIBRARIES)
        set(MUPDF_LIBRARIES
            "${CMAKE_SOURCE_DIR}/android/mupdf-build/lib/libmupdf.a"
            "${CMAKE_SOURCE_DIR}/android/mupdf-build/lib/libmupdf-third.a")
    endif()
    
    include_directories(${CMAKE_CURRENT_BINARY_DIR} ${MUPDF_INCLUDE_DIR})
    
    # No controller support on Android
    set(ENABLE_CONTROLLER_SUPPORT OFF)
    set(WIN_RESOURCES "")
    
elseif(UNIX)
    # =========================================================================
    # Linux Configuration
    # =========================================================================
    message(STATUS "🐧 Configuring for Linux")
    
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Test)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)

    if(ENABLE_CONTROLLER_SUPPORT)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        add_compile_definitions(SPEEDYNOTE_CONTROLLER_SUPPORT)
    endif()

    include_directories(${CMAKE_CURRENT_BINARY_DIR})

    # CPU optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        message(STATUS "🚀 Optimizing for x86_64 Linux (Nehalem+)")
        add_compile_options(-march=nehalem -mtune=nehalem -msse4.2 -mpopcnt
            -O3 -ffast-math -funroll-loops -fomit-frame-pointer -DNDEBUG)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        message(STATUS "🚀 Optimizing for ARM64 Linux (Cortex-A72)")
        add_compile_options(-march=armv8-a+crc+simd -mtune=cortex-a72
            -O3 -ffast-math -funroll-loops -fomit-frame-pointer -DNDEBUG)
    else()
        message(STATUS "Building for generic Linux (${CMAKE_SYSTEM_PROCESSOR})")
        add_compile_options(-O3 -DNDEBUG)
    endif()
    
    set(WIN_RESOURCES "")
endif()

# ============================================================================
# Third-party Libraries (cross-platform)
# ============================================================================
# miniz: ZIP library for .snbx package export/import (MIT license)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/miniz)

# ============================================================================
# Translation Files
# ============================================================================
set(TS_FILES
        ${CMAKE_SOURCE_DIR}/resources/translations/app_zh.ts
        ${CMAKE_SOURCE_DIR}/resources/translations/app_es.ts
        ${CMAKE_SOURCE_DIR}/resources/translations/app_fr.ts
)

# ============================================================================
# QMarkdownTextEdit (bundled library)
# ============================================================================
set(QMARKDOWNTEXTEDIT_SOURCES
        markdown/qmarkdowntextedit.cpp
        markdown/markdownhighlighter.cpp
        markdown/qownlanguagedata.cpp
        markdown/qplaintexteditsearchwidget.cpp
)

qt6_wrap_cpp(QMARKDOWNTEXTEDIT_MOC_SOURCES markdown/linenumberarea.h)
qt6_wrap_ui(QMARKDOWNTEXTEDIT_UI_HEADERS markdown/qplaintexteditsearchwidget.ui)

# ============================================================================
# Resources
# ============================================================================
set(RESOURCES resources.qrc markdown/media.qrc)
qt6_add_resources(QRCC_FILES ${RESOURCES})

# ============================================================================
# Source Files
# ============================================================================

# Core document architecture
set(CORE_SOURCES
    source/core/Page.cpp
    source/core/Document.cpp
    source/core/DocumentViewport.cpp
    source/core/DocumentManager.cpp
    source/core/NotebookLibrary.cpp
    source/core/TouchGestureHandler.cpp
    source/core/MarkdownNote.cpp
    source/core/ShortcutManager.cpp
)

# Inserted objects (images, links, etc.)
set(OBJECT_SOURCES
    source/objects/InsertedObject.cpp
    source/objects/ImageObject.cpp
    source/objects/LinkObject.cpp
)

# UI components
set(UI_SOURCES
    # Core UI
    source/ui/TabManager.cpp
    source/ui/DebugOverlay.cpp
    source/ui/ToolbarButtons.cpp
    source/ui/NavigationBar.cpp
    source/ui/Toolbar.cpp
    source/ui/StyleLoader.cpp
    source/ui/TabBar.cpp
    source/ui/PageThumbnailModel.cpp
    source/ui/PageThumbnailDelegate.cpp
    source/ui/ThumbnailRenderer.cpp
    # Sidebars
    source/ui/sidebars/LayerPanel.cpp
    source/ui/sidebars/LeftSidebarContainer.cpp
    source/ui/sidebars/OutlinePanel.cpp
    source/ui/sidebars/OutlineItemDelegate.cpp
    source/ui/sidebars/PagePanel.cpp
    source/ui/sidebars/PagePanelListView.cpp
    # Widgets
    source/ui/widgets/ColorPresetButton.cpp
    source/ui/widgets/ThicknessPresetButton.cpp
    source/ui/widgets/ToggleButton.cpp
    source/ui/widgets/ModeToggleButton.cpp
    source/ui/widgets/LinkSlotButton.cpp
    source/ui/widgets/ActionBarButton.cpp
    source/ui/widgets/PageWheelPicker.cpp
    source/ui/widgets/UndoDeleteButton.cpp
    source/ui/widgets/LayerItemWidget.cpp
    source/ui/widgets/LayerPanelPillButton.cpp
    # Subtoolbars
    source/ui/subtoolbars/SubToolbar.cpp
    source/ui/subtoolbars/SubToolbarContainer.cpp
    source/ui/subtoolbars/PenSubToolbar.cpp
    source/ui/subtoolbars/MarkerSubToolbar.cpp
    source/ui/subtoolbars/HighlighterSubToolbar.cpp
    source/ui/subtoolbars/ObjectSelectSubToolbar.cpp
    # Action bars
    source/ui/actionbars/ActionBar.cpp
    source/ui/actionbars/ActionBarContainer.cpp
    source/ui/actionbars/LassoActionBar.cpp
    source/ui/actionbars/ObjectSelectActionBar.cpp
    source/ui/actionbars/TextSelectionActionBar.cpp
    source/ui/actionbars/ClipboardActionBar.cpp
    source/ui/actionbars/PagePanelActionBar.cpp
    # Banners
    source/ui/banners/MissingPdfBanner.cpp
    # Launcher
    source/ui/launcher/Launcher.cpp
    source/ui/launcher/LauncherNavButton.cpp
    source/ui/launcher/TimelineModel.cpp
    source/ui/launcher/TimelineDelegate.cpp
    source/ui/launcher/TimelineListView.cpp
    source/ui/launcher/NotebookCard.cpp
    source/ui/launcher/StarredView.cpp
    source/ui/launcher/SearchView.cpp
    source/ui/launcher/FloatingActionButton.cpp
)

# PDF abstraction layer (platform-specific backend)
if(ANDROID)
    set(PDF_SOURCES
        source/pdf/PdfProviderFactory.cpp
        source/pdf/MuPdfProvider.cpp
        source/pdf/PdfRelinkDialog.cpp
        source/pdf/PdfMismatchDialog.cpp
    )
else()
set(PDF_SOURCES
        source/pdf/PdfProviderFactory.cpp
        source/pdf/PopplerPdfProvider.cpp
        source/pdf/PdfRelinkDialog.cpp
        source/pdf/PdfMismatchDialog.cpp
    )
endif()

# Controller support (optional)
set(CONTROLLER_SOURCES "")
if(ENABLE_CONTROLLER_SUPPORT)
    set(CONTROLLER_SOURCES
        source/SDLControllerManager.cpp
        source/ControllerMappingDialog.cpp
    )
endif()

# Sharing module (cross-platform .snbx package export/import)
# Uses miniz library for ZIP creation/extraction (MIT license, thirdparty/miniz/)
set(SHARING_SOURCES
    source/sharing/NotebookExporter.cpp
    source/sharing/NotebookImporter.cpp
    source/sharing/ExportDialog.cpp
    thirdparty/miniz/miniz.c
)

# Android-specific utilities and UI
set(ANDROID_PLATFORM_SOURCES "")
if(ANDROID)
    set(ANDROID_PLATFORM_SOURCES
        source/android/PdfPickerAndroid.cpp
        source/ui/dialogs/SaveDocumentDialog.cpp  # BUG-A002: Touch-friendly save dialog
    )
endif()

# Desktop-only sources (tests require Qt::Test)
set(DESKTOP_ONLY_SOURCES "")
if(NOT ANDROID)
    set(DESKTOP_ONLY_SOURCES
        source/ui/ToolbarButtonTests.cpp
        source/ui/ToolbarButtonTestWidget.cpp
    )
endif()

# ============================================================================
# Main Application Sources
# ============================================================================
set(APP_SOURCES
        source/Main.cpp
        source/MainWindow.cpp
    source/ControlPanelDialog.cpp
    source/text/MarkdownNoteEntry.cpp
    source/ui/MarkdownNotesSidebar.cpp
        ${CORE_SOURCES}
    ${OBJECT_SOURCES}
        ${UI_SOURCES}
        ${PDF_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SHARING_SOURCES}
    ${ANDROID_PLATFORM_SOURCES}
    ${DESKTOP_ONLY_SOURCES}
        ${QMARKDOWNTEXTEDIT_SOURCES}
    ${QMARKDOWNTEXTEDIT_MOC_SOURCES}
        ${QMARKDOWNTEXTEDIT_UI_HEADERS}
    ${QRCC_FILES}
        ${WIN_RESOURCES}
)

# ============================================================================
# Build Target
# ============================================================================
if(ANDROID)
    qt_add_executable(NoteApp ${APP_SOURCES})
    
    set_target_properties(NoteApp PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android/app-resources"
        QT_ANDROID_TARGET_SDK_VERSION 35
        QT_ANDROID_MIN_SDK_VERSION 26
    )
    
    target_include_directories(NoteApp PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/source
        ${MUPDF_INCLUDE_DIR}
    )
else()
    add_executable(NoteApp ${APP_SOURCES})
endif()

# ============================================================================
# Translation Compilation (.ts -> .qm)
# ============================================================================
if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        find_program(LRELEASE_EXECUTABLE lrelease PATHS "C:/msys64/clangarm64/bin")
    else()
        find_program(LRELEASE_EXECUTABLE lrelease PATHS "C:/msys64/clang64/bin")
    endif()
else()
    find_program(LRELEASE_EXECUTABLE lrelease PATHS "${QT_PATH}/bin" NO_DEFAULT_PATH)
endif()

if(LRELEASE_EXECUTABLE)
    foreach(_ts ${TS_FILES})
        get_filename_component(_qm ${_ts} NAME_WE)
        set(_qm_file ${CMAKE_CURRENT_BINARY_DIR}/${_qm}.qm)
        add_custom_command(
                OUTPUT ${_qm_file}
                COMMAND ${LRELEASE_EXECUTABLE} ${_ts} -qm ${_qm_file}
                DEPENDS ${_ts}
                COMMENT "Generating ${_qm_file}"
                VERBATIM
        )
        list(APPEND QM_FILES ${_qm_file})
    endforeach()
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
endif()

# ============================================================================
# Link Libraries (Platform-Specific)
# ============================================================================
if(WIN32)
    set(PLATFORM_LIBS
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml Qt6::Network Qt6::Test
            PkgConfig::POPPLER
    )
    if(ENABLE_CONTROLLER_SUPPORT)
        list(APPEND PLATFORM_LIBS SDL2::SDL2)
    endif()
    target_link_libraries(NoteApp ${PLATFORM_LIBS})

elseif(APPLE)
    set(PLATFORM_LIBS
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml Qt6::Network Qt6::Test
            ${POPPLER_LIBRARIES}
    )
    if(ENABLE_CONTROLLER_SUPPORT)
        list(APPEND PLATFORM_LIBS PkgConfig::SDL2)
    endif()
    target_link_libraries(NoteApp ${PLATFORM_LIBS})

elseif(ANDROID)
    target_link_libraries(NoteApp PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml Qt6::Network
        ${MUPDF_LIBRARIES}
        log z
    )

elseif(UNIX)
    set(PLATFORM_LIBS
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml Qt6::Network Qt6::Test
            PkgConfig::POPPLER
    )
    if(ENABLE_CONTROLLER_SUPPORT)
        list(APPEND PLATFORM_LIBS PkgConfig::SDL2)
    endif()
    target_link_libraries(NoteApp ${PLATFORM_LIBS})
endif()

# ============================================================================
# Install Targets
# ============================================================================
if(APPLE)
    install(TARGETS NoteApp DESTINATION bin)
    if(QM_FILES)
        install(FILES ${QM_FILES} DESTINATION share/speedynote/translations)
    endif()
    file(GLOB RESOURCE_QM_FILES "resources/translations/*.qm")
    if(RESOURCE_QM_FILES)
        install(FILES ${RESOURCE_QM_FILES} DESTINATION share/speedynote/translations)
    endif()
    install(FILES resources/icons/mainicon.png DESTINATION share/speedynote/icons)

elseif(UNIX AND NOT ANDROID)
    install(TARGETS NoteApp DESTINATION bin)
    if(QM_FILES)
        install(FILES ${QM_FILES} DESTINATION share/speedynote/translations)
    endif()
    file(GLOB RESOURCE_QM_FILES "resources/translations/*.qm")
    if(RESOURCE_QM_FILES)
        install(FILES ${RESOURCE_QM_FILES} DESTINATION share/speedynote/translations)
    endif()
    install(FILES resources/icons/mainicon.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME com.github.alpha_liu_01.SpeedyNote.png)
endif()
