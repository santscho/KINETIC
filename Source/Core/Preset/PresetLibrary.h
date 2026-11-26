#pragma once

#include "Core/Sequencer/PatternState.h"

#include <juce_core/juce_core.h>

namespace kinetic::presets
{

/**
    One entry in the preset folder: the built-in factory reset, or one file.

    The factory entry has no file, so it can be selected and loaded but never written to, which
    is what keeps the reset available after a preset has been overwritten.
*/
struct Entry
{
    juce::String name;
    juce::File file;                // empty for the factory entry
    bool isFactory = false;
};

/**
    The preset folder: one document per preset, in the user's Application Support directory.

    A preset is the same document the host stores in its session (see patternstate), wrapped in a
    <KINETIC_PRESET> element that carries the format, its version and the preset's name. Nothing
    new is serialised for presets: a preset and a saved session hold identical pattern data, so a
    preset can be inspected, backed up or copied between machines as plain text.

    The library is a snapshot of the folder taken by reload(); a file added or removed behind the
    plug-in's back is picked up the next time the list is read. Only files that parse as a KINETIC
    preset appear in the list, so unrelated documents in the folder are ignored rather than
    offered as broken presets.
*/
class Library
{
public:
    explicit Library (juce::File rootDirectory = defaultDirectory());

    /** Re-reads the folder: the factory entry first, then every preset by name. */
    void reload();

    const juce::Array<Entry>& entries() const noexcept { return list; }
    juce::File directory() const noexcept { return root; }

    /**
        Writes `pattern` as a preset called `name`.

        When `replacing` points at an existing preset its file is overwritten and the name is
        kept as given; otherwise a free file name is chosen, so two presets may share a display
        name without one silently replacing the other. `savedFile` receives the file that now
        holds the preset.
    */
    juce::Result save (const Pattern& pattern, int selectedLane, const juce::String& name,
                       const juce::File& replacing = {}, juce::File* savedFile = nullptr);

    /** Reads `entry` into `pattern` and `selectedLane`; on failure both are left untouched. */
    bool load (const Entry& entry, Pattern& pattern, int& selectedLane) const;

    juce::Result remove (const Entry& entry);

    /** `~/Library/Application Support/KINETIC/Presets`. */
    static juce::File defaultDirectory();

    /** A name that is safe to use as a file name, or `fallback` when nothing is left. */
    static juce::String sanitiseName (const juce::String& name,
                                      const juce::String& fallback = "Preset");

    static constexpr const char* fileExtension = "kineticpreset";
    static constexpr const char* factoryName = "FACTORY DEFAULT";

private:
    juce::File fileForName (const juce::String& name) const;
    static juce::String readName (const juce::File& file);

    juce::File root;
    juce::Array<Entry> list;

    JUCE_DECLARE_NON_COPYABLE (Library)
};

} // namespace kinetic::presets
