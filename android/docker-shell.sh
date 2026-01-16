#!/bin/bash
# =============================================================================
# Enter the SpeedyNote Android Docker container
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
IMAGE_NAME="speedynote-android"
IMAGE_TAG="latest"

# Check if image exists
if ! docker image inspect "${IMAGE_NAME}:${IMAGE_TAG}" > /dev/null 2>&1; then
    echo "Docker image not found. Building it first..."
    "${SCRIPT_DIR}/docker-build.sh"
fi

echo "=== Entering SpeedyNote Android Container ==="
echo "Project mounted at: /workspace"
echo ""

# Run container with:
# - Interactive terminal (-it)
# - Remove container on exit (--rm)
# - Mount project directory (-v)
# - Mount .gradle cache for faster builds (-v)
docker run -it --rm \
    -v "${PROJECT_ROOT}:/workspace" \
    -v "${HOME}/.gradle:/root/.gradle" \
    -w /workspace \
    "${IMAGE_NAME}:${IMAGE_TAG}" \
    /bin/bash

