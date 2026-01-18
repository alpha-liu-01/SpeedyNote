# =============================================================================
# SpeedyNote Android Build Environment
# =============================================================================
# This Dockerfile creates a reproducible environment for building SpeedyNote
# for Android arm64-v8a.
#
# Contents:
#   - Ubuntu 22.04 LTS base
#   - Android SDK (API 35) + NDK r27
#   - Qt 6.9.3 for Android arm64-v8a
#   - Qt 6.9.3 host tools (Linux x86_64)
#   - CMake, Ninja, OpenJDK 17
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
    # Java (required for Android SDK)
    openjdk-17-jdk \
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
ENV JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64

# Qt version to install
# Qt 6.9.3 is the recommended version:
#   - 6.7.x: Has keyboard crash bug (QtEditText.m_optionsChanged NullPointerException)
#   - 6.9.3: Works correctly ✓
#   - 6.10.x: Has OpenGL threading deadlock on Android
ENV QT_VERSION=6.9.3
ENV QT_ROOT=/opt/qt
ENV QT_ANDROID=${QT_ROOT}/${QT_VERSION}/android_arm64_v8a
# Note: aqtinstall uses "linux_gcc_64" as arch name but installs to "gcc_64"
ENV QT_HOST=${QT_ROOT}/${QT_VERSION}/gcc_64

# NDK version
# Qt 6.10.x requires NDK r27+ for API 35 support (androidx.core:core:1.16.0 dependency)
ENV NDK_VERSION=27.2.12479018
ENV ANDROID_NDK_ROOT=${ANDROID_SDK_ROOT}/ndk/${NDK_VERSION}

# API levels
# Qt 6.10+ requires compileSdk 35+ due to androidx dependencies
ENV ANDROID_MIN_SDK=26
ENV ANDROID_TARGET_SDK=35

# Add tools to PATH
ENV PATH="${QT_HOST}/bin:${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${PATH}"

# =============================================================================
# Install Android SDK & NDK
# =============================================================================
RUN mkdir -p ${ANDROID_SDK_ROOT}/cmdline-tools && \
    # Download Android command-line tools
    wget -q -O /tmp/cmdline-tools.zip \
        "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip" && \
    unzip -q /tmp/cmdline-tools.zip -d ${ANDROID_SDK_ROOT}/cmdline-tools && \
    mv ${ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools ${ANDROID_SDK_ROOT}/cmdline-tools/latest && \
    rm /tmp/cmdline-tools.zip

# Accept licenses
RUN yes | sdkmanager --licenses > /dev/null 2>&1 || true

# Install SDK components
RUN sdkmanager --install \
    "platforms;android-${ANDROID_TARGET_SDK}" \
    "build-tools;35.0.0" \
    "ndk;${NDK_VERSION}" \
    "platform-tools"

# =============================================================================
# Install Qt via aqtinstall
# =============================================================================
# Create virtual environment for aqtinstall to avoid pip externally-managed error
RUN python3 -m venv /opt/aqt-venv && \
    /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall

# Install Qt for Android (arm64-v8a)
RUN /opt/aqt-venv/bin/aqt install-qt linux android ${QT_VERSION} android_arm64_v8a \
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
    echo "Qt Android: $(ls ${QT_ANDROID}/bin/ | head -5)" && \
    echo "Qt Host: $(${QT_HOST}/bin/qmake --version)" && \
    echo "CMake: $(cmake --version | head -1)" && \
    echo "Java: $(java --version | head -1)"

# =============================================================================
# Workspace Setup
# =============================================================================
WORKDIR /workspace

# Default command
CMD ["/bin/bash"]

