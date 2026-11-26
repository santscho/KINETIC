#include <juce_core/juce_core.h>

#include "Core/Preset/PresetLibrary.h"

namespace kinetic
{

class PresetLibraryTests final : public juce::UnitTest
{
public:
    PresetLibraryTests() : juce::UnitTest ("Presets", "kinetic") {}

    ~PresetLibraryTests() override
    {
        temp.deleteRecursively();
    }

    void runTest() override
    {
        testSaveAndLoad();
        testNamesGetTheirOwnFiles();
        testForeignFilesAreIgnored();
        testFactoryReset();
        testDelete();
        testIndexOfFileSurvivesASort();
        testStoredFileNameFallsBackToFactory();
        testFolderChangesArePickedUp();
        testRename();
        testEmptyNameIsRefused();
    }

private:
    juce::File temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("kinetic-preset-tests-" + juce::Uuid().toString());

    presets::Library makeLibrary (const juce::String& folder)
    {
        auto directory = temp.getChildFile (folder);
        directory.createDirectory();
        return presets::Library (directory);
    }

    static Pattern makePattern()
    {
        Pattern pattern;
        pattern.rootNote = 67;
        pattern.beatsPerBar = 3;
        pattern.globalScale = ScaleType::dorian;
        pattern.lanes[0].settings.enabled = true;
        pattern.lanes[0].settings.gateLength = 5;
        pattern.lanes[0].steps[0].trigger = true;
        pattern.lanes[0].steps[3].pitch = 7;
        pattern.lanes[0].steps[4].ratchets = 3;

        return pattern;
    }

    void testSaveAndLoad()
    {
        beginTest ("A saved preset comes back unchanged");

        auto library = makeLibrary ("round-trip");
        const auto pattern = makePattern();

        juce::File saved;
        const auto result = library.save (pattern, 3, "Acid Line", {}, &saved);
        expect (result.wasOk(), "save failed: " + result.getErrorMessage()
                                    + " (folder " + library.directory().getFullPathName() + ")");
        expect (saved.existsAsFile(), "missing file: " + saved.getFullPathName());
        expectEquals (library.entries().size(), 2);   // factory + the new preset

        Pattern loaded;
        int lane = 0;

        expect (library.load (library.entries()[1], loaded, lane));
        expectEquals (loaded.rootNote, 67);
        expectEquals (loaded.beatsPerBar, 3);
        expect (loaded.globalScale == ScaleType::dorian);
        expect (loaded.lanes[0].steps[0].trigger);
        expectEquals (static_cast<int> (loaded.lanes[0].steps[3].pitch), 7);
        expectEquals (static_cast<int> (loaded.lanes[0].steps[4].ratchets), 3);
        expectEquals (lane, 3);
    }

    void testNamesGetTheirOwnFiles()
    {
        beginTest ("Two presets may share a name without overwriting each other");

        auto library = makeLibrary ("unique-names");

        juce::File first, second;
        expect (library.save (makePattern(), 0, "Bass", {}, &first).wasOk());
        expect (library.save (makePattern(), 0, "Bass", {}, &second).wasOk());

        expect (first != second);
        expect (first.existsAsFile());
        expect (second.existsAsFile());
        expectEquals (library.entries().size(), 3);
    }

    void testForeignFilesAreIgnored()
    {
        beginTest ("Documents that are not presets are left out of the list");

        auto library = makeLibrary ("foreign");

        library.directory().getChildFile ("notes.kineticpreset")
             .replaceWithText ("<NOTES/>");
        library.directory().getChildFile ("song.xml")
             .replaceWithText ("<KINETIC_PRESET format=\"other\" version=\"1\" name=\"x\"/>");

        library.reload();
        expectEquals (library.entries().size(), 1);
        expect (library.entries()[0].isFactory);
    }

    void testFactoryReset()
    {
        beginTest ("The factory entry is read-only and restores the default pattern");

        auto library = makeLibrary ("factory");
        const auto& factory = library.entries()[0];

        expect (factory.isFactory);
        expect (factory.file == juce::File());
        expect (library.remove (factory).failed());

        Pattern pattern = makePattern();
        int lane = 5;

        expect (library.load (factory, pattern, lane));
        expectEquals (pattern.rootNote, 60);
        expectEquals (pattern.beatsPerBar, 4);
        expect (pattern.lanes[0].settings.enabled);
        expect (! pattern.lanes[0].steps[0].trigger);
        expectEquals (lane, 0);
    }

