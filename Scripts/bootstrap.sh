#!/usr/bin/env bash
# Fetches the pinned JUCE release into external/JUCE.
# Run once after cloning KINETIC; CMake falls back to FetchContent if this is skipped.
set -euo pipefail

JUCE_TAG="${JUCE_TAG:-8.0.15}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/external/JUCE"

if [[ -f "${DEST}/CMakeLists.txt" ]]; then
    echo "JUCE already present at ${DEST}"
    exit 0
fi

mkdir -p "${ROOT}/external"
git clone --depth 1 --branch "${JUCE_TAG}" --single-branch \
    https://github.com/juce-framework/JUCE.git "${DEST}"
echo "JUCE ${JUCE_TAG} vendored at ${DEST}"
