cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0074 NEW)

project(SpeedyNote VERSION 1.6.3 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Build Options
# ============================================================================
option(ENABLE_DEBUG_OUTPUT "Enable verbose debug output (qDebug prints)" OFF)
option(ENABLE_SANITIZERS "Enable AddressSanitizer + LeakSanitizer for memory debugging" OFF)
option(SPEEDYNOTE_ENABLE_WINDOWS_INK_OCR "Enable Windows Ink handwriting recognition (requires Windows 10 1703+)" OFF)
option(SPEEDYNOTE_ENABLE_MLKIT_INK_OCR "Enable ML Kit Digital Ink OCR (Android/iOS)" OFF)
option(SPEEDYNOTE_ENABLE_VISION_OCR "Enable Apple Vision OCR (macOS)" OFF)
option(SPEEDYNOTE_ENABLE_PADDLE_OCR "Enable PaddleOCR raster OCR (Linux, via vendored ONNX Runtime)" OFF)

if(ENABLE_DEBUG_OUTPUT)
    add_compile_definitions(SPEEDYNOTE_DEBUG)
    # Absolute source dir so debug-only golden-image tests can locate baselines
    # under tests/ocr/golden/ at runtime (see source/ocr/OcrGoldenTests.h).
    add_compile_definitions(SPEEDYNOTE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    message(STATUS "Debug output: ENABLED")
else()
    message(STATUS "Debug output: DISABLED")
endif()

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
    message(STATUS "Sanitizers: AddressSanitizer ENABLED")
endif()

# ============================================================================
# Qt Version Selection
# ============================================================================
option(USE_QT5 "Build with Qt5 instead of Qt6 (for 32-bit Windows, HarmonyOS, etc.)" OFF)
if(USE_QT5)
    set(QT_VERSION_MAJOR 5)
    message(STATUS "Qt version: Qt5")
else()
    set(QT_VERSION_MAJOR 6)
    message(STATUS "Qt version: Qt6")
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

# iOS detection: Qt's iOS toolchain sets CMAKE_SYSTEM_NAME to "iOS".
# Define IOS early so it can be tested before APPLE (which is true for both macOS and iOS).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(IOS TRUE)
endif()

# Auto-enable ML Kit Digital Ink OCR on Android/iOS
if(ANDROID OR IOS)
    set(SPEEDYNOTE_ENABLE_MLKIT_INK_OCR ON CACHE BOOL "" FORCE)
    message(STATUS "ML Kit Digital Ink OCR: ENABLED (${CMAKE_SYSTEM_NAME})")
endif()

# Auto-enable Apple Vision OCR on macOS desktop (APPLE is true for iOS too,
# so the explicit "AND NOT IOS" guard is required).
if(APPLE AND NOT IOS)
    set(SPEEDYNOTE_ENABLE_VISION_OCR ON CACHE BOOL "" FORCE)
    message(STATUS "Apple Vision OCR: ENABLED")
endif()

# The Vision bridge is Objective-C++ (.mm). macOS desktop builds otherwise use
# only C/C++, so OBJCXX must be enabled explicitly (the iOS branch does the same
# for the ML Kit bridge).
if(SPEEDYNOTE_ENABLE_VISION_OCR)
    enable_language(OBJCXX)
endif()

# Auto-enable PaddleOCR raster OCR on Linux desktop, but only when the vendored
# ONNX Runtime AND the mandatory default recognition model are both present
# (mirrors the iOS ML Kit auto-disable pattern: graceful, no hard failure).
# Populate them with linux/fetch-onnxruntime.sh + linux/fetch-ocr-models.sh.
if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/linux/onnxruntime-build")
    file(GLOB _ort_libs "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so*")
    if(_ort_libs
       AND EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
       AND EXISTS "${CMAKE_SOURCE_DIR}/linux/ocr-models/latin_rec.onnx")
        set(SPEEDYNOTE_ENABLE_PADDLE_OCR ON CACHE BOOL "" FORCE)
        message(STATUS "PaddleOCR raster OCR: ENABLED (ONNX Runtime + models found)")
    else()
        set(SPEEDYNOTE_ENABLE_PADDLE_OCR OFF CACHE BOOL "" FORCE)
        message(WARNING "PaddleOCR raster OCR: DISABLED -- vendored ONNX Runtime or "
                        "default model not found under linux/. OCR will be unavailable "
                        "on Linux. Run linux/fetch-onnxruntime.sh and "
                        "linux/fetch-ocr-models.sh, then reconfigure.")
    endif()
endif()

if(WIN32)
    # =========================================================================
    # Windows Configuration
    # =========================================================================
    message(STATUS "🪟 Configuring for Windows")
    
    # Detect architecture and set toolchain paths
    # Only set defaults if not already provided via -DQT_PATH=...
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        if(NOT DEFINED QT_PATH OR QT_PATH STREQUAL "")
            set(QT_PATH "C:/msys64/clangarm64" CACHE PATH "Path to Qt installation")
        endif()
        set(MSYS2_PACKAGE_PREFIX "mingw-w64-clang-aarch64")
        message(STATUS "Detected ARM64 Windows - using clangarm64 toolchain")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        if(NOT DEFINED QT_PATH OR QT_PATH STREQUAL "")
            set(QT_PATH "C:/Qt/5.15.2/mingw81_32" CACHE PATH "Path to Qt installation")
        endif()
        set(MSYS2_PACKAGE_PREFIX "mingw-w64-i686")
        message(STATUS "Detected x86 (32-bit) Windows - using standalone Qt5 SDK")
    else()
        if(NOT DEFINED QT_PATH OR QT_PATH STREQUAL "")
            set(QT_PATH "C:/msys64/clang64" CACHE PATH "Path to Qt installation")
        endif()
        set(MSYS2_PACKAGE_PREFIX "mingw-w64-clang-x86_64")
        message(STATUS "Detected x86_64 Windows - using clang64 toolchain")
    endif()
    
    # Convert paths to forward slashes to avoid escaping issues with backslashes
    file(TO_CMAKE_PATH "${QT_PATH}" QT_PATH)
    message(STATUS "QT_PATH: ${QT_PATH}")

    # CMake paths
    set(CMAKE_PREFIX_PATH "${QT_PATH}/lib/cmake")
    include_directories(${CMAKE_CURRENT_BINARY_DIR})

    # Find dependencies
    if(ENABLE_DEBUG_OUTPUT)
        find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Svg Test)
    else()
        find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Svg)
    endif()
    if(USE_QT5)
        find_package(PkgConfig QUIET)
    else()
        find_package(PkgConfig REQUIRED)
    endif()
    # Poppler removed - using MuPDF exclusively for PDF rendering

    # Auto-enable Windows Ink OCR on 64-bit Qt6 Windows builds
    if(NOT USE_QT5 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SPEEDYNOTE_ENABLE_WINDOWS_INK_OCR ON CACHE BOOL "" FORCE)
        message(STATUS "Windows Ink OCR: ENABLED (64-bit Qt6 Windows)")
    else()
        message(STATUS "Windows Ink OCR: DISABLED (requires 64-bit Qt6)")
    endif()

    # MuPDF for PDF export
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        # -----------------------------------------------------------------
        # 32-bit Windows: cross-compiled MuPDF from win32/mupdf-build/
        # Built by private_scripts/build-mupdf-win32.sh (all deps bundled)
        # -----------------------------------------------------------------
        set(MUPDF_WIN32_DIR "${CMAKE_SOURCE_DIR}/win32/mupdf-build")
        if(NOT MUPDF_INCLUDE_DIR)
            set(MUPDF_INCLUDE_DIR "${MUPDF_WIN32_DIR}/include")
        endif()
        if(EXISTS "${MUPDF_WIN32_DIR}/lib/libmupdf.a")
            message(STATUS "✅ MuPDF found for PDF export (static, win32)")
            add_compile_definitions(SPEEDYNOTE_MUPDF_EXPORT)
            set(MUPDF_FOUND TRUE)
            set(MUPDF_LIBRARIES
                "${MUPDF_WIN32_DIR}/lib/libmupdf.a"
                "${MUPDF_WIN32_DIR}/lib/libmupdf-third.a")
        else()
            message(STATUS "⚠️ MuPDF not found - PDF export will be disabled")
            message(STATUS "   Build with: ./private_scripts/build-mupdf-win32.sh (in MSYS2)")
            set(MUPDF_FOUND FALSE)
        endif()
    else()
        # -----------------------------------------------------------------
        # 64-bit Windows: MuPDF via MSYS2 packages
        # Try dynamic linking first (libmupdf.dll.a), fallback to static
        # -----------------------------------------------------------------
        set(MUPDF_INCLUDE_DIR "${QT_PATH}/include")

        if(EXISTS "${QT_PATH}/lib/libmupdf.dll.a")
            set(MUPDF_LIBRARY "${QT_PATH}/lib/libmupdf.dll.a")
            message(STATUS "✅ MuPDF found for PDF export (dynamic): ${MUPDF_LIBRARY}")
            add_compile_definitions(SPEEDYNOTE_MUPDF_EXPORT)
            set(MUPDF_FOUND TRUE)
            set(MUPDF_LIBRARIES ${MUPDF_LIBRARY})

            if(EXISTS "${QT_PATH}/lib/libmujs.dll.a")
                list(APPEND MUPDF_LIBRARIES "${QT_PATH}/lib/libmujs.dll.a")
                message(STATUS "   Found mujs (dynamic)")
            endif()
            message(STATUS "   Note: Requires libmupdf.dll at runtime (copied by ntldd)")

        elseif(EXISTS "${QT_PATH}/lib/libmupdf.a")
            set(MUPDF_LIBRARY "${QT_PATH}/lib/libmupdf.a")
            message(STATUS "✅ MuPDF found for PDF export (static): ${MUPDF_LIBRARY}")
            add_compile_definitions(SPEEDYNOTE_MUPDF_EXPORT)
            set(MUPDF_FOUND TRUE)
            set(MUPDF_LIBRARIES ${MUPDF_LIBRARY})

            if(EXISTS "${QT_PATH}/lib/libmupdf-third.a")
                list(APPEND MUPDF_LIBRARIES "${QT_PATH}/lib/libmupdf-third.a")
                message(STATUS "   Found mupdf-third")
            endif()

            if(EXISTS "${QT_PATH}/lib/libmujs.a")
                list(APPEND MUPDF_LIBRARIES "${QT_PATH}/lib/libmujs.a")
                message(STATUS "   Found mujs (static)")
            elseif(EXISTS "${QT_PATH}/lib/libmujs.dll.a")
                list(APPEND MUPDF_LIBRARIES "${QT_PATH}/lib/libmujs.dll.a")
                message(STATUS "   Found mujs (dynamic)")
            endif()

            # Static MuPDF needs these dependencies via pkg-config
            pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
            pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
            pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)
            pkg_check_modules(LIBOPENJP2 REQUIRED IMPORTED_TARGET libopenjp2)
            pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
            pkg_check_modules(JBIG2DEC QUIET IMPORTED_TARGET jbig2dec)
            pkg_check_modules(GUMBO QUIET IMPORTED_TARGET gumbo)
            pkg_check_modules(BROTLIDEC QUIET IMPORTED_TARGET libbrotlidec)
            pkg_check_modules(BROTLIENC QUIET IMPORTED_TARGET libbrotlienc)

            list(APPEND MUPDF_LIBRARIES
                PkgConfig::HARFBUZZ PkgConfig::FREETYPE PkgConfig::LIBJPEG
                PkgConfig::LIBOPENJP2 PkgConfig::ZLIB)
            if(JBIG2DEC_FOUND)
                list(APPEND MUPDF_LIBRARIES PkgConfig::JBIG2DEC)
            endif()
            if(GUMBO_FOUND)
                list(APPEND MUPDF_LIBRARIES PkgConfig::GUMBO)
            endif()
            if(BROTLIDEC_FOUND)
                list(APPEND MUPDF_LIBRARIES PkgConfig::BROTLIDEC)
            endif()
            if(BROTLIENC_FOUND)
                list(APPEND MUPDF_LIBRARIES PkgConfig::BROTLIENC)
            endif()
            message(STATUS "   Note: Statically linked (no libmupdf.dll needed)")
        else()
            message(STATUS "⚠️ MuPDF not found - PDF export will be disabled")
            message(STATUS "   Checked: ${QT_PATH}/lib/libmupdf.dll.a (dynamic)")
            message(STATUS "   Checked: ${QT_PATH}/lib/libmupdf.a (static)")
            message(STATUS "   Install with: pacman -S ${MSYS2_PACKAGE_PREFIX}-mupdf")
            set(MUPDF_FOUND FALSE)
        endif()
    endif()
    
    message(STATUS "   MuPDF libraries: ${MUPDF_LIBRARIES}")

    # Add MuPDF include directory if found
    if(MUPDF_FOUND)
        include_directories(${MUPDF_INCLUDE_DIR})
    endif()

    # CPU optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        message(STATUS "🚀 Optimizing for ARM64 Windows (Snapdragon)")
        add_compile_options(-mcpu=cortex-a75 -march=armv8.2-a+fp16+rcpc+dotprod
            -O3 -ffast-math -ftree-vectorize -fvectorize -flto=thin
            -fomit-frame-pointer -funroll-loops -DNDEBUG)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=thin")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(STATUS "🚀 Optimizing for x86 (32-bit) Windows (Core Duo / Atom+)")
        add_compile_options(-march=prescott -mtune=generic -mfpmath=sse
            -O3 -ffast-math -DNDEBUG)
    else()
        message(STATUS "🚀 Optimizing for x86_64 Windows (Nehalem+)")
        add_compile_options(-march=nehalem -msse4.2 -O3 -ffast-math -DNDEBUG)
    endif()

    # Windows resources (version substituted from project(VERSION ...))
    enable_language(RC)
    configure_file(app_icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc" @ONLY)
    set(WIN_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")

    # Architecture suffix used in the installer filename and as a key for the
    # Inno Setup #if MyArch == "arm64" gate (see packaging.iss.in).
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64|AARCH64")
        set(WIN_INSTALLER_ARCH "arm64")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(WIN_INSTALLER_ARCH "x86")
    else()
        set(WIN_INSTALLER_ARCH "amd64")
    endif()

    # Inno Setup script (version substituted from project(VERSION ...))
    configure_file(packaging.iss.in "${CMAKE_SOURCE_DIR}/packaging.iss" @ONLY)

elseif(IOS)
    # =========================================================================
    # iPadOS Configuration
    # =========================================================================
    message(STATUS "📱 Configuring for iPadOS")
    
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "Minimum iPadOS version")
    
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Svg)
    
    # MuPDF paths (cross-compiled separately via ios/build-mupdf.sh)
    # Libraries and headers are under ios/mupdf-build/.
    if(NOT MUPDF_INCLUDE_DIR)
        set(MUPDF_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/ios/mupdf-build/include")
    endif()
    if(NOT MUPDF_LIBRARIES)
        set(MUPDF_LIBRARIES
            "${CMAKE_SOURCE_DIR}/ios/mupdf-build/lib/libmupdf.a"
            "${CMAKE_SOURCE_DIR}/ios/mupdf-build/lib/libmupdf-third.a")
    endif()
    
    include_directories(${CMAKE_CURRENT_BINARY_DIR} ${MUPDF_INCLUDE_DIR})
    
    # MuPDF is always available on iOS (cross-compiled in Phase 2)
    add_compile_definitions(SPEEDYNOTE_MUPDF_EXPORT)
    message(STATUS "✅ MuPDF PDF export enabled for iPadOS")
    
    set(WIN_RESOURCES "")

elseif(APPLE)
    # =========================================================================
    # macOS Configuration
    # =========================================================================
    message(STATUS "🍎 Configuring for macOS")
    
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
    
    # Homebrew paths
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(HOMEBREW_PREFIX "/opt/homebrew")
        message(STATUS "Detected Apple Silicon - using ${HOMEBREW_PREFIX}")
    else()
        set(HOMEBREW_PREFIX "/usr/local")
        message(STATUS "Detected Intel Mac - using ${HOMEBREW_PREFIX}")
    endif()
    
    if(USE_QT5)
        set(CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}/opt/qt@5")
    else()
        set(CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}/opt/qt@6")
    endif()
    include_directories(${CMAKE_CURRENT_BINARY_DIR} "${HOMEBREW_PREFIX}/include")
    
    # Find dependencies
    if(ENABLE_DEBUG_OUTPUT)
        find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Svg Test)
    else()
        find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Svg)
    endif()
    find_package(PkgConfig REQUIRED)
    # macOS uses MuPDF exclusively for PDF rendering and export (no Poppler)
    
    # MuPDF for PDF rendering and export (via Homebrew)
    # Homebrew may install MuPDF in different locations depending on version/linking
    set(MUPDF_FOUND FALSE)
    set(MUPDF_SEARCH_PATHS
        "${HOMEBREW_PREFIX}/opt/mupdf"      # Homebrew keg-only location (most common)
        "${HOMEBREW_PREFIX}"                 # Direct Homebrew prefix
        "${HOMEBREW_PREFIX}/opt/mupdf-tools" # Alternative package name
    )
    
    foreach(MUPDF_PATH ${MUPDF_SEARCH_PATHS})
        # Check for static library first (preferred for bundling)
        if(EXISTS "${MUPDF_PATH}/lib/libmupdf.a")
            set(MUPDF_INCLUDE_DIR "${MUPDF_PATH}/include")
            set(MUPDF_LIBRARY_MAIN "${MUPDF_PATH}/lib/libmupdf.a")
            set(MUPDF_LIBRARY_THIRD "${MUPDF_PATH}/lib/libmupdf-third.a")
            message(STATUS "✅ MuPDF found (static): ${MUPDF_PATH}")
            set(MUPDF_FOUND TRUE)
            break()
        # Check for shared library as fallback
        elseif(EXISTS "${MUPDF_PATH}/lib/libmupdf.dylib")
            set(MUPDF_INCLUDE_DIR "${MUPDF_PATH}/include")
            set(MUPDF_LIBRARY_MAIN "${MUPDF_PATH}/lib/libmupdf.dylib")
            set(MUPDF_LIBRARY_THIRD "")  # Shared builds don't have -third
            message(STATUS "✅ MuPDF found (shared): ${MUPDF_PATH}")
            set(MUPDF_FOUND TRUE)
            break()
        endif()
    endforeach()
    
    if(MUPDF_FOUND)
        add_compile_definitions(SPEEDYNOTE_MUPDF_EXPORT)
        
        # MuPDF requires these system libraries via pkg-config
        pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
        pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
        pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)
        pkg_check_modules(LIBOPENJP2 REQUIRED IMPORTED_TARGET libopenjp2)
        pkg_check_modules(JBIG2DEC QUIET IMPORTED_TARGET jbig2dec)
        pkg_check_modules(GUMBO QUIET IMPORTED_TARGET gumbo)
        pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
        pkg_check_modules(BROTLIDEC QUIET IMPORTED_TARGET libbrotlidec)
        pkg_check_modules(BROTLIENC QUIET IMPORTED_TARGET libbrotlienc)
        
        # Search for mujs in multiple locations
        find_library(MUJS_LIBRARY NAMES mujs 
            PATHS "${HOMEBREW_PREFIX}/lib" "${HOMEBREW_PREFIX}/opt/mujs/lib"
            NO_DEFAULT_PATH)
        
        # Start with main library
        set(MUPDF_LIBRARIES ${MUPDF_LIBRARY_MAIN})
        
        # libmupdf-third.a is optional - Homebrew may bundle deps into main lib
        if(MUPDF_LIBRARY_THIRD AND EXISTS "${MUPDF_LIBRARY_THIRD}")
            list(APPEND MUPDF_LIBRARIES ${MUPDF_LIBRARY_THIRD})
            message(STATUS "   Found mupdf-third: ${MUPDF_LIBRARY_THIRD}")
        endif()
        
        # Add pkg-config dependencies (including zlib and brotli for MuPDF)
        list(APPEND MUPDF_LIBRARIES
            PkgConfig::HARFBUZZ
            PkgConfig::FREETYPE
            PkgConfig::LIBJPEG
            PkgConfig::LIBOPENJP2
            PkgConfig::ZLIB
        )
        if(BROTLIDEC_FOUND)
            list(APPEND MUPDF_LIBRARIES PkgConfig::BROTLIDEC)
            message(STATUS "   Found brotli decoder")
        endif()
        if(BROTLIENC_FOUND)
            list(APPEND MUPDF_LIBRARIES PkgConfig::BROTLIENC)
            message(STATUS "   Found brotli encoder")
        endif()
        if(JBIG2DEC_FOUND)
            list(APPEND MUPDF_LIBRARIES PkgConfig::JBIG2DEC)
        endif()
        if(GUMBO_FOUND)
            list(APPEND MUPDF_LIBRARIES PkgConfig::GUMBO)
        endif()
        if(MUJS_LIBRARY)
            list(APPEND MUPDF_LIBRARIES ${MUJS_LIBRARY})
            message(STATUS "   Found mujs: ${MUJS_LIBRARY}")
        endif()
    else()
        message(STATUS "⚠️ MuPDF not found - PDF export will be disabled")
        message(STATUS "   Searched in:")
        foreach(MUPDF_PATH ${MUPDF_SEARCH_PATHS})
            message(STATUS "     - ${MUPDF_PATH}/lib/libmupdf.a")
            message(STATUS "     - ${MUPDF_PATH}/lib/libmupdf.dylib")
        endforeach()
        message(STATUS "   Install with: brew install mupdf")
    endif()

    # Add MuPDF include directory if found
    if(MUPDF_FOUND)
        include_directories(${MUPDF_INCLUDE_DIR})
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
    
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network Svg)
    
    # MuPDF paths (cross-compiled separately, per-ABI)
    # ANDROID_ABI is set by the NDK toolchain (arm64-v8a, armeabi-v7a, etc.)
    # Libraries are ABI-specific; headers are shared across all ABIs.
    if(NOT MUPDF_INCLUDE_DIR)
        set(MUPDF_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/android/mupdf-build/include")
    endif()
    if(NOT MUPDF_LIBRARIES)
        set(MUPDF_LIBRARIES
            "${CMAKE_SOURCE_DIR}/android/mupdf-build/${ANDROID_ABI}/lib/libmupdf.a"
            "${CMAKE_SOURCE_DIR}/android/mupdf-build/${ANDROID_ABI}/lib/libmupdf-third.a")
    endif()
    
    include_directories(${CMAKE_CURRENT_BINARY_DIR} ${MUPDF_INCLUDE_DIR})
    
    # Enable MuPDF export on Android (MuPDF is always available on Android)
    add_compile_definitions(SPEEDYNOTE_MUPDF_EXPORT)
    message(STATUS "✅ MuPDF PDF export enabled for Android")
    
    set(WIN_RESOURCES "")
    
elseif(UNIX)
    # =========================================================================
    # Linux Configuration
    # =========================================================================
    message(STATUS "🐧 Configuring for Linux")
    
    # -------------------------------------------------------------------------
    # As of v1.2.1, SpeedyNote uses MuPDF exclusively on all platforms.
    # This eliminates symbol conflicts between MuPDF and Poppler/OpenJPEG.
    # -------------------------------------------------------------------------
    
    if(ENABLE_DEBUG_OUTPUT)
        find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network DBus Svg Test)
    else()
        find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network DBus Svg)
    endif()
    find_package(PkgConfig REQUIRED)
    # Poppler removed - using MuPDF exclusively for PDF rendering

    # MuPDF for PDF export (prefer dynamic linking)
    # Search in standard paths: /usr/lib, /usr/lib64 (Fedora), /usr/lib/x86_64-linux-gnu (Debian)
    # MUPDF_INCLUDE_DIR can be overridden via -D (e.g. for Flatpak where headers are in /app/include)
    if(NOT MUPDF_INCLUDE_DIR)
        set(MUPDF_INCLUDE_DIR "/usr/include")
    endif()
    
    # First try to find dynamic library (libmupdf.so)
    find_library(MUPDF_LIBRARY_DYNAMIC NAMES libmupdf.so mupdf
        PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
    
    if(MUPDF_LIBRARY_DYNAMIC AND NOT MUPDF_LIBRARY_DYNAMIC MATCHES "\\.a$")
        # Found dynamic library
        message(STATUS "✅ MuPDF found for PDF export (dynamic): ${MUPDF_LIBRARY_DYNAMIC}")
        add_compile_definitions(SPEEDYNOTE_MUPDF_EXPORT)
        set(MUPDF_FOUND TRUE)
        
        # Dynamic linking - just need the shared library
        set(MUPDF_LIBRARIES ${MUPDF_LIBRARY_DYNAMIC})
        
        # mujs may also be dynamically linked
        find_library(MUJS_LIBRARY NAMES libmujs.so mujs
            PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
        if(MUJS_LIBRARY AND NOT MUJS_LIBRARY MATCHES "\\.a$")
            list(APPEND MUPDF_LIBRARIES ${MUJS_LIBRARY})
            message(STATUS "   Found mujs: ${MUJS_LIBRARY}")
        endif()
        
        message(STATUS "   Note: Requires libmupdf.so at runtime")
    else()
        # No dynamic library found - try static as fallback
        find_library(MUPDF_LIBRARY_STATIC NAMES libmupdf.a
            PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu
            NO_DEFAULT_PATH)
        
        if(MUPDF_LIBRARY_STATIC)
            message(STATUS "✅ MuPDF found for PDF export (static): ${MUPDF_LIBRARY_STATIC}")
            add_compile_definitions(SPEEDYNOTE_MUPDF_EXPORT)
            set(MUPDF_FOUND TRUE)
            
            # Static linking requires additional dependencies
            pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
            pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
            pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)
            pkg_check_modules(LIBOPENJP2 REQUIRED IMPORTED_TARGET libopenjp2)
            pkg_check_modules(JBIG2DEC QUIET IMPORTED_TARGET jbig2dec)
            pkg_check_modules(GUMBO QUIET IMPORTED_TARGET gumbo)
            
            find_library(MUPDF_LIBRARY_THIRD NAMES libmupdf-third.a
                PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu
                NO_DEFAULT_PATH)
            find_library(MUJS_LIBRARY NAMES mujs
                PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
            
            set(MUPDF_LIBRARIES ${MUPDF_LIBRARY_STATIC})
            if(MUPDF_LIBRARY_THIRD)
                list(APPEND MUPDF_LIBRARIES ${MUPDF_LIBRARY_THIRD})
                message(STATUS "   Found mupdf-third: ${MUPDF_LIBRARY_THIRD}")
            endif()
            
            list(APPEND MUPDF_LIBRARIES
                PkgConfig::HARFBUZZ PkgConfig::FREETYPE PkgConfig::LIBJPEG PkgConfig::LIBOPENJP2
                m pthread z)
            if(JBIG2DEC_FOUND)
                list(APPEND MUPDF_LIBRARIES PkgConfig::JBIG2DEC)
            endif()
            if(GUMBO_FOUND)
                list(APPEND MUPDF_LIBRARIES PkgConfig::GUMBO)
            endif()
            if(MUJS_LIBRARY)
                list(APPEND MUPDF_LIBRARIES ${MUJS_LIBRARY})
                message(STATUS "   Found mujs: ${MUJS_LIBRARY}")
            endif()
        else()
            message(STATUS "⚠️ MuPDF not found - PDF export will be disabled")
            message(STATUS "   Install with: sudo apt install libmupdf-dev (Debian/Ubuntu)")
            message(STATUS "              or: sudo dnf install mupdf-devel (Fedora/RHEL)")
            set(MUPDF_FOUND FALSE)
        endif()
    endif()

    include_directories(${CMAKE_CURRENT_BINARY_DIR} ${MUPDF_INCLUDE_DIR})

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
# Auto-discover all app_*.ts files so new community-contributed languages are
# picked up automatically without requiring changes to this file.
# ============================================================================
file(GLOB TS_FILES "${CMAKE_SOURCE_DIR}/resources/translations/app_*.ts")

# ============================================================================
# QMarkdownTextEdit (bundled library)
# ============================================================================
set(QMARKDOWNTEXTEDIT_SOURCES
        markdown/qmarkdowntextedit.cpp
        markdown/markdownhighlighter.cpp
        markdown/qownlanguagedata.cpp
        markdown/qplaintexteditsearchwidget.cpp
)

if(USE_QT5)
    qt5_wrap_cpp(QMARKDOWNTEXTEDIT_MOC_SOURCES markdown/linenumberarea.h)
    qt5_wrap_ui(QMARKDOWNTEXTEDIT_UI_HEADERS markdown/qplaintexteditsearchwidget.ui)
else()
    qt6_wrap_cpp(QMARKDOWNTEXTEDIT_MOC_SOURCES markdown/linenumberarea.h)
    qt6_wrap_ui(QMARKDOWNTEXTEDIT_UI_HEADERS markdown/qplaintexteditsearchwidget.ui)
endif()

# ============================================================================
# ============================================================================
# Qt translation catalogs (qtbase_<lang>.qm)
# ============================================================================
# These hold the QMessageBox / QFileDialog standard button strings
# (Save / Discard / Cancel / Open / Yes / No / ...). Without them the loader
# in source/Main.cpp falls back to English even when the rest of the UI is
# translated.
#
# Two deploy paths:
#   - Desktop (Windows / macOS / Linux): copied as loose files next to the
#     binary or into share/speedynote/translations via _install_qtbase_translations.
#   - Mobile (iOS / Android): no on-disk filesystem layout to rely on, so we
#     embed the files into the binary via a generated qt_translations.qrc
#     and the loader picks them up from :/qt-translations/.
#
# IMPORTANT: When a new app language is added, also extend the list below,
# the compile.ps1 $supportedQtbase array, and the qtbase_files arrays in
# build-package.sh (helper + spec/PKGBUILD/APKBUILD heredocs).
set(_SUPPORTED_QTBASE_QM
    qtbase_de.qm
    qtbase_es.qm
    qtbase_fr.qm
    qtbase_pt_BR.qm
    qtbase_zh_CN.qm
    qtbase_en.qm
)

set(_QT_TRANSLATIONS_DIR "")
if(TARGET Qt6::qmake)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
elseif(TARGET Qt5::qmake)
    get_target_property(_qmake_exe Qt5::qmake IMPORTED_LOCATION)
else()
    set(_qmake_exe "")
endif()
if(_qmake_exe AND EXISTS "${_qmake_exe}")
    execute_process(
        COMMAND "${_qmake_exe}" -query QT_INSTALL_TRANSLATIONS
        OUTPUT_VARIABLE _QT_TRANSLATIONS_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _qmake_query_result
        ERROR_QUIET
    )
    if(NOT _qmake_query_result EQUAL 0)
        set(_QT_TRANSLATIONS_DIR "")
    endif()
endif()

# Helper used by the desktop install section below. Emits one install(FILES)
# rule per existing qtbase_*.qm; missing catalogs are silently skipped.
function(_install_qtbase_translations DEST)
    if(NOT _QT_TRANSLATIONS_DIR)
        return()
    endif()
    foreach(_qm IN LISTS _SUPPORTED_QTBASE_QM)
        if(EXISTS "${_QT_TRANSLATIONS_DIR}/${_qm}")
            install(FILES "${_QT_TRANSLATIONS_DIR}/${_qm}" DESTINATION "${DEST}")
        endif()
    endforeach()
endfunction()

# Mobile builds: embed qtbase_*.qm into the binary via a generated qrc.
# We can't rely on the on-disk paths Main.cpp's loader uses (no /usr/share,
# applicationDirPath() is the app data sandbox, and androiddeployqt's own
# translation bundling only includes the legacy qt_*.qm aggregated catalog
# which lacks the Qt 6 widget strings). The loader checks :/qt-translations
# as a universal fallback so this works on every platform if needed.
set(_QT_TRANSLATIONS_QRC "")
if(IOS OR ANDROID)
    if(_QT_TRANSLATIONS_DIR)
        set(_qrc_body "<RCC>\n  <qresource prefix=\"/qt-translations\">\n")
        set(_qrc_has_files FALSE)
        foreach(_qm IN LISTS _SUPPORTED_QTBASE_QM)
            set(_qm_src "${_QT_TRANSLATIONS_DIR}/${_qm}")
            if(EXISTS "${_qm_src}")
                string(APPEND _qrc_body
                    "    <file alias=\"${_qm}\">${_qm_src}</file>\n")
                set(_qrc_has_files TRUE)
            endif()
        endforeach()
        string(APPEND _qrc_body "  </qresource>\n</RCC>\n")
        if(_qrc_has_files)
            # Two-step write so the qrc mtime only updates when content
            # actually changes, avoiding spurious rcc reruns.
            set(_qrc_tmp "${CMAKE_BINARY_DIR}/qt_translations.qrc.tmp")
            set(_QT_TRANSLATIONS_QRC "${CMAKE_BINARY_DIR}/qt_translations.qrc")
            file(WRITE "${_qrc_tmp}" "${_qrc_body}")
            configure_file("${_qrc_tmp}" "${_QT_TRANSLATIONS_QRC}" COPYONLY)
            message(STATUS "Embedding Qt translations from ${_QT_TRANSLATIONS_DIR} into qt_translations.qrc")
        else()
            message(WARNING "No qtbase_*.qm found in ${_QT_TRANSLATIONS_DIR}; mobile build will render standard buttons in English")
        endif()
    else()
        message(WARNING "Qt translation prefix not discovered; mobile build will render standard buttons in English")
    endif()
endif()

# ============================================================================
# Resources
# ============================================================================
set(RESOURCES resources.qrc markdown/media.qrc)
if(_QT_TRANSLATIONS_QRC)
    list(APPEND RESOURCES "${_QT_TRANSLATIONS_QRC}")
endif()
if(USE_QT5)
    qt5_add_resources(QRCC_FILES ${RESOURCES})
else()
    qt6_add_resources(QRCC_FILES ${RESOURCES})
endif()

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
    source/core/DarkModeUtils.cpp
)

# Inserted objects (images, links, etc.)
set(OBJECT_SOURCES
    source/objects/InsertedObject.cpp
    source/objects/ImageObject.cpp
    source/objects/LinkObject.cpp
    source/objects/TextBoxObject.cpp
    source/objects/OcrTextObject.cpp
)

# OCR engine abstraction and worker
set(OCR_SOURCES
    source/ocr/OcrEngine.cpp
    source/ocr/OcrWorker.cpp
)
# Platform-agnostic raster OCR pipeline (Phase 4A). Pure Qt; shared base for
# the desktop image-OCR backends (macOS Vision, Linux PaddleOCR/ONNX). No
# backend/option needed yet -- createBest() is unchanged until 4B/4C.
if(NOT ANDROID AND NOT IOS)
    list(APPEND OCR_SOURCES
        source/ocr/OcrStrokeRasterizer.cpp
        source/ocr/engines/RasterOcrEngine.cpp)
endif()
if(SPEEDYNOTE_ENABLE_WINDOWS_INK_OCR)
    list(APPEND OCR_SOURCES source/ocr/engines/WindowsInkOcrEngine.cpp)
    # C++/WinRT headers require C++20 for coroutine support
    set_source_files_properties(source/ocr/engines/WindowsInkOcrEngine.cpp
        PROPERTIES COMPILE_OPTIONS "-std=c++20")
endif()
if(SPEEDYNOTE_ENABLE_MLKIT_INK_OCR)
    list(APPEND OCR_SOURCES source/ocr/engines/MlKitOcrEngine.cpp)
    if(ANDROID)
        list(APPEND OCR_SOURCES source/ocr/engines/MlKitOcrEngine_android.cpp)
    elseif(IOS)
        list(APPEND OCR_SOURCES source/ocr/engines/MlKitOcrEngine_ios.mm)
        set_source_files_properties(source/ocr/engines/MlKitOcrEngine_ios.mm
            PROPERTIES LANGUAGE OBJCXX)
    endif()
endif()
if(SPEEDYNOTE_ENABLE_VISION_OCR)
    # Apple Vision backend (macOS); shares the 4A RasterOcrEngine base above.
    list(APPEND OCR_SOURCES source/ocr/engines/VisionOcrEngine_macos.mm)
    set_source_files_properties(source/ocr/engines/VisionOcrEngine_macos.mm
        PROPERTIES LANGUAGE OBJCXX)
endif()
if(SPEEDYNOTE_ENABLE_PADDLE_OCR)
    # PaddleOCR backend (Linux); shares the 4A RasterOcrEngine base above.
    # Plain C++ (no OBJCXX); links the vendored ONNX Runtime in the UNIX block.
    list(APPEND OCR_SOURCES source/ocr/engines/PaddleOcrEngine_linux.cpp)
endif()

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
    source/ui/SplitViewManager.cpp
    source/ui/PageThumbnailModel.cpp
    source/ui/PageThumbnailDelegate.cpp
    source/ui/ThumbnailRenderer.cpp
    # Sidebars
    source/ui/sidebars/LayerPanel.cpp
    source/ui/sidebars/LeftSidebarContainer.cpp
    source/ui/sidebars/OutlinePanel.cpp
    source/ui/sidebars/OutlinePanelTreeWidget.cpp
    source/ui/sidebars/OutlineItemDelegate.cpp
    source/ui/sidebars/NotesTreePanel.cpp
    source/ui/sidebars/NotesTreeWidget.cpp
    source/ui/sidebars/NotesTreeDelegate.cpp
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
    source/ui/widgets/ExportProgressWidget.cpp
    source/ui/widgets/LayerItemWidget.cpp
    source/ui/widgets/LayerPanelPillButton.cpp
    source/ui/widgets/PdfSearchBar.cpp
    source/ui/widgets/ExpandableToolButton.cpp
    source/ui/widgets/ViewportScrollBar.cpp
    # Subtoolbars
    source/ui/subtoolbars/SubToolbar.cpp
    
    source/ui/subtoolbars/PenSubToolbar.cpp
    source/ui/subtoolbars/MarkerSubToolbar.cpp
    source/ui/subtoolbars/EraserSubToolbar.cpp
    source/ui/subtoolbars/HighlighterSubToolbar.cpp
    source/ui/subtoolbars/OcrSubToolbar.cpp
    # Panels
    source/ui/panels/InlineTextBoxEditor.cpp
    source/ui/panels/TextBoxFormatBar.cpp
    source/ui/panels/LinkObjectBar.cpp
    # Action bars
    source/ui/actionbars/ActionBar.cpp
    source/ui/actionbars/ActionBarContainer.cpp
    source/ui/actionbars/LassoActionBar.cpp
    source/ui/actionbars/ObjectSelectActionBar.cpp
    source/ui/actionbars/PagePanelActionBar.cpp
    # Banners
    source/ui/banners/MissingPdfBanner.cpp
    # Launcher
    source/ui/launcher/Launcher.cpp
    source/ui/launcher/LauncherNavButton.cpp
    source/ui/launcher/TimelineModel.cpp
    source/ui/launcher/TimelineDelegate.cpp
    source/ui/launcher/TimelineListView.cpp
    source/ui/launcher/KineticScrollHelper.cpp
    source/ui/launcher/KineticListView.cpp
    source/ui/launcher/NotebookCardDelegate.cpp
    source/ui/launcher/FolderHeaderDelegate.cpp
    source/ui/launcher/SearchModel.cpp
    source/ui/launcher/StarredModel.cpp
    source/ui/launcher/SearchListView.cpp
    source/ui/launcher/StarredListView.cpp
    source/ui/launcher/StarredView.cpp
    source/ui/launcher/SearchView.cpp
    source/ui/launcher/FloatingActionButton.cpp
    source/ui/launcher/FolderPickerDialog.cpp
)