    void testDelete()
    {
        beginTest ("Deleting a preset removes its file and leaves the factory entry");

        auto library = makeLibrary ("delete");

        juce::File saved;
        expect (library.save (makePattern(), 0, "Throwaway", {}, &saved).wasOk());
        expectEquals (library.entries().size(), 2);

        expect (library.remove (library.entries()[1]).wasOk());
        expect (! saved.existsAsFile());
        expectEquals (library.entries().size(), 1);
    }

    void testIndexOfFileSurvivesASort()
    {
        beginTest ("A preset is found again by its file after the list re-sorts");

        auto library = makeLibrary ("remap");

        juce::File alpha, zulu;
        expect (library.save (makePattern(), 0, "Alpha", {}, &alpha).wasOk());
        expect (library.save (makePattern(), 0, "Zulu", {}, &zulu).wasOk());

        // Factory, Alpha, Zulu: the current program would be 2.
        expectEquals (library.indexOfFile (alpha), 1);
        expectEquals (library.indexOfFile (zulu), 2);

        // Deleting Alpha re-sorts everything after it one place to the left, so holding the old
        // number 2 would now name a preset that no longer exists.
        expect (library.remove (library.entries()[1]).wasOk());
        expectEquals (library.entries().size(), 2);
        expectEquals (library.entries()[1].name, juce::String ("Zulu"));
        expectEquals (library.indexOfFile (zulu), 1);

        // The deleted file is gone, which is what sends a deleted current program to the factory
        // entry, and the factory entry itself is always entry 0.
        expectEquals (library.indexOfFile (alpha), -1);
        expectEquals (library.indexOfFile (juce::File()), 0);
    }

    void testStoredFileNameFallsBackToFactory()
    {
        beginTest ("A session's stored preset file name resolves, or falls back to factory");

        auto library = makeLibrary ("stored-name");

        juce::File saved;
        expect (library.save (makePattern(), 0, "Saved", {}, &saved).wasOk());

        expectEquals (presets::indexForFileName (library, saved.getFileName()), 1);

        // Deleted or renamed since the session was saved.
        expectEquals (presets::indexForFileName (library, "Gone.kineticpreset"), 0);

        // A session that never used a preset.
        expectEquals (presets::indexForFileName (library, {}), 0);
    }

    void testFolderChangesArePickedUp()
    {
        beginTest ("A changed folder is noticed, stat-ed between checks rather than scanned");

        auto library = makeLibrary ("stale");

        // A fresh library has just read the folder.
        expect (! library.refreshIfChanged (0));
        expectEquals (library.entries().size(), 1);

        // Somebody else writes a preset into the same folder.
        juce::Thread::sleep (20);

        juce::File written;
        expect (presets::Library (library.directory())
                    .save (makePattern(), 0, "Added Outside", {}, &written).wasOk());

        // Inside the interval the check does not even look at the folder.
        expect (! library.refreshIfChanged (60000));
        expectEquals (library.entries().size(), 1);

        // Past it, the folder's timestamp is read and the list picks the preset up.
        expect (library.refreshIfChanged (0));
        expectEquals (library.entries().size(), 2);
        expectEquals (library.entries()[1].name, juce::String ("Added Outside"));

        // Nothing moved since: another look is a no-op.
        expect (! library.refreshIfChanged (0));

        // A removal is noticed the same way.
        juce::Thread::sleep (20);
        expect (written.deleteFile());

        expect (library.refreshIfChanged (0));
        expectEquals (library.entries().size(), 1);
    }

    void testRename()
    {
        beginTest ("Renaming moves the document name and the file together");

        auto library = makeLibrary ("rename");

        juce::File first;
        expect (library.save (makePattern(), 3, "Old Name", {}, &first).wasOk());

        juce::File renamed;
        expect (library.rename (library.entries()[1], "New Name", &renamed).wasOk());

        expect (! first.existsAsFile());
        expect (renamed.existsAsFile());
        expectEquals (library.entries().size(), 2);
        expectEquals (library.entries()[1].name, juce::String ("New Name"));

        Pattern loaded;
        int lane = 0;

        expect (library.load (library.entries()[1], loaded, lane));
        expectEquals (loaded.rootNote, 67);
        expectEquals (lane, 3);
    }

    void testEmptyNameIsRefused()
    {
        beginTest ("A preset with no name is refused rather than written as an empty file");

        auto library = makeLibrary ("empty-name");
        expect (library.save (makePattern(), 0, "   ").failed());
        expectEquals (library.entries().size(), 1);
    }
};

static PresetLibraryTests presetLibraryTests;

} // namespace kinetic
