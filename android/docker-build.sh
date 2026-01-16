#!/bin/bash
# =============================================================================
# Build the SpeedyNote Android Docker image
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="speedynote-android"
IMAGE_TAG="latest"

echo "=== Building SpeedyNote Android Docker Image ==="
echo "This will take 10-20 minutes on first build..."
echo ""

cd "${SCRIPT_DIR}"

# Build the Docker image
docker build \
    --tag "${IMAGE_NAME}:${IMAGE_TAG}" \
    --file Dockerfile \
    .

echo ""
echo "=== Build Complete ==="
echo "Image: ${IMAGE_NAME}:${IMAGE_TAG}"
echo ""
echo "To enter the container:"
echo "  ./docker-shell.sh"
echo ""
echo "To build SpeedyNote for Android:"
echo "  ./docker-shell.sh"
echo "  cd /workspace"
echo "  ./android/build-android.sh"