# PDF abstraction layer
# As of v1.2.1, SpeedyNote uses MuPDF exclusively on all platforms:
# - Eliminates symbol conflicts between MuPDF and Poppler/OpenJPEG
# - Consistent rendering across all platforms
# - Faster rendering performance
set(PDF_SOURCES
    source/pdf/PdfProviderFactory.cpp
    source/pdf/MuPdfProvider.cpp
    source/pdf/PdfSearchEngine.cpp
    source/pdf/MuPdfExporter.cpp
    source/pdf/PdfMaterializer.cpp
)
message(STATUS "   PDF provider: MuPDF (all platforms)")

# Sharing module (cross-platform .snbx package export/import)
# Uses miniz library for ZIP creation/extraction (MIT license, thirdparty/miniz/)
set(SHARING_SOURCES
    source/sharing/NotebookExporter.cpp
    source/sharing/NotebookImporter.cpp
    thirdparty/miniz/miniz.c
)

# Batch operations module (CLI and bulk export/import)
# See docs/private/BATCH_OPERATIONS.md for design documentation
set(BATCH_SOURCES
    source/batch/BatchOperations.cpp
    source/batch/BundleDiscovery.cpp
    source/batch/ExportQueueManager.cpp
)

# CLI module (Desktop only - headless batch operations)
# Mobile platforms (Android/iOS) use launcher UI for batch operations instead
set(CLI_SOURCES "")
if(NOT ANDROID AND NOT IOS)
    set(CLI_SOURCES
        source/cli/CliParser.cpp
        source/cli/CliProgress.cpp
        source/cli/CliHandler.cpp
        source/cli/CliSignal.cpp
        source/ui/dialogs/BatchImportDialog.cpp  # Desktop-only batch import dialog
    )
