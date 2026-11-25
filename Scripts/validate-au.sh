#!/usr/bin/env bash
# Installs the built AU into the user Components folder and runs auval against it.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-Release}"
ARCH_REQUEST="${2:-arm64}"
SUBTYPE="${AU_SUBTYPE:-Kntk}"
MANUFACTURER="${AU_MANUFACTURER:-Kntc}"
BUILD_DIR="${ROOT}/build/${CONFIG}-${ARCH_REQUEST}"

COMPONENT="$(find "${BUILD_DIR}" -maxdepth 4 -name "KINETIC.component" -print -quit)"

if [[ -z "${COMPONENT}" ]]; then
    echo "No KINETIC.component found in ${BUILD_DIR}. Build first." >&2
    exit 1
fi

DEST="${HOME}/Library/Audio/Plug-Ins/Components"
mkdir -p "${DEST}"
rsync -a --delete "${COMPONENT}" "${DEST}/"

# Make the host-visible component registry pick up the new bundle.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "Installed: ${DEST}/$(basename "${COMPONENT}")"
echo "component type: $(/usr/libexec/PlistBuddy -c 'Print :AudioComponents:0:type' "${COMPONENT}/Contents/Info.plist" 2>/dev/null || echo '?')"
echo
echo "--- auval -v aumi ${SUBTYPE} ${MANUFACTURER} ---"
set +e
auval -v aumi "${SUBTYPE}" "${MANUFACTURER}"
STATUS=$?
set -e
exit "${STATUS}"
