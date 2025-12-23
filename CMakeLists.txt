cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0074 NEW)

project(SpeedyNote VERSION 0.12.2)
project(NoteApp)
set(CMAKE_CXX_STANDARD 17)

# ✅ Detect Windows architecture and set appropriate toolchain paths
if (WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        # ARM64 Windows - use clangarm64 toolchain
        set(QT_PATH "C:/msys64/clangarm64" CACHE PATH "Path to Qt installation")
        set(SDL2_ROOT "C:/msys64/clangarm64" CACHE PATH "Path to SDL2")
        set(POPPLER_PATH "C:/msys64/clangarm64" CACHE PATH "Path to Poppler")
        message(STATUS "Detected ARM64 Windows - using clangarm64 toolchain")
    else()
        # x86_64 Windows - use clang64 toolchain
        set(QT_PATH "C:/msys64/clang64" CACHE PATH "Path to Qt installation")
        set(SDL2_ROOT "C:/msys64/clang64" CACHE PATH "Path to SDL2")
        set(POPPLER_PATH "C:/msys64/clang64" CACHE PATH "Path to Poppler")
        message(STATUS "Detected x86_64 Windows - using clang64 toolchain")
    endif()
endif()

# This is where the "libpoppler-qt6-dev" equivalent in Windows is located. 
# Go to the MSYS2 terminal to run these commands:
# For x86_64: pacman -S mingw-w64-clang-x86_64-poppler-qt6
# For ARM64:  pacman -S mingw-w64-clang-aarch64-poppler-qt6
#
# Note: We use pkg-config to find Poppler, so no FindPoppler.cmake file is needed!

# ✅ Platform-specific configuration
if (WIN32)
    # ✅ Set CMake prefix paths for Qt6 and SDL2
    set(CMAKE_PREFIX_PATH
            "${QT_PATH}/lib/cmake"
            "${SDL2_ROOT}/lib/cmake"
    )

    # ✅ Use pkg-config to find Poppler (eliminates need for FindPoppler.cmake)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)
    
    # Include directories
    include_directories(
            ${CMAKE_CURRENT_BINARY_DIR}
            "${SDL2_ROOT}/include"
    )

    # Prevent SDL2 from redefining main()
    add_compile_definitions(SDL_MAIN_HANDLED)

    # ✅ CPU architecture-specific optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        # ✅ ARM64 Windows optimizations (Snapdragon X Elite, 850, etc.)
        message(STATUS "🚀 Building for ARM64 Windows (Cortex-A75/Snapdragon optimized)")
        add_compile_options(
            -mcpu=cortex-a75                        # Target Cortex-A75 (Snapdragon 850+)
            -march=armv8.2-a+fp16+rcpc+dotprod     # ARMv8.2-A with extensions
            -O3                                     # Maximum optimization
            -ffast-math                             # Aggressive FP optimizations
            -ftree-vectorize                        # Enable auto-vectorization
            -fvectorize                             # Clang vectorization
            -flto=thin                              # Thin LTO for faster builds
            -fomit-frame-pointer                    # Free up registers
            -funroll-loops                          # Unroll loops
            -DNDEBUG                                # Disable debug assertions
        )
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=thin")
        
    else()
        # ✅ x86_64 Windows optimizations
        # CPU architecture optimization target (can be set via -DCPU_ARCH=old)
        if(NOT DEFINED CPU_ARCH)
            set(CPU_ARCH "modern")
        endif()

        if(CPU_ARCH STREQUAL "old")
            # Optimizations for older CPUs (SSE3, compatible with Core 2 Duo, Atom Z)
            message(STATUS "Building for older x86_64 CPUs (SSE3/SSSE3 compatible - Core 2 Duo era)")
            add_compile_options(
                -march=core2            # Enable SSE3, SSSE3 (2006+ CPUs)
                -mtune=generic          # Optimize for broad CPU compatibility
                -msse3                  # Explicitly enable SSE3
                -mssse3                 # Explicitly enable SSSE3
                -O3                     # Maximum optimization level
                -DNDEBUG                # Disable debug assertions
            )
        else()
            # Optimizations for modern x86_64 CPUs (1st gen Core i / Nehalem+)
            message(STATUS "Building for modern x86_64 CPUs (SSE4.2 compatible - Core i series)")
            add_compile_options(
                -march=nehalem          # Target 1st gen Intel Core i
                -msse4.2                # Enable SSE4.2
                -O3                     # Maximum optimization level
                -ffast-math             # Aggressive floating-point optimizations
                -DNDEBUG                # Disable debug assertions
            )
        endif()
    endif()

    # Windows resource file
    enable_language(RC)
    set(WIN_RESOURCES app_icon.rc)

    # Find dependencies for Windows
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network)
    # Poppler is already found via pkg-config above
    find_package(SDL2 REQUIRED CONFIG)
    unset(SDL2main CACHE)