endif()

# Android-specific utilities and UI
set(ANDROID_PLATFORM_SOURCES "")
if(ANDROID)
    set(ANDROID_PLATFORM_SOURCES
        source/android/PdfPickerAndroid.cpp
        source/android/AndroidShareHelper.cpp
        source/ui/dialogs/SaveDocumentDialog.cpp  # BUG-A002: Touch-friendly save dialog
    )
endif()

# iOS-specific utilities and UI (Objective-C++ .mm files)
set(IOS_PLATFORM_SOURCES "")
if(IOS)
    set(IOS_PLATFORM_SOURCES
        source/ios/PdfPickerIOS.mm
        source/ios/SnbxPickerIOS.mm
        source/ios/IOSShareHelper.mm
        source/ios/IOSPlatformHelper.mm
        source/ios/IOSTouchTracker.mm
        source/ui/dialogs/SaveDocumentDialog.cpp  # Reuse Android's touch-friendly save dialog
    )
    # Enable Objective-C++ compilation for .mm files
    set_source_files_properties(
        source/ios/PdfPickerIOS.mm
        source/ios/SnbxPickerIOS.mm
        source/ios/IOSShareHelper.mm
        source/ios/IOSPlatformHelper.mm
        source/ios/IOSTouchTracker.mm
        PROPERTIES LANGUAGE OBJCXX
    )
