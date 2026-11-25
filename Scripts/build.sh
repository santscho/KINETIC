#!/usr/bin/env bash
# Configures and builds KINETIC.
#
#   Scripts/build.sh [Debug|Release] [arm64|universal]
#
# arm64 (the default) is a native Apple Silicon build. "universal" adds the
# Intel slice through the KINETIC_UNIVERSAL CMake option, so there is a single
# definition of what Universal means.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-Release}"
TARGET_ARCH="${2:-arm64}"

case "${TARGET_ARCH}" in
    arm64)
        ARCH_ARGS=(-DCMAKE_OSX_ARCHITECTURES=arm64)
        ;;
    universal)
        ARCH_ARGS=(-DKINETIC_UNIVERSAL=ON)
        ;;
    *)
        echo "Unknown architecture '${TARGET_ARCH}' (expected arm64 or universal)" >&2
        exit 2
        ;;
esac

BUILD_DIR="${ROOT}/build/${CONFIG}-${TARGET_ARCH}"

# Every build signs and installs both formats for the local host scans; CI has no host to
# serve, so it sets KINETIC_INSTALL_PLUGINS=OFF in the environment.
cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CONFIG}" \
    -DKINETIC_INSTALL_PLUGINS="${KINETIC_INSTALL_PLUGINS:-ON}" \
    "${ARCH_ARGS[@]}"

cmake --build "${BUILD_DIR}" --config "${CONFIG}" -j "$(sysctl -n hw.ncpu)"

echo
echo "Built artefacts (${TARGET_ARCH}):"
find "${BUILD_DIR}" -maxdepth 4 \( -name "*.component" -o -name "*.vst3" \) | sort
