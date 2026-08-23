# =============================================================================
# SpeedyNote Android Build Environment
# =============================================================================
# This Dockerfile creates a reproducible environment for building SpeedyNote
# for Android arm64-v8a and armeabi-v7a (32-bit).
#
# Contents:
#   - Ubuntu 22.04 LTS base
#   - Android SDK (API 36) + NDK r27
#   - Qt 6.11.2 for Android arm64-v8a + armv7 (32-bit)
#   - Qt 6.11.2 host tools (Linux x86_64)
#   - CMake, Ninja, OpenJDK 21
#
# Build:
#   docker build -t speedynote-android:latest .
#
# Run:
#   docker run -it --rm -v $(pwd)/..:/workspace speedynote-android:latest
#
# =============================================================================

FROM ubuntu:22.04

LABEL maintainer="SpeedyNote Developers"
LABEL description="Android build environment for SpeedyNote"

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# =============================================================================
# System Dependencies
# =============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build tools
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    # Version control
    git \
    # Download tools
    wget \
    curl \
    unzip \
    # Java (required for Android SDK; Qt 6.11 / AGP 9 need JDK 21)
    openjdk-21-jdk \
    # Python (for aqtinstall)
    python3 \
    python3-pip \
    python3-venv \
    # SSL certificates (for downloads)
    ca-certificates \
    # Useful utilities
    file \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Environment Variables
# =============================================================================
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV ANDROID_HOME=${ANDROID_SDK_ROOT}
ENV JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64

# Qt version to install
# History of what works on Android:
#   - 6.7.x:  Keyboard crash bug (QtEditText.m_optionsChanged NullPointerException)
#   - 6.9.3:  Works correctly ✓ (previous baseline)
#   - 6.10.x: OpenGL threading deadlock on Android (QTBUG-141579 family)
#   - 6.11.x: Current. The 6.10 deadlock reports extend to 6.11, so if
#             "Failed to acquire deadlock protector" shows up in logcat,
#             drop back to 6.9.3 and revert the JDK/SDK bumps with it.
ENV QT_VERSION=6.11.2
ENV QT_ROOT=/opt/qt
ENV QT_ANDROID=${QT_ROOT}/${QT_VERSION}/android_arm64_v8a
ENV QT_ANDROID_ARMV7=${QT_ROOT}/${QT_VERSION}/android_armv7
# Note: aqtinstall uses "linux_gcc_64" as arch name but installs to "gcc_64"
ENV QT_HOST=${QT_ROOT}/${QT_VERSION}/gcc_64

# NDK version
# Unchanged from the 6.9.3 setup, so the prebuilt MuPDF static libs in
# android/mupdf-build/ stay valid and do not need rebuilding.
ENV NDK_VERSION=27.2.12479018
ENV ANDROID_NDK_ROOT=${ANDROID_SDK_ROOT}/ndk/${NDK_VERSION}

# API levels
# Qt 6.11 builds against compileSdk 36. Min SDK stays at 26: Qt declares an
# API 28 floor but 26 has worked since 6.9 and older tablets need it.
ENV ANDROID_MIN_SDK=26
ENV ANDROID_TARGET_SDK=36

# Add tools to PATH
ENV PATH="${QT_HOST}/bin:${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${PATH}"

# =============================================================================
# Install Android SDK & NDK
# =============================================================================
RUN mkdir -p ${ANDROID_SDK_ROOT}/cmdline-tools && \
    # Download Android command-line tools
    wget -q -O /tmp/cmdline-tools.zip \
        "https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip" && \
    unzip -q /tmp/cmdline-tools.zip -d ${ANDROID_SDK_ROOT}/cmdline-tools && \
    mv ${ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools ${ANDROID_SDK_ROOT}/cmdline-tools/latest && \
    rm /tmp/cmdline-tools.zip

# Accept licenses
RUN yes | sdkmanager --licenses > /dev/null 2>&1 || true

# Install SDK components
RUN sdkmanager --install \
    "platforms;android-${ANDROID_TARGET_SDK}" \
    "build-tools;36.0.0" \
    "ndk;${NDK_VERSION}" \
    "platform-tools"

# =============================================================================
# Install Qt via aqtinstall
# =============================================================================
# Create virtual environment for aqtinstall to avoid pip externally-managed error.
# Deliberately unpinned: the Qt online repository layout changes between minor
# releases and older aqtinstall versions fail to resolve 6.11.
RUN python3 -m venv /opt/aqt-venv && \
    /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall

# Install Qt for Android (arm64-v8a)
# Since Qt 6.7 the Android artifacts are host-agnostic, so the host is "all_os".
RUN /opt/aqt-venv/bin/aqt install-qt all_os android ${QT_VERSION} android_arm64_v8a \
    -O ${QT_ROOT} \
    --modules qtimageformats

# Install Qt for Android (armeabi-v7a / 32-bit ARM)
RUN /opt/aqt-venv/bin/aqt install-qt all_os android ${QT_VERSION} android_armv7 \
    -O ${QT_ROOT} \
    --modules qtimageformats

# Install Qt host tools (needed for moc, uic, rcc, etc.)
# Note: For Qt 6.x, architecture is "linux_gcc_64" not "gcc_64"
RUN /opt/aqt-venv/bin/aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
    -O ${QT_ROOT} \
    --modules qtimageformats

# =============================================================================
# Verify Installation
# =============================================================================
RUN echo "=== Verifying installation ===" && \
    echo "Android SDK: $(ls ${ANDROID_SDK_ROOT}/platforms/)" && \
    echo "Android NDK: $(ls ${ANDROID_SDK_ROOT}/ndk/)" && \
    echo "Qt Android arm64: $(ls ${QT_ANDROID}/bin/ | head -5)" && \
    echo "Qt Android armv7: $(ls ${QT_ANDROID_ARMV7}/bin/ | head -5)" && \
    echo "Qt Host: $(${QT_HOST}/bin/qmake --version)" && \
    echo "CMake: $(cmake --version | head -1)" && \
    echo "Java: $(java --version | head -1)" && \
    echo "--- assertions ---" && \
    test -d "${ANDROID_SDK_ROOT}/platforms/android-${ANDROID_TARGET_SDK}" && \
    test -d "${ANDROID_SDK_ROOT}/build-tools/36.0.0" && \
    test -d "${ANDROID_NDK_ROOT}" && \
    test -x "${QT_HOST}/bin/androiddeployqt" && \
    ${QT_HOST}/bin/qmake --version | grep -q "Qt version ${QT_VERSION}" && \
    java --version | grep -q " 21\." && \
    echo "All assertions passed."

# =============================================================================
# Workspace Setup
# =============================================================================
WORKDIR /workspace

# Default command
CMD ["/bin/bash"]