endif()

# macOS desktop-specific sources (system menu bar, About dialog).
# Excluded from iOS, Android, Windows, Linux. APPLE is true for both macOS
# and iOS, so the explicit "AND NOT IOS" guard is required.
set(MACOS_PLATFORM_SOURCES "")
if(APPLE AND NOT IOS)
    set(MACOS_PLATFORM_SOURCES
        source/macos/MacMenuBar.cpp
        source/macos/AboutDialog.cpp
    )
endif()

# Cross-platform utilities (Step 3.11: System notifications)
set(PLATFORM_SOURCES
    source/platform/SystemNotification.cpp
)

# Desktop-only sources (tests require Qt::Test, debug builds only)
set(DESKTOP_ONLY_SOURCES "")
if(NOT ANDROID AND NOT IOS AND ENABLE_DEBUG_OUTPUT)
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
    source/ui/dialogs/KeyCaptureDialog.cpp
    source/ui/dialogs/PageRangeSelectDialog.cpp
    source/ui/dialogs/PdfSourcesDialog.cpp
    source/ui/dialogs/CopyPagesToDocDialog.cpp
    source/ui/dialogs/BatchExportDialog.cpp
    source/ui/dialogs/ExportResultsDialog.cpp
    source/ui/dialogs/DocumentSettingsDialog.cpp
    source/text/MarkdownNoteEntry.cpp
    source/ui/MarkdownNotesSidebar.cpp
        ${CORE_SOURCES}
    ${OBJECT_SOURCES}
    ${OCR_SOURCES}
        ${UI_SOURCES}
        ${PDF_SOURCES}
    ${SHARING_SOURCES}
    ${BATCH_SOURCES}
    ${CLI_SOURCES}
    ${PLATFORM_SOURCES}
    ${ANDROID_PLATFORM_SOURCES}
    ${IOS_PLATFORM_SOURCES}
    ${MACOS_PLATFORM_SOURCES}
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
    if(USE_QT5)
        add_executable(speedynote ${APP_SOURCES})
    else()
        qt_add_executable(speedynote ${APP_SOURCES})
        qt_policy(SET QTP0002 NEW)
    endif()
    
    # Generate AndroidManifest.xml with version from CMake, then point
    # the Android deploy tool at the configured copy.
    set(ANDROID_CONFIGURED_DIR "${CMAKE_CURRENT_BINARY_DIR}/android-configured")
    file(COPY "${CMAKE_SOURCE_DIR}/android/app-resources/"
         DESTINATION "${ANDROID_CONFIGURED_DIR}/")
    configure_file(
        "${CMAKE_SOURCE_DIR}/android/app-resources/AndroidManifest.xml.in"
        "${ANDROID_CONFIGURED_DIR}/AndroidManifest.xml"
        @ONLY)

    set_target_properties(speedynote PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${ANDROID_CONFIGURED_DIR}"
        QT_ANDROID_TARGET_SDK_VERSION 35
        QT_ANDROID_MIN_SDK_VERSION 26
    )
    
    target_include_directories(speedynote PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/source
        ${MUPDF_INCLUDE_DIR}
    )

