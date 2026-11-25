#!/usr/bin/env bash
# Signs a freshly built plug-in bundle and installs it into a per-user plug-in folder.
#
#   Scripts/install-plugin.sh <path/to/Bundle> <destination folder>
#
# For the VST3 this is required, not cosmetic: JUCE signs the bundle and then writes
# Contents/Resources/moduleinfo.json, which leaves the code signature seal invalid, and a
# bundle whose seal does not verify is one a host scanner can refuse. Ad-hoc signing here,
# after that step, fixes it. The AU has no such late write, but signing both formats the same
# way keeps one install path and one guarantee: the copy a host loads verifies on disk.
#
# The destination is signed too rather than trusting the copy, because the installed bundle is
# the one the host actually reads.
set -euo pipefail

BUNDLE="${1:?usage: install-plugin.sh <path/to/Bundle> <destination folder>}"
DEST_DIR="${2:?usage: install-plugin.sh <path/to/Bundle> <destination folder>}"
DEST="${DEST_DIR}/$(basename "${BUNDLE}")"

if [ ! -d "${BUNDLE}" ]; then
    echo "install-plugin: no bundle at ${BUNDLE}" >&2
    exit 1
fi

# Ad-hoc (identity "-") only: this is a locally built plug-in, not a distributed, notarised one.
codesign --force --deep --sign - "${BUNDLE}" >/dev/null 2>&1
codesign --verify --strict --deep "${BUNDLE}"

mkdir -p "${DEST_DIR}"
rsync -a --delete "${BUNDLE}/" "${DEST}/"

codesign --verify --strict --deep "${DEST}"

# Hosts notice a changed bundle by its modification time; the files inside keep the build's own
# timestamps, so touch the bundle itself.
touch "${DEST}"

echo "install-plugin: $(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "${DEST}/Contents/Info.plist" 2>/dev/null || echo '?') installed at ${DEST}"
