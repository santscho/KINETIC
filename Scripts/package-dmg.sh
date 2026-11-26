#!/usr/bin/env bash
# Builds the plug-ins and writes a disk image that can be handed to someone else.
#
#   Scripts/package-dmg.sh [Debug|Release] [arm64|universal] [--no-build]
#
# The image is not signed with a Developer ID and is not notarised - there is no Apple
# Developer account behind this project - so macOS may refuse to open it, and a host may refuse
# or silently skip the plug-ins once they are copied out of it. That is why the image carries a
# READ ME FIRST.txt: it says exactly what macOS says and what to type to get past it.
#
# The bundles are ad-hoc signed ("-", not an identity) before they are packed, so their seal is
# intact and verifies on disk; only the identity macOS wants for an unidentified download is
# missing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-Release}"
TARGET_ARCH="${2:-arm64}"
SKIP_BUILD="${3:-}"

case "${TARGET_ARCH}" in
    arm64|universal) ;;
    *)
        echo "Unknown architecture '${TARGET_ARCH}' (expected arm64 or universal)" >&2
        exit 2
        ;;
esac

if [ "${SKIP_BUILD}" != "--no-build" ]; then
    bash "${ROOT}/Scripts/build.sh" "${CONFIG}" "${TARGET_ARCH}"
fi

BUILD_DIR="${ROOT}/build/${CONFIG}-${TARGET_ARCH}"
COMPONENT="$(find "${BUILD_DIR}" -maxdepth 4 -name KINETIC.component -print -quit)"
VST3="$(find "${BUILD_DIR}" -maxdepth 4 -name KINETIC.vst3 -print -quit)"

if [ -z "${COMPONENT}" ] || [ -z "${VST3}" ]; then
    echo "package-dmg: no built plug-ins in ${BUILD_DIR} (run without --no-build)" >&2
    exit 1
fi

VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
           "${COMPONENT}/Contents/Info.plist" 2>/dev/null || echo '0.0.0')"
STAMP="$(date '+%Y-%m-%d')"
STAGE="${ROOT}/build/dmg-${TARGET_ARCH}"
DMG="${ROOT}/build/KINETIC-${VERSION}-macOS-${TARGET_ARCH}.dmg"

if [ "${TARGET_ARCH}" = "universal" ]; then
    ARCH_NOTE="arm64 + x86_64 (Universal)"
else
    ARCH_NOTE="arm64 (Apple Silicon)"
fi

rm -rf "${STAGE}"
mkdir -p "${STAGE}"

# ditto, not cp: the bundle's layout, permissions and signature have to survive the copy.
ditto "${COMPONENT}" "${STAGE}/KINETIC.component"
ditto "${VST3}" "${STAGE}/KINETIC.vst3"

# Sign after staging: JUCE writes moduleinfo.json after signing the VST3 itself, which leaves the
# seal invalid, and a host scanner can refuse a bundle whose seal does not verify.
for bundle in "${STAGE}/KINETIC.component" "${STAGE}/KINETIC.vst3"; do
    codesign --force --deep --sign - "${bundle}" >/dev/null 2>&1
    codesign --verify --strict --deep "${bundle}"
done

cp "${ROOT}/LICENSE" "${STAGE}/LICENSE"
cp "${ROOT}/Docs/Manual.md" "${STAGE}/Manual.md"

cat > "${STAGE}/READ ME FIRST.txt" <<EOF
KINETIC ${VERSION} - Audio Unit MIDI Effect and VST3
====================================================

KINETIC is a sample-accurate, pure MIDI FX step sequencer for macOS 11 or later.
It generates MIDI from the host's own transport position and makes no audio of
its own.

THIS BUILD
  version: ${VERSION}, built ${STAMP}
  arch:    ${ARCH_NOTE}
  signed:  ad-hoc only - no Developer ID, not notarised

The last line of the header matters. This disk image is not signed by an Apple
Developer, so macOS will treat it as an unidentified download and may refuse to
open it, and a host may refuse or silently skip the plug-ins once they are
copied out of it. Nothing is wrong with the files; the two sections below say
what macOS says and what to type.

WHAT IS IN HERE
  KINETIC.component   Audio Unit MIDI Effect (Logic Pro, MainStage)
  KINETIC.vst3        VST3 instrument (Live, Cubase, Studio One, Bitwig, ...)
  Manual.md           the user manual
  LICENSE             GNU GPLv3, which this plug-in is released under