elseif(IOS)
    # Enable Objective-C++ language for .mm files
    enable_language(OBJCXX)
    
    if(USE_QT5)
        add_executable(speedynote ${APP_SOURCES})
    else()
        qt_add_executable(speedynote ${APP_SOURCES})
    endif()
    
    set_target_properties(speedynote PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.speedynote.speedynote"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.speedynote.speedynote"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    )

    # App icon asset catalog (generated by ios/generate-icons.sh)
    target_sources(speedynote PRIVATE "${CMAKE_SOURCE_DIR}/ios/Assets.xcassets")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/ios/Assets.xcassets" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )

    # Code signing: provisioned when DEVELOPMENT_TEAM is set, ad-hoc otherwise.
    # Pass -DDEVELOPMENT_TEAM=XXXXXXXXXX at configure time for real device deployment.
    # Without it, the build uses ad-hoc signing (works for simulator, jailbreak, TrollStore).
    if(DEVELOPMENT_TEAM)
        set_target_properties(speedynote PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DEVELOPMENT_TEAM}"
        )
        message(STATUS "iOS signing: Team ${DEVELOPMENT_TEAM} (automatic)")
    else()
        set_target_properties(speedynote PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )
        message(STATUS "iOS signing: ad-hoc (no DEVELOPMENT_TEAM — simulator / jailbreak / TrollStore)")
    endif()
    
    target_include_directories(speedynote PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/source
        ${MUPDF_INCLUDE_DIR}
    )

