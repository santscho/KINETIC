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
        testCategoryPresetSurvivesReopening();
        testMoveToCategory();
        testCategoryRenameAndDelete();
        testFolderChangesArePickedUp();
        testExportAndImport();
        testImportPolicies();
        testCategories();
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

        expectEquals (library.indexForFileName (saved.getFileName()), 1);

        // Deleted or renamed since the session was saved.
        expectEquals (library.indexForFileName ("Gone.kineticpreset"), 0);

        // A session that never used a preset.
        expectEquals (library.indexForFileName ({}), 0);
    }

    void testCategoryPresetSurvivesReopening()
    {
        beginTest ("A preset saved in a category is found again when the session is reopened");

        auto session = makeLibrary ("session-category");
        expect (session.createCategory ("Bass").wasOk());

        // One preset of that name in each category, so the category is what tells them apart.
        auto deepPattern = makePattern();
        deepPattern.rootNote = 40;

        juce::File userFile, bassFile;
        expect (session.save (makePattern(), 0, "Bass", {}, &userFile).wasOk());
        expect (session.save (deepPattern, 4, "Bass", {}, &bassFile, "Bass").wasOk());

        expect (bassFile.getParentDirectory() == session.directory().getChildFile ("Bass"));
        expectEquals (bassFile.getFileName(), juce::String ("Bass.kineticpreset"));

        // What the session stores: the bare file name plus the category.
        const auto storedName = bassFile.getFileName();
        const auto storedCategory = session.entries()[session.indexOfFile (bassFile)].category;

        // Reopening builds a new library over the same folder.
        presets::Library reopened (session.directory());
        expect (reopened.categories().contains ("Bass"));

        const auto resolved = reopened.indexForFileName (storedName, storedCategory);
        expect (resolved > 0, "the preset fell back to the factory entry");
        expectEquals (reopened.entries()[resolved].category, juce::String ("Bass"));

        Pattern loaded;
        int lane = 0;
        expect (reopened.load (reopened.entries()[resolved], loaded, lane));
        expectEquals (loaded.rootNote, 40);
        expectEquals (lane, 4);

        // The name alone would be ambiguous; the category picks the right one.
        const auto userResolved = reopened.indexForFileName (
            userFile.getFileName(), presets::Library::defaultCategoryName);
        expectEquals (reopened.entries()[userResolved].category,
                      juce::String (presets::Library::defaultCategoryName));
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

    void testExportAndImport()
    {
        beginTest ("A preset and a folder archive survive being exported and imported");

        auto source = makeLibrary ("export-source");

        juce::File bass, lead;
        expect (source.save (makePattern(), 2, "Bass", {}, &bass).wasOk());

        auto brighter = makePattern();
        brighter.rootNote = 48;
        expect (source.save (brighter, 1, "Lead", {}, &lead).wasOk());

        // One preset on its own.
        const auto single = temp.getChildFile ("Exported Bass.kineticpreset");
        expect (source.exportPreset (source.entries()[1], single).wasOk());
        expect (single.existsAsFile());

        // The whole default category.
        const auto archive = temp.getChildFile ("Set.kineticset");
        expect (source.exportArchive (archive, presets::Library::defaultCategoryName).wasOk());
        expect (archive.existsAsFile());

        auto destination = makeLibrary ("export-destination");

        juce::Array<juce::File> files;
        files.add (single);

        auto summary = destination.importFrom (files);
        expectEquals (summary.imported, 1);
        expect (summary.failures.isEmpty());
        expectEquals (destination.entries().size(), 2);
        expectEquals (destination.entries()[1].name, juce::String ("Bass"));

        // The pattern, the lane and the name all came through the file.
        Pattern loaded;
        int lane = 0;
        expect (destination.load (destination.entries()[1], loaded, lane));
        expectEquals (loaded.rootNote, 67);
        expectEquals (loaded.beatsPerBar, 3);
        expectEquals (lane, 2);

        // The archive brings both presets, and the names already there are kept rather than
        // overwritten.
        files.clear();
        files.add (archive);

        summary = destination.importFrom (files);
        expectEquals (summary.imported, 2);
        expectEquals (destination.entries().size(), 4);

        juce::StringArray names;
        for (const auto& entry : destination.entries())
            names.add (entry.name);

        expect (names.contains ("Bass"));
        expect (names.contains ("Bass version"));
        expect (names.contains ("Lead"));

        // A file that is not a preset is reported rather than imported.
        const auto junk = temp.getChildFile ("junk.kineticpreset");
        junk.replaceWithText ("<NOTES/>");

        files.clear();
        files.add (junk);

        summary = destination.importFrom (files);
        expectEquals (summary.imported, 0);
        expectEquals (summary.failures.size(), 1);
        expectEquals (destination.entries().size(), 4);
    }

    void testImportPolicies()
    {
        beginTest ("An import keeps both, skips or replaces a name already in the folder");

        // The incoming preset is called Bass but carries a different root note.
        auto origin = makeLibrary ("policy-origin");

        auto darker = makePattern();
        darker.rootNote = 40;

        juce::File incomingFile;
        expect (origin.save (darker, 0, "Bass", {}, &incomingFile).wasOk());

        juce::Array<juce::File> files;
        files.add (incomingFile);

        // Keep both, the default: the folder ends up with both patterns.
        {
            auto library = makeLibrary ("policy-keep-both");
            juce::File local;
            expect (library.save (makePattern(), 0, "Bass", {}, &local).wasOk());

            const auto summary = library.importFrom (files);
            expectEquals (summary.imported, 1);
            expectEquals (summary.skipped, 0);
            expectEquals (library.entries().size(), 3);
            expectEquals (library.entries()[2].name, juce::String ("Bass version"));
        }

        // Skip: the local preset is untouched.
        {
            auto library = makeLibrary ("policy-skip");
            juce::File local;
            expect (library.save (makePattern(), 0, "Bass", {}, &local).wasOk());

            const auto summary = library.importFrom (files, presets::Library::ImportPolicy::skip);
            expectEquals (summary.imported, 0);
            expectEquals (summary.skipped, 1);
            expectEquals (library.entries().size(), 2);

            Pattern loaded;
            int lane = 0;
            expect (library.load (library.entries()[1], loaded, lane));
            expectEquals (loaded.rootNote, 67);
        }

        // Overwrite: the preset keeps its name and file, and takes the new pattern.
        {
            auto library = makeLibrary ("policy-overwrite");
            juce::File local;
            expect (library.save (makePattern(), 0, "Bass", {}, &local).wasOk());

            const auto summary = library.importFrom (files,
                                                     presets::Library::ImportPolicy::overwrite);
            expectEquals (summary.imported, 1);
            expectEquals (summary.skipped, 0);
            expectEquals (library.entries().size(), 2);
            expectEquals (library.entries()[1].name, juce::String ("Bass"));
            expect (library.entries()[1].file == local);

            Pattern loaded;
            int lane = 0;
            expect (library.load (library.entries()[1], loaded, lane));
            expectEquals (loaded.rootNote, 40);
        }
    }

    void testCategories()
    {
        beginTest ("Categories are folders, and each one archives on its own");

        auto library = makeLibrary ("categories");

        expect (library.createCategory ("Bass").wasOk());
        expect (library.categories().contains ("Bass"));
        expect (library.categories().contains (presets::Library::defaultCategoryName));

        juce::File home, deep;
        expect (library.save (makePattern(), 0, "Home", {}, &home).wasOk());
        expect (library.save (makePattern(), 0, "Deep", {}, &deep, "Bass").wasOk());

        // The default category is the library root, so a folder written before categories
        // existed keeps its presets where they are; other categories are folders of their own.
        expect (home.getParentDirectory() == library.directory());
        expect (deep.getParentDirectory() == library.directory().getChildFile ("Bass"));

        for (const auto& entry : library.entries())
        {
            if (entry.name == "Home")
                expectEquals (entry.category, juce::String (presets::Library::defaultCategoryName));
            else if (entry.name == "Deep")
                expectEquals (entry.category, juce::String ("Bass"));
        }

        // One archive holds one category.
        const auto archive = temp.getChildFile ("Bass.kineticset");
        expect (library.exportArchive (archive, "Bass").wasOk());

        auto destination = makeLibrary ("categories-destination");

        juce::Array<juce::File> files;
        files.add (archive);

        const auto summary = destination.importFrom (files);
        expectEquals (summary.imported, 1);
        expect (summary.failures.isEmpty());
        expectEquals (destination.entries().size(), 2);
        expectEquals (destination.entries()[1].name, juce::String ("Deep"));

        // The archive restores its own folder, which did not exist on this machine.
        expectEquals (destination.entries()[1].category, juce::String ("Bass"));
        expect (destination.categories().contains ("Bass"));
        expect (destination.entries()[1].file.getParentDirectory()
                    == destination.directory().getChildFile ("Bass"));

        Pattern loaded;
        int lane = 0;
        expect (destination.load (destination.entries()[1], loaded, lane));
        expectEquals (loaded.rootNote, 67);

        // A category with nothing in it has nothing to archive.
        expect (library.createCategory ("Empty").wasOk());
        expect (library.exportArchive (temp.getChildFile ("Empty.kineticset"), "Empty").failed());
    }

    void testMoveToCategory()
    {
        beginTest ("A preset moves to another category and keeps its name");

        auto library = makeLibrary ("move");
        expect (library.createCategory ("Bass").wasOk());

        juce::File file;
        expect (library.save (makePattern(), 2, "Line", {}, &file).wasOk());

        const auto index = library.indexOfFile (file);
        expect (index > 0);

        juce::File moved;
        expect (library.moveToCategory (library.entries()[index], "Bass", &moved).wasOk());

        // Moved, not copied: the file lives in the category and the old one is gone.
        expect (moved.getParentDirectory() == library.directory().getChildFile ("Bass"));
        expect (! file.existsAsFile());
        expectEquals (library.entries().size(), 2);

        const auto movedIndex = library.indexOfFile (moved);
        expect (movedIndex > 0);
        expectEquals (library.entries()[movedIndex].name, juce::String ("Line"));
        expectEquals (library.entries()[movedIndex].category, juce::String ("Bass"));

        // The pair a session stores still names the preset after the move.
        expectEquals (library.indexForFileName (moved.getFileName(), "Bass"), movedIndex);

        Pattern loaded;
        int lane = 0;
        expect (library.load (library.entries()[movedIndex], loaded, lane));
        expectEquals (loaded.rootNote, 67);
        expectEquals (lane, 2);

        // Moving a preset to the category it is already in changes nothing.
        expect (library.moveToCategory (library.entries()[movedIndex], "Bass", &moved).wasOk());
        expectEquals (library.entries().size(), 2);
    }

    void testCategoryRenameAndDelete()
    {
        beginTest ("Categories rename with their presets, and only empty ones can be deleted");

        auto library = makeLibrary ("category-admin");
        expect (library.createCategory ("Bass").wasOk());

        juce::File line;
        expect (library.save (makePattern(), 1, "Line", {}, &line, "Bass").wasOk());
        const auto storedName = line.getFileName();

        expect (library.renameCategory ("Bass", "Sub").wasOk());
        expect (! library.categories().contains ("Bass"));
        expect (library.categories().contains ("Sub"));
        expectEquals (library.entries().size(), 2);

        const auto index = library.indexForFileName (storedName, "Sub");
        expect (index > 0, "the preset was not found in the renamed category");
        expectEquals (library.entries()[index].name, juce::String ("Line"));
        expectEquals (library.entries()[index].category, juce::String ("Sub"));
        expect (library.entries()[index].file.getParentDirectory()
                    == library.directory().getChildFile ("Sub"));

        // A session saved before the rename still names the same preset: its category is stale,
        // but the file name finds it.
        const auto stale = library.indexForFileName (storedName, "Bass");
        expect (stale > 0);
        expect (library.entries()[stale].file == library.entries()[index].file);

        // Renaming onto a folder that exists is refused instead of merging two categories.
        expect (library.createCategory ("Pad").wasOk());
        expect (library.renameCategory ("Sub", "Pad").failed());
        expect (library.categories().contains ("Sub"));
        expect (library.categories().contains ("Pad"));

        // Only a category with no presets in it can be deleted.
        expect (! library.isCategoryEmpty ("Sub"));
        expect (library.deleteCategory ("Sub").failed());
        expect (library.isCategoryEmpty ("Pad"));
        expect (library.deleteCategory ("Pad").wasOk());
        expect (! library.categories().contains ("Pad"));

        // The default category is the library root and stays.
        expect (library.deleteCategory (presets::Library::defaultCategoryName).failed());

        // A folder holding anything that is not a preset is refused, and the message names it,
        // because deleting the category would take that file with it.
        expect (library.createCategory ("Sketches").wasOk());
        const auto sketches = library.directory().getChildFile ("Sketches");
        const auto note = sketches.getChildFile ("notes.txt");
        expect (note.replaceWithText ("not a preset"));
        expect (library.isCategoryEmpty ("Sketches"));

        const auto refused = library.deleteCategory ("Sketches");
        expect (refused.failed());
        expect (refused.getErrorMessage().contains ("notes.txt"));
        expect (note.existsAsFile());
        expect (library.categories().contains ("Sketches"));

        // Renaming applies the same rule as deleting: the file is neither left behind as a
        // stranded folder nor carried somewhere the user did not ask for, so the category keeps
        // its name and its contents.
        const auto refusedRename = library.renameCategory ("Sketches", "Ideas");
        expect (refusedRename.failed());
        expect (refusedRename.getErrorMessage().contains ("notes.txt"));
        expect (note.existsAsFile());
        expect (library.categories().contains ("Sketches"));
        expect (! library.categories().contains ("Ideas"));

        // A subfolder is in the way just the same.
        expect (sketches.getChildFile ("older").createDirectory().wasOk());
        expect (library.renameCategory ("Sketches", "Ideas").failed());
        expect (library.deleteCategory ("Sketches").failed());
        expect (sketches.getChildFile ("older").isDirectory());

        // With nothing but presets in the folder - or nothing at all - both operations work, and
        // the folder that is left behind is gone rather than listed as an empty category.
        expect (note.deleteFile());
        expect (sketches.getChildFile ("older").deleteRecursively());
        expect (library.renameCategory ("Sketches", "Ideas").wasOk());
        expect (! sketches.isDirectory());

        const auto ideas = library.directory().getChildFile ("Ideas");
        expect (ideas.isDirectory());
        expect (library.categories().contains ("Ideas"));
        expect (library.deleteCategory ("Ideas").wasOk());
        expect (! ideas.isDirectory());
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