elseif (APPLE)
    # macOS-specific configuration
    # Use Homebrew paths for macOS
    message(STATUS "🍎 Configuring for macOS with Homebrew dependencies")
    
    # Set minimum macOS version (Qt6 requires 12.0+, but we'll try 12.0 for broader compatibility)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment version" FORCE)
    message(STATUS "Setting minimum macOS version: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    
    # Set Homebrew prefix based on architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(HOMEBREW_PREFIX "/opt/homebrew")
        message(STATUS "Detected Apple Silicon (ARM64) - using ${HOMEBREW_PREFIX}")
    else()
        set(HOMEBREW_PREFIX "/usr/local")
        message(STATUS "Detected Intel Mac (x86-64) - using ${HOMEBREW_PREFIX}")
    endif()
    
    # Set paths for Homebrew-installed Qt6
    set(CMAKE_PREFIX_PATH
        "${HOMEBREW_PREFIX}/opt/qt@6"
        "${HOMEBREW_PREFIX}/opt/sdl2"
    )
    
    # Find Qt6 from Homebrew
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network)
    
    # Find SDL2 from Homebrew via pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    
    # Poppler-Qt6 on macOS doesn't provide a .pc file, so we configure it manually
    set(POPPLER_PREFIX "/opt/poppler-qt6")
    message(STATUS "Using Poppler from: ${POPPLER_PREFIX}")
    
    # Include directories
    include_directories(
        ${CMAKE_CURRENT_BINARY_DIR}
        "${POPPLER_PREFIX}/include/poppler/qt6"
        "${HOMEBREW_PREFIX}/include"
    )
    
    # Link directories for Poppler
    link_directories("${POPPLER_PREFIX}/lib")
    
    # Set Poppler libraries manually
    set(POPPLER_LIBRARIES "poppler-qt6")
    
    # No Windows-specific resources
    set(WIN_RESOURCES "")
    
    # macOS CPU architecture optimization
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        # Apple Silicon (M1/M2/M3/M4) optimization
        message(STATUS "🚀 Optimizing for Apple Silicon (ARM64)")
        add_compile_options(
            -mcpu=apple-m1           # Target Apple M1 (compatible with M2/M3/M4)
            -O3                      # Maximum optimization level
            -ffast-math              # Aggressive floating-point optimizations
            -funroll-loops           # Unroll loops for better performance
            -fomit-frame-pointer     # Omit frame pointer for more registers
            -DNDEBUG                 # Disable debug assertions
        )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        # Intel Mac optimization - optimize for Nehalem (Core i series)
        message(STATUS "🚀 Optimizing for Intel Mac (x86-64, Nehalem+)")
        add_compile_options(
            -march=nehalem           # Target 1st gen Intel Core i
            -mtune=nehalem           # Tune for Nehalem
            -msse4.2                 # Enable SSE4.2
            -mpopcnt                 # Enable POPCNT instruction
            -O3                      # Maximum optimization level
            -ffast-math              # Aggressive floating-point optimizations
            -funroll-loops           # Unroll loops for better performance
            -fomit-frame-pointer     # Omit frame pointer for more registers
            -DNDEBUG                 # Disable debug assertions
        )
    else()
        # Generic fallback
        message(STATUS "Building for generic macOS (${CMAKE_SYSTEM_PROCESSOR})")
        add_compile_options(
            -O3
            -DNDEBUG
        )
    endif()
    
    # macOS-specific frameworks
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    find_library(AUDIOUNIT_FRAMEWORK AudioUnit)
    
elseif (UNIX)
    # Linux-specific configuration
    # Use system paths for Linux
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)

    # For SDL2, we use sdl2-compat which provides SDL2 compatibility
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

    # Include current binary directory for generated files
    include_directories(${CMAKE_CURRENT_BINARY_DIR})

    # No Windows-specific resources
    set(WIN_RESOURCES "")
    
    # Linux CPU architecture optimization
    # Detect architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        # x86-64 Linux build - optimize for 1st gen Intel Core i (Nehalem) with SSE4.2
        message(STATUS "🚀 Optimizing for x86-64 Linux (1st gen Intel Core i / Nehalem with SSE4.2)")
        add_compile_options(
            -march=nehalem           # Target 1st gen Intel Core i (Nehalem microarchitecture)
            -mtune=nehalem           # Tune for Nehalem specifically
            -msse4.2                 # Explicitly enable SSE4.2
            -mpopcnt                 # Enable POPCNT instruction (part of SSE4.2)
            -O3                      # Maximum optimization level
            -ffast-math              # Aggressive floating-point optimizations
            -funroll-loops           # Unroll loops for better performance
            -fomit-frame-pointer     # Omit frame pointer for more registers
            -DNDEBUG                 # Disable debug assertions
        )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        # ARM64 Linux build - optimize for older ARM cores (Cortex-A72/A53 big.LITTLE)
        message(STATUS "🚀 Optimizing for ARM64 Linux (Cortex-A72/A53 compatible)")
        add_compile_options(
            -march=armv8-a+crc+simd  # ARMv8-A baseline with CRC32 and NEON SIMD
            -mtune=cortex-a72        # Tune for Cortex-A72 (big cores in big.LITTLE)
            -O3                      # Maximum optimization level
            -ffast-math              # Aggressive floating-point optimizations
            -funroll-loops           # Unroll loops for better performance
            -fomit-frame-pointer     # Omit frame pointer for more registers
            -DNDEBUG                 # Disable debug assertions
        )
    else()
        # Generic fallback for other architectures
        message(STATUS "Building for generic Linux (${CMAKE_SYSTEM_PROCESSOR})")
        add_compile_options(
            -O3
            -DNDEBUG
        )
    endif()