else()
    add_executable(speedynote ${APP_SOURCES})
endif()

# ============================================================================
# Version Defines (single source of truth: project(VERSION ...) above)
# ============================================================================
target_compile_definitions(speedynote PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    APP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
if(SPEEDYNOTE_ENABLE_MLKIT_INK_OCR)
    target_compile_definitions(speedynote PRIVATE SPEEDYNOTE_HAS_MLKIT_INK)
endif()

# ============================================================================
# Translation Compilation (.ts -> .qm)
# ============================================================================
if(WIN32)
    find_program(LRELEASE_EXECUTABLE lrelease PATHS "${QT_PATH}/bin")
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
            Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Xml Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Svg
    )
    if(ENABLE_DEBUG_OUTPUT)
        list(APPEND PLATFORM_LIBS Qt${QT_VERSION_MAJOR}::Test)
    endif()
    
    # MuPDF for PDF rendering and export
    if(MUPDF_FOUND)
        list(APPEND PLATFORM_LIBS ${MUPDF_LIBRARIES})
        message(STATUS "MuPDF: Will link dynamically with: ${MUPDF_LIBRARIES}")
    elseif(USE_QT5)
        message(WARNING "MuPDF not found - PDF features will be unavailable (Qt5 build)")
    else()
        message(FATAL_ERROR "MuPDF is required for PDF support")
    endif()
    if(SPEEDYNOTE_ENABLE_WINDOWS_INK_OCR)
        target_compile_definitions(speedynote PRIVATE SPEEDYNOTE_HAS_WINDOWS_INK)
        list(APPEND PLATFORM_LIBS windowsapp)
    endif()
    target_link_libraries(speedynote ${PLATFORM_LIBS})

