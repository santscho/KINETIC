#pragma once

#include "Core/Sequencer/PatternState.h"

#include <juce_core/juce_core.h>

namespace kinetic::presets
{

/**
    One entry in the preset folder: the built-in factory reset, or one file in a category.

    The factory entry has no file, so it can be selected and loaded but never written to, which
    is what keeps the reset available after a preset has been overwritten.
*/
struct Entry
{
    juce::String name;
    juce::String category;
    juce::File file;                // empty for the factory entry
    bool isFactory = false;
};

/**
    The preset folder: one sub-folder per category, one document per preset.

    A preset is the same document the host stores in its session (see patternstate), wrapped in a
    <KINETIC_PRESET> element that carries the format, its version and the preset's name. Nothing
    new is serialised for presets: a preset and a saved session hold identical pattern data, so a
    preset can be inspected, backed up or copied between machines as plain text.

    Categories are the immediate sub-folders of the library. The default category, "User", is the
    library root itself, so a folder written before categories existed keeps working and no file
    has to move; every other category is a folder of its own and can be exported on its own.

    The library is a snapshot of the folder taken by reload(); a file added or removed behind the
    plug-in's back is picked up by refreshIfChanged(), which compares the folder's timestamp and
    re-reads it at most once per interval. That is what keeps a host's program list current
    without turning a per-query read into a folder scan. Only files that parse as a KINETIC
    preset appear in the list, so unrelated documents are ignored rather than offered as broken
    presets.
*/
class Library
{
public:
    explicit Library (juce::File rootDirectory = defaultDirectory());

    /** Re-reads the folder: the factory entry first, then every preset by category and name. */
    void reload();

    /**
        Re-reads the folder when its timestamp moved, at most once every `minimumIntervalMs`.

        Returns true when the list was re-read. Callers that answer a host's program queries on
        every refresh of a menu use this instead of reload(), so the folder is stat-ed rather
        than scanned while nothing changes.
    */
    bool refreshIfChanged (int minimumIntervalMs = 500);

    const juce::Array<Entry>& entries() const noexcept { return list; }
    juce::File directory() const noexcept { return root; }

    /** Every category, the default one included, sorted by name. */
    juce::StringArray categories() const;

    /**
        The entry holding `file`, or -1 when the folder has no such file.

        Entry numbers move whenever a preset is added, removed or renamed, so anything that holds
        on to one - the current program, for instance - has to be found again by its file. The
        factory entry has no file and is always entry 0.
    */
    int indexOfFile (const juce::File& file) const;

    /**
        The entry a stored preset refers to, or 0 when it no longer exists.

        A session saves the preset it was using as a file name and its category rather than an
        entry number: numbers move when the folder changes, while a name stays attached to its
        preset. The category is what tells two presets of the same name apart, and a file that
        was moved to another folder is still found by its name alone - only a preset that has
        been deleted falls back to the factory entry.
    */
    int indexForFileName (const juce::String& fileName, const juce::String& category = {}) const;

    /**
        Writes `pattern` as a preset called `name` in `category`.

        When `replacing` points at an existing preset its file is overwritten and the name is
        kept as given; otherwise a free file name is chosen in the category's folder, so two
        presets may share a display name without one silently replacing the other. `savedFile`
        receives the file that now holds the preset.
    */
    juce::Result save (const Pattern& pattern, int selectedLane, const juce::String& name,
                       const juce::File& replacing = {}, juce::File* savedFile = nullptr,
                       const juce::String& category = defaultCategoryName);

    /** Reads `entry` into `pattern` and `selectedLane`; on failure both are left untouched. */
    bool load (const Entry& entry, Pattern& pattern, int& selectedLane) const;

    juce::Result remove (const Entry& entry);

    /** Renames a preset: the document's name and its file name move together. */
    juce::Result rename (const Entry& entry, const juce::String& newName,
                         juce::File* renamedFile = nullptr);

    /**
        Moves a preset to another category, keeping its name.

        The document is written into the new category before the old file is removed, so a failed
        write cannot lose the pattern. `movedFile` receives where the preset now lives.
    */
    juce::Result moveToCategory (const Entry& entry, const juce::String& category,
                                 juce::File* movedFile = nullptr);

    /** Copies one preset document to `target`, which may be anywhere on disk. */
    juce::Result exportPreset (const Entry& entry, const juce::File& target) const;

    /** Writes every preset of one category into a single archive named after it. */
    juce::Result exportArchive (const juce::File& target, const juce::String& category) const;

    /** What an import does with a preset whose name is already in the category. */
    enum class ImportPolicy
    {
        skip,           // leave the existing preset alone
        keepBoth,       // bring the imported one in under "<name> version" (the default)
        overwrite       // replace the existing preset's pattern, keeping its name and file
    };

    /** What an import did, so the editor can report it. */
    struct ImportSummary
    {
        int imported = 0;
        int skipped = 0;
        juce::StringArray failures;         // one line per source that could not be imported
    };

    /**
        Imports preset documents and category archives, in any order.

        A single preset goes into `fallbackCategory`. An archive goes into the category it is
        named after, which is created when it does not exist yet, so a folder can be moved
        between machines one category at a time. A name already in the target category follows
        `policy`.
    */
    ImportSummary importFrom (const juce::Array<juce::File>& sources,
                              ImportPolicy policy = ImportPolicy::keepBoth,
                              const juce::String& fallbackCategory = defaultCategoryName);

    /** Creates an empty category, so presets can be saved into it. */
    juce::Result createCategory (const juce::String& name);

    /** `~/Library/Application Support/KINETIC/Presets`. */
    static juce::File defaultDirectory();

    /** A name that is safe to use as a file or folder name, or `fallback` when nothing is left. */
    static juce::String sanitiseName (const juce::String& name,
                                      const juce::String& fallback = "Preset");

    static constexpr const char* fileExtension = "kineticpreset";
    static constexpr const char* archiveExtension = "kineticset";
    static constexpr const char* factoryName = "FACTORY DEFAULT";
    static constexpr const char* defaultCategoryName = "User";

private:
    /** The folder a category is stored in; the default category lives in the library root. */
    juce::File categoryDirectory (const juce::String& category) const;

    void addPresetsFrom (const juce::File& folder, const juce::String& category);

    juce::File fileForName (const juce::String& name, const juce::String& category) const;
    static juce::String readName (const juce::File& file);

    /** Writes one preset document into `category`. Refuses a document it cannot read.
        `imported` says whether anything was written, so a skipped preset is not counted. */
    juce::Result importElement (const juce::XmlElement& xml, ImportPolicy policy,
                                const juce::String& category, bool& imported);

    /** The entry called `name` in `category`, ignoring the factory entry, or -1. */
    int entryNamed (const juce::String& name, const juce::String& category) const;
    bool nameExists (const juce::String& name, const juce::String& category) const;
    juce::String versionedName (const juce::String& name, const juce::String& category) const;

    juce::File root;
    juce::Array<Entry> list;

    // The folder's timestamp when it was last read, and when it was last checked.
    juce::Time folderStamp;
    juce::uint32 lastCheckMs = 0;

    JUCE_DECLARE_NON_COPYABLE (Library)
};



} // namespace kinetic::presets