endif ()

# Translation files
set(TS_FILES
        ${CMAKE_SOURCE_DIR}/resources/translations/app_zh.ts
        ${CMAKE_SOURCE_DIR}/resources/translations/app_es.ts
        ${CMAKE_SOURCE_DIR}/resources/translations/app_fr.ts
)

# ✅ Enable Qt automatic features
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ✅ QMarkdownTextEdit sources
set(QMARKDOWNTEXTEDIT_SOURCES
        markdown/qmarkdowntextedit.cpp
        markdown/markdownhighlighter.cpp
        markdown/qownlanguagedata.cpp
        markdown/qplaintexteditsearchwidget.cpp
)

set(QMARKDOWNTEXTEDIT_HEADERS
        markdown/qmarkdowntextedit.h
        markdown/markdownhighlighter.h
        markdown/qownlanguagedata.h
        markdown/qplaintexteditsearchwidget.h
        markdown/linenumberarea.h
)

# Process headers that contain Q_OBJECT for MOC
qt6_wrap_cpp(QMARKDOWNTEXTEDIT_MOC_SOURCES markdown/linenumberarea.h)

set(QMARKDOWNTEXTEDIT_UI
        markdown/qplaintexteditsearchwidget.ui
)

set(QMARKDOWNTEXTEDIT_RESOURCES
        markdown/media.qrc
)

# Process UI files to generate headers
qt6_wrap_ui(QMARKDOWNTEXTEDIT_UI_HEADERS ${QMARKDOWNTEXTEDIT_UI})

# ✅ Resources
set(RESOURCES resources.qrc ${QMARKDOWNTEXTEDIT_RESOURCES})
QT6_ADD_RESOURCES(QRCC_FILES ${RESOURCES})

# ✅ NEW ARCHITECTURE: Core document classes (Phase 1-3 migration)
# These files implement the new Page/Document/Viewport architecture
set(CORE_SOURCES
        source/core/Page.cpp           # Task 1.1.6
        source/core/Document.cpp       # Task 1.2.3
        source/core/DocumentViewport.cpp # Task 1.3.1
        source/core/DocumentManager.cpp  # Task 3.0.1
)

# ✅ NEW ARCHITECTURE: Vector layer system
set(LAYER_SOURCES
        # source/layers/VectorLayer.cpp  # Task 1.1.3
)

# ✅ NEW ARCHITECTURE: Inserted objects (images, text, shapes)
set(OBJECT_SOURCES
        source/objects/InsertedObject.cpp  # Task 1.1.4
        source/objects/ImageObject.cpp     # Task 1.1.5
)

# ✅ NEW ARCHITECTURE: Stroke data types (extracted from VectorCanvas)
set(STROKE_SOURCES
        # source/strokes/StrokePoint.cpp     # Task 1.1.2 (header-only, no cpp needed)
        # source/strokes/VectorStroke.cpp    # Task 1.1.2
)

# ✅ NEW ARCHITECTURE: UI components (Phase 3)
set(UI_SOURCES
        source/ui/TabManager.cpp             # Task 3.0.2
        source/ui/LayerPanel.cpp             # Task 3.0.3
)

# ✅ NEW ARCHITECTURE: PDF abstraction layer (Phase 1.2)
set(PDF_SOURCES
        source/pdf/PopplerPdfProvider.cpp  # Task 1.2.2
)