elseif(IOS)
    target_link_libraries(speedynote PRIVATE
        Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Xml Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Svg
        ${MUPDF_LIBRARIES}
        z
        "-framework UIKit"
        "-framework Foundation"
        "-framework CoreGraphics"
        "-framework UniformTypeIdentifiers"
    )
    if(SPEEDYNOTE_ENABLE_MLKIT_INK_OCR)
        set(MLKIT_DIR "${CMAKE_SOURCE_DIR}/ios/mlkit-build")
        if(EXISTS "${MLKIT_DIR}/Frameworks")
            file(GLOB MLKIT_FRAMEWORKS "${MLKIT_DIR}/Frameworks/*.xcframework")
            foreach(fw ${MLKIT_FRAMEWORKS})
                target_link_libraries(speedynote PRIVATE "${fw}")
            endforeach()
            target_include_directories(speedynote PRIVATE "${MLKIT_DIR}/Frameworks")
            target_link_libraries(speedynote PRIVATE "-framework Network")
            target_link_options(speedynote PRIVATE "-ObjC")

            file(GLOB MLKIT_RESOURCE_BUNDLES "${MLKIT_DIR}/Resources/*.bundle")
            foreach(rb ${MLKIT_RESOURCE_BUNDLES})
                get_filename_component(rb_name "${rb}" NAME)
                add_custom_command(TARGET speedynote POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${rb}"
                        "$<TARGET_BUNDLE_DIR:speedynote>/${rb_name}"
                    COMMENT "Copying ${rb_name} into app bundle"
                )
            endforeach()
        else()
            message(WARNING "ML Kit frameworks not found at ${MLKIT_DIR}/Frameworks. Run ios/fetch-mlkit.sh first.")
            set(SPEEDYNOTE_ENABLE_MLKIT_INK_OCR OFF CACHE BOOL "" FORCE)
        endif()
    endif()