INSTALL
  1. Copy the two bundles into your per-user plug-in folders:

       KINETIC.component  ->  ~/Library/Audio/Plug-Ins/Components
       KINETIC.vst3       ->  ~/Library/Audio/Plug-Ins/VST3

     "~" is your home folder. In Finder use Go > Go to Folder... (Shift-Cmd-G),
     paste ~/Library/Audio/Plug-Ins, and create Components or VST3 with
     File > New Folder if it is not there. Copy in Finder, not from a Terminal
     inside the mounted image, so the bundles arrive whole.

  2. Quit every host application completely (Cmd-Q, not just closing the window)
     and reopen it. Hosts scan at launch and keep the previously loaded binary
     mapped otherwise; rescanning inside a running host is not enough.

  3. Insert KINETIC from the plug-in list. In Logic Pro it is a MIDI FX on the
     instrument track (Audio Units > Kinetic MIDI > KINETIC); in Ableton Live it
     is the VST3, placed on a MIDI track.

IF macOS REFUSES TO OPEN THIS DISK IMAGE
  "KINETIC-.dmg cannot be opened because Apple cannot check it for malicious
  software", or "... is damaged and can't be opened":

  - Right-click (or Control-click) the .dmg in Finder, choose Open, then Open
    again in the dialog. This is allowed once and macOS remembers the image; or
  - Open System Settings > Privacy & Security, scroll to Security, and click
    "Open Anyway" next to KINETIC; or
  - Remove the quarantine flag that the download added, then open it normally:

      xattr -dr com.apple.quarantine ~/Downloads/KINETIC-${VERSION}-macOS-${TARGET_ARCH}.dmg

    (Change the path if the image is somewhere other than Downloads. You can
    type "xattr -dr com.apple.quarantine " with the space, then drag the .dmg
    from Finder into the Terminal window to fill in the path.)

IF A HOST REFUSES OR SILENTLY SKIPS THE PLUG-IN
  Copying the bundles out of a quarantined image carries that flag onto them. A
  host may then refuse to load them, or leave them out of its plug-in scan with
  no message at all. Remove the flag on the installed copies:

      xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/KINETIC.component
      xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/KINETIC.vst3

  then quit the host completely and reopen it.

  If a host still will not list the plug-in, check that the bundle's seal is
  intact - it should say "valid on disk" and "satisfies its Designated
  Requirement":

      codesign --verify --strict --deep -v ~/Library/Audio/Plug-Ins/VST3/KINETIC.vst3

  On Apple Silicon, Logic Pro and MainStage additionally refuse Audio Units that
  are not signed by a known identity in some configurations; the VST3 is not
  affected by that rule. If the AU stays out of the list in Logic, use the VST3
  build where the host supports VST3, or build from source and install with
  Scripts/install-plugin.sh, which signs the bundle the same ad-hoc way.

WHY IT IS NOT SIGNED
  Signing with a Developer ID and notarising the result both need a paid Apple
  Developer account. Without one, macOS can only report that it cannot identify
  the download. The bundles themselves are ad-hoc signed, so their code seal is
  intact; what is missing is Apple's statement that the file is known.

FIRST STEPS
  Insert the plug-in, press PLAY in the host: KINETIC follows the host's own
  position, so it stays silent while the transport is stopped.

  Presets live in ~/Library/Application Support/KINETIC/Presets, one XML file
  per preset, one folder per category. The header's preset menu saves, renames,
  moves and deletes them; Section 4.6 of Manual.md covers it.

SOURCE AND LICENCE
  KINETIC is released under the GNU General Public License v3.0; the full text
  is in LICENSE. Source: https://github.com/santscho/KINETIC
EOF

rm -f "${DMG}"

# UDZO: compressed and read-only, the format a download should be. HFS+ keeps the bundle
# metadata that copying out of the mounted image relies on. hdiutil reports itself deprecated on
# current macOS in favour of "diskutil image create", which older systems (11.0, the floor) do not
# have; the call still works and the checksum is verified below.
hdiutil create -volname "KINETIC ${VERSION}" -srcfolder "${STAGE}" -ov -format UDZO \
    -fs HFS+ "${DMG}" >/dev/null

hdiutil verify "${DMG}" >/dev/null

rm -rf "${STAGE}"

echo
echo "package-dmg: $(du -h "${DMG}" | cut -f1) at ${DMG}"