# ✅ Build target
add_executable(${PROJECT_NAME}
        source/Main.cpp
        source/MainWindow.cpp
        source/InkCanvas.cpp
        source/SimpleAudio.cpp
        source/ControlPanelDialog.cpp
        source/SDLControllerManager.cpp
        source/ButtonMappingTypes.cpp
        source/RecentNotebooksManager.cpp
        source/KeyCaptureDialog.cpp
        source/ControllerMappingDialog.cpp
        source/MarkdownNoteEntry.cpp
        source/MarkdownNotesSidebar.cpp
	  source/PictureWindow.cpp
    	  source/PictureWindowManager.cpp
        source/PdfOpenDialog.cpp
	  source/PdfRelinkDialog.cpp
    	  source/SpnPackageManager.cpp
    	  source/LauncherWindow.cpp
        source/PictureSourceDialog.cpp
        source/ExistingPictureDialog.cpp
        source/DocumentConverter.cpp

        # source/VectorCanvas.cpp  # REMOVED Phase 3.1.3 - Features migrated to DocumentViewport
        # source/VectorCanvas.h    # REMOVED Phase 3.1.3 - Features migrated to DocumentViewport
        
        # New architecture sources (uncomment as implemented)
        ${CORE_SOURCES}
        ${LAYER_SOURCES}
        ${UI_SOURCES}
        ${OBJECT_SOURCES}
        ${STROKE_SOURCES}
        ${PDF_SOURCES}
        
        ${QRCC_FILES}
        ${QMARKDOWNTEXTEDIT_SOURCES}
        ${QMARKDOWNTEXTEDIT_UI_HEADERS}
        ${QMARKDOWNTEXTEDIT_MOC_SOURCES}
        ${WIN_RESOURCES}
)

# ✅ Compile .ts → .qm using Qt's lrelease
find_program(LRELEASE_EXECUTABLE
        lrelease
        PATHS "${QT_PATH}/bin"
        NO_DEFAULT_PATH
)
if (WIN32)
    # Find lrelease based on architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        find_program(LRELEASE_EXECUTABLE lrelease PATHS "C:/msys64/clangarm64/bin")
    else()
        find_program(LRELEASE_EXECUTABLE lrelease PATHS "C:/msys64/clang64/bin")
    endif()
endif ()

if (LRELEASE_EXECUTABLE)
    foreach (_ts ${TS_FILES})
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
    endforeach ()

    # Add a target to build translations
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
endif ()

# Disable Qt deprecated warnings
# add_compile_definitions(QT_NO_DEPRECATED_WARNINGS)

# ✅ Link libraries - platform specific
if (WIN32)
    target_link_libraries(NoteApp
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml Qt6::Network
            SDL2::SDL2
            PkgConfig::POPPLER
            dsound  # For DirectSound API
            dxguid  # For DirectSound GUIDs
    )
elseif (APPLE)
    target_link_libraries(NoteApp
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml Qt6::Network
            PkgConfig::SDL2
            ${POPPLER_LIBRARIES}
            ${COREAUDIO_FRAMEWORK}
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${AUDIOUNIT_FRAMEWORK}
    )
elseif (UNIX)
    target_link_libraries(NoteApp
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Xml Qt6::Network
            PkgConfig::SDL2
            PkgConfig::POPPLER
            asound  # For ALSA audio
    )
endif ()

# ✅ Set output name for macOS and Linux
if (APPLE OR UNIX)
    set_target_properties(NoteApp PROPERTIES OUTPUT_NAME NoteApp)
endif ()

# ✅ Install targets for macOS
if (APPLE)
    # Install the main executable
    install(TARGETS NoteApp DESTINATION bin)

    # Install translation files if they exist
    if (QM_FILES)
        install(FILES ${QM_FILES} DESTINATION share/speedynote/translations)
    endif ()
    
    # Also install resource translation files
    file(GLOB RESOURCE_QM_FILES "resources/translations/*.qm")
    if (RESOURCE_QM_FILES)
        install(FILES ${RESOURCE_QM_FILES} DESTINATION share/speedynote/translations)
    endif ()

    # Install icon for macOS
    install(FILES resources/icons/mainicon.png
            DESTINATION share/speedynote/icons)
endif ()

# ✅ Install targets for Linux/Flatpak
if (UNIX AND NOT APPLE)
    # Install the main executable
    install(TARGETS NoteApp DESTINATION bin)

    # Install translation files if they exist
    if (QM_FILES)
        install(FILES ${QM_FILES} DESTINATION share/speedynote/translations)
    endif ()
    
    # Also install resource translation files
    file(GLOB RESOURCE_QM_FILES "resources/translations/*.qm")
    if (RESOURCE_QM_FILES)
        install(FILES ${RESOURCE_QM_FILES} DESTINATION share/speedynote/translations)
    endif ()

    # Install icon with app ID name for proper Flatpak desktop integration
    install(FILES resources/icons/mainicon.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME com.github.alpha_liu_01.SpeedyNote.png)

    # Install desktop file and metainfo (handled by Flatpak post-install)
endif ()