elseif(APPLE)
    set(PLATFORM_LIBS
            Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Xml Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Svg
    )
    if(ENABLE_DEBUG_OUTPUT)
        list(APPEND PLATFORM_LIBS Qt${QT_VERSION_MAJOR}::Test)
    endif()
    # MuPDF for PDF rendering and export (macOS uses MuPDF exclusively)
    if(MUPDF_FOUND)
        list(APPEND PLATFORM_LIBS ${MUPDF_LIBRARIES})
        message(STATUS "Linking MuPDF for PDF rendering and export")
    else()
        message(FATAL_ERROR "MuPDF is required for PDF support on macOS. Install with: brew install mupdf")
    endif()
    if(SPEEDYNOTE_ENABLE_VISION_OCR)
        target_compile_definitions(speedynote PRIVATE SPEEDYNOTE_HAS_VISION_OCR)
        list(APPEND PLATFORM_LIBS
            "-framework Vision" "-framework CoreGraphics" "-framework Foundation")
        message(STATUS "Apple Vision OCR: linking Vision/CoreGraphics/Foundation")
    endif()
    target_link_libraries(speedynote ${PLATFORM_LIBS})

elseif(ANDROID)
    target_link_libraries(speedynote PRIVATE
        Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Xml Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Svg
        ${MUPDF_LIBRARIES}
        log z
    )

elseif(UNIX)
    set(PLATFORM_LIBS
            Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Xml Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Svg
            Qt${QT_VERSION_MAJOR}::DBus  # Step 3.11: System notifications via org.freedesktop.Notifications
    )
    # MuPDF for PDF rendering and export (all Linux distributions)
    if(MUPDF_FOUND)
        list(APPEND PLATFORM_LIBS ${MUPDF_LIBRARIES})
        message(STATUS "Linking MuPDF for PDF rendering and export")
    else()
        message(FATAL_ERROR "MuPDF is required for PDF support")
    endif()
    if(ENABLE_DEBUG_OUTPUT)
        list(APPEND PLATFORM_LIBS Qt${QT_VERSION_MAJOR}::Test)
    endif()
    # PaddleOCR raster OCR: vendored ONNX Runtime (CPU EP). ONNXRUNTIME_ROOT was
    # set during the configure-time gating above.
    if(SPEEDYNOTE_ENABLE_PADDLE_OCR)
        target_compile_definitions(speedynote PRIVATE SPEEDYNOTE_HAS_PADDLE_OCR)
        target_include_directories(speedynote PRIVATE "${ONNXRUNTIME_ROOT}/include")
        file(GLOB _ort_so "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
        if(NOT _ort_so)
            file(GLOB _ort_so "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so.*")
        endif()
        list(GET _ort_so 0 ONNXRUNTIME_LIB)
        list(APPEND PLATFORM_LIBS "${ONNXRUNTIME_LIB}")
        # Resolve the bundled .so next to the install (lib/) at runtime.
        # BUILD_RPATH is set in addition to the auto-generated absolute path to
        # the vendored lib, so local `./build/speedynote` dev runs still work
        # AND the build-tree binary that packaging copies verbatim (deb/apk/
        # arch/rpm never run `cmake --install`) resolves the bundled lib via
        # $ORIGIN/../lib once installed under <prefix>/bin with the lib in
        # <prefix>/lib.
        set_target_properties(speedynote PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
            BUILD_RPATH   "$ORIGIN/../lib"
            BUILD_WITH_INSTALL_RPATH FALSE)
        message(STATUS "PaddleOCR raster OCR: linking ${ONNXRUNTIME_LIB}")
    endif()
    target_link_libraries(speedynote ${PLATFORM_LIBS})
endif()

# ============================================================================
# Install Targets
# ============================================================================
# Qt's own qtbase_<lang>.qm catalogs are discovered + (on mobile) embedded
# higher up in this file, in the "Qt translation catalogs" section above
# the Resources block. _install_qtbase_translations() is defined there.

if(IOS)
    # iOS: No install targets — packaging is handled by ios/build-deb.sh and
    # ios/build-speedynote.sh. qtbase_*.qm reaches the device via the
    # embedded qt_translations.qrc generated above (see "Qt translation
    # catalogs" section).

elseif(APPLE)
    install(TARGETS speedynote DESTINATION bin)
    if(QM_FILES)
        install(FILES ${QM_FILES} DESTINATION share/speedynote/translations)
    endif()
    file(GLOB RESOURCE_QM_FILES "resources/translations/*.qm")
    if(RESOURCE_QM_FILES)
        install(FILES ${RESOURCE_QM_FILES} DESTINATION share/speedynote/translations)
    endif()
    _install_qtbase_translations(share/speedynote/translations)
    install(FILES resources/icons/mainicon.svg DESTINATION share/speedynote/icons)

elseif(UNIX AND NOT ANDROID)
    install(TARGETS speedynote DESTINATION bin)
    if(QM_FILES)
        install(FILES ${QM_FILES} DESTINATION share/speedynote/translations)
    endif()
    file(GLOB RESOURCE_QM_FILES "resources/translations/*.qm")
    if(RESOURCE_QM_FILES)
        install(FILES ${RESOURCE_QM_FILES} DESTINATION share/speedynote/translations)
    endif()
    _install_qtbase_translations(share/speedynote/translations)
    # Desktop integration files
    install(FILES data/org.speedynote.SpeedyNote.desktop
            DESTINATION share/applications)
    install(FILES data/org.speedynote.SpeedyNote.metainfo.xml
            DESTINATION share/metainfo)
    install(FILES resources/icons/mainicon.svg
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME org.speedynote.SpeedyNote.svg)
    # PaddleOCR raster OCR: bundle the recognition models + the vendored ONNX
    # Runtime shared library alongside the install (resolved via INSTALL_RPATH).
    if(SPEEDYNOTE_ENABLE_PADDLE_OCR)
        file(GLOB OCR_MODEL_FILES "${CMAKE_SOURCE_DIR}/linux/ocr-models/*.onnx")
        if(OCR_MODEL_FILES)
            install(FILES ${OCR_MODEL_FILES} DESTINATION share/speedynote/ocr-models)
        endif()
        file(GLOB ONNXRUNTIME_SO_FILES "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so*")
        if(ONNXRUNTIME_SO_FILES)
            install(FILES ${ONNXRUNTIME_SO_FILES} DESTINATION lib)
        endif()
    endif()
endif()
