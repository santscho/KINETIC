#include "PresetLibrary.h"

#include <algorithm>
#include <vector>

namespace kinetic::presets
{

namespace
{
    constexpr const char* rootTag = "KINETIC_PRESET";
    constexpr const char* formatIdentifier = "kinetic.preset";
    constexpr int formatVersion = 1;

    // A category archive: one <KINETIC_PRESET> document per preset, under one element whose
    // `name` is the category. It parses with the same per-preset reading a single file uses,
    // which is what keeps exporting and importing free of a second pattern format.
    constexpr const char* archiveTag = "KINETIC_PRESETS";
    constexpr const char* archiveFormatIdentifier = "kinetic.presets";
    constexpr int archiveFormatVersion = 1;

    /** The document a preset file holds: format, version, name, then the PATTERN element. */
    bool isPresetElement (const juce::XmlElement& xml) noexcept
    {
        return xml.hasTagName (rootTag)
                && xml.getStringAttribute ("format") == formatIdentifier
                && xml.getIntAttribute ("version", 0) == formatVersion;
    }

    /**
        The first thing in a category folder that a preset is not, described for a message.

        A category folder belongs to the plug-in and holds presets. Renaming and deleting both act
        on the folder itself - one moves it, the other removes it - so both ask this first: a note,
        a rendering or a subfolder somebody left there keeps the folder where it is, and the
        message names the item, because neither operation should move or remove a file the plug-in
        did not write.
    */
    juce::String foreignItemIn (const juce::File& folder)
    {
        for (const auto& child : folder.findChildFiles (juce::File::findFilesAndDirectories, false))
        {
            if (child.isDirectory())
                return "\"" + child.getFileName() + "\", which is a folder";

            if (! child.hasFileExtension (Library::fileExtension))
                return "\"" + child.getFileName() + "\"";

            const auto xml = juce::XmlDocument::parse (child);

            // A file that claims the preset extension but does not read as one is not a preset
            // either, so it is treated the same way: the folder stays.
            if (xml == nullptr || ! isPresetElement (*xml))
                return "\"" + child.getFileName() + "\"";
        }

        return {};
    }

    /** `action` is what the caller wanted to do to the folder, so both messages read the same. */
    juce::Result checkFolderHoldsOnlyPresets (const juce::File& folder, const juce::String& category,
                                              const juce::String& action)
    {
        const auto foreign = foreignItemIn (folder);

        if (foreign.isEmpty())
            return juce::Result::ok();

        return juce::Result::fail ("The folder of \"" + category + "\" still holds " + foreign
                                   + ", which is not a preset. Only a folder that holds nothing "
                                     "but presets can be " + action + ". Move it elsewhere in "
                                     "Finder first.");
    }
}

Library::Library (juce::File rootDirectory)
    : root (rootDirectory)
{
    // A failed first creation is not fatal: the browser still opens and saving reports the
    // reason, which is more useful than refusing to show the editor at all.
    root.createDirectory();
    reload();
}

void Library::reload()
{
    // Taken before the scan: a change during it leaves the stamps different, which costs one more
    // re-read later rather than silently missing the change.
    folderStamp = root.getLastModificationTime();

    list.clearQuick();

    Entry factory;
    factory.name = factoryName;
    factory.isFactory = true;
    list.add (factory);

    if (root.isDirectory())
    {
        // One folder per category, then the presets that sit directly in the library root: those
        // are the default category, which is where they were before categories existed.
        for (const auto& folder : root.findChildFiles (juce::File::findDirectories, false, "*"))
            if (! folder.getFileName().startsWithChar ('.'))
                addPresetsFrom (folder, sanitiseName (folder.getFileName(), defaultCategoryName));

        addPresetsFrom (root, defaultCategoryName);
    }

    // The factory reset stays first; everything else reads by category, then by name, so
    // presets of one folder stay together in the host's program list.
    std::sort (list.begin(), list.end(), [] (const Entry& a, const Entry& b)
    {
        if (a.isFactory != b.isFactory)
            return a.isFactory;

        const auto byCategory = a.category.compareIgnoreCase (b.category);

        if (byCategory != 0)
            return byCategory < 0;

        return a.name.compareIgnoreCase (b.name) < 0;
    });
}

juce::StringArray Library::categories() const
{
    juce::StringArray names { defaultCategoryName };

    // Folders count even while empty, so a category created just before saving into it is there
    // to be chosen.
    if (root.isDirectory())
        for (const auto& folder : root.findChildFiles (juce::File::findDirectories, false, "*"))
            if (! folder.getFileName().startsWithChar ('.'))
                names.addIfNotAlreadyThere (sanitiseName (folder.getFileName(),
                                                          defaultCategoryName));

    for (const auto& entry : list)
        if (! entry.isFactory)
            names.addIfNotAlreadyThere (entry.category);

    names.sort (true);
    return names;
}

void Library::addPresetsFrom (const juce::File& folder, const juce::String& category)
{
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false,
                                                    juce::String ("*.") + fileExtension))
    {
        const auto name = readName (file);

        if (name.isEmpty())
            continue;       // not a KINETIC preset, so not a broken entry either

        Entry entry;
        entry.name = name;
        entry.category = category;
        entry.file = file;
        list.add (entry);
    }
}

bool Library::refreshIfChanged (int minimumIntervalMs)
{
    const auto now = juce::Time::getMillisecondCounter();

    if (now - lastCheckMs < static_cast<juce::uint32> (juce::jmax (0, minimumIntervalMs)))
        return false;

    lastCheckMs = now;

    if (root.getLastModificationTime() == folderStamp)
        return false;

    reload();
    return true;
}

int Library::indexOfFile (const juce::File& file) const
{
    if (file == juce::File())
        return 0;

    for (int i = 0; i < list.size(); ++i)
        if (list[i].file == file)
            return i;

    return -1;
}

int Library::indexForFileName (const juce::String& fileName, const juce::String& category) const
{
    if (fileName.isEmpty())
        return 0;

    // The category is checked first, so two categories holding a preset of the same name still
    // resolve to the one the session was saved with.
    if (category.isNotEmpty())
        if (const auto index = indexOfFile (categoryDirectory (category).getChildFile (fileName));
            index >= 0)
            return index;

    // Either the category has been renamed since, or the file was moved between folders by hand:
    // the name alone still identifies the preset.
    for (int i = 0; i < list.size(); ++i)
        if (! list[i].isFactory && list[i].file.getFileName() == fileName)
            return i;

    return 0;
}

juce::String Library::readName (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! isPresetElement (*xml))
        return {};

    return xml->getStringAttribute ("name").trim();
}

juce::File Library::categoryDirectory (const juce::String& category) const
{
    // The default category is the library root itself, so a folder that predates categories
    // keeps its presets where they are and nothing has to move.
    if (category.isEmpty() || category == defaultCategoryName)
        return root;

    return root.getChildFile (sanitiseName (category, defaultCategoryName));
}

juce::Result Library::createCategory (const juce::String& name)
{
    const auto cleanName = sanitiseName (name, "");

    if (cleanName.isEmpty())
        return juce::Result::fail ("A category must have a name.");

    if (const auto created = categoryDirectory (cleanName).createDirectory(); created.failed())
        return juce::Result::fail ("The category folder could not be created: "
                                   + created.getErrorMessage());

    reload();
    return juce::Result::ok();
}

bool Library::isCategoryEmpty (const juce::String& category) const
{
    for (const auto& entry : list)
        if (! entry.isFactory && entry.category == category)
            return false;

    return true;
}

juce::Result Library::renameCategory (const juce::String& from, const juce::String& to,
                                      juce::String* renamedTo)
{
    const auto source = from.isEmpty() ? juce::String (defaultCategoryName) : from;
    const auto target = sanitiseName (to, "");

    if (target.isEmpty())
        return juce::Result::fail ("A category must have a name.");

    // The default category is the preset folder itself, the one the categories live in: renaming
    // it would mean renaming the library, and the folder it holds is not a category but the hold
    // they sit in. Deleting refuses it for the same reason.
    if (source == defaultCategoryName)
        return juce::Result::fail ("The default category is the preset folder that holds the "
                                   "other categories, so it cannot be renamed. Save or move the "
                                   "preset into a category and rename that instead.");

    if (target == source)
    {
        if (renamedTo != nullptr)
            *renamedTo = target;

        return juce::Result::ok();
    }

    if (! categoryDirectory (source).isDirectory())
        return juce::Result::fail ("\"" + source + "\" is not a category.");

    // Renaming onto a folder that is already there would merge two categories, which is not what
    // renaming means; importing is how presets of two folders are brought together.
    if (categoryDirectory (target).isDirectory())
        return juce::Result::fail ("\"" + target + "\" already exists.");

    // Renaming moves the folder, and a file the plug-in did not write stays with it instead of
    // being left in a folder that lags behind as an empty category, or being carried somewhere the
    // user did not ask for: the rename is refused, exactly as deleting that folder would be.
    if (const auto clean = checkFolderHoldsOnlyPresets (categoryDirectory (source), source, "renamed");
        clean.failed())
        return clean;

    // Every preset is read before anything is written, so a file that cannot be read fails the
    // rename with the folder still as it was.
    struct Pending
    {
        Pattern pattern;
        int lane = 0;
        juce::String name;
        juce::File file;
    };

    std::vector<Pending> pending;

    for (const auto& entry : list)
    {
        if (entry.isFactory || entry.category != source)
            continue;

        const auto xml = juce::XmlDocument::parse (entry.file);

        if (xml == nullptr || ! isPresetElement (*xml))
            return juce::Result::fail ("\"" + entry.name + "\" could not be read.");

        Pending item;
        item.name = entry.name;
        item.file = entry.file;

        if (! patternstate::readFromXml (*xml, item.pattern, item.lane))
            return juce::Result::fail ("\"" + entry.name + "\" is in a format this build does not"
                                                                " read.");

        pending.push_back (std::move (item));
    }

    std::vector<juce::File> originals;

    for (const auto& item : pending)
    {
        if (const auto result = save (item.pattern, item.lane, item.name, {}, nullptr, target);
            result.failed())
            return result;

        originals.push_back (item.file);
    }

    for (const auto& file : originals)
        file.deleteFile();

    // A category with no presets in it is still a category, so renaming it leaves the new folder
    // in place even though nothing was written into it.
    if (pending.empty())
        categoryDirectory (target).createDirectory();

    // The presets were the folder's only contents, so it is empty now. The emptiness check stays
    // as a guard: nothing here may remove a file that is not one of ours.
    const auto folder = categoryDirectory (source);

    if (folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        folder.deleteRecursively();

    if (renamedTo != nullptr)
        *renamedTo = target;

    reload();
    return juce::Result::ok();
}

juce::Result Library::deleteCategory (const juce::String& category)
{
    if (category.isEmpty() || category == defaultCategoryName)
        return juce::Result::fail ("The default category cannot be deleted.");

    const auto folder = categoryDirectory (category);

    if (! folder.isDirectory())
        return juce::Result::fail ("\"" + category + "\" is not a category.");

    if (! isCategoryEmpty (category))
        return juce::Result::fail ("Only an empty category can be deleted. Move or delete its "
                                   "presets first.");

    // The empty check above only looks at presets; anything else in the folder would be deleted
    // with it, so the folder is left alone and the reason names what is in the way.
    if (const auto clean = checkFolderHoldsOnlyPresets (folder, category, "deleted"); clean.failed())
        return clean;

    if (! folder.deleteRecursively() && folder.isDirectory())
        return juce::Result::fail ("The category folder could not be deleted.");

    reload();
    return juce::Result::ok();
}

juce::Result Library::save (const Pattern& pattern, int selectedLane, const juce::String& name,
                            const juce::File& replacing, juce::File* savedFile,
                            const juce::String& category)
{
    const auto cleanName = name.trim();

    if (cleanName.isEmpty())
        return juce::Result::fail ("The preset name is empty.");

    if (const auto created = root.createDirectory(); created.failed())
        return juce::Result::fail ("The preset folder could not be created: "
                                   + created.getErrorMessage());

    const auto target = replacing == juce::File() ? fileForName (cleanName, category) : replacing;

    if (target == juce::File())
        return juce::Result::fail ("The preset folder could not be created.");

    // Only ever write inside the library: a stale entry from a moved folder must not turn a save
    // into a write somewhere else on disk. The parent is compared rather than tested with
    // isAChildOf(), which does not accept a path that has not been written yet.
    const auto parent = target.getParentDirectory();

    if (parent != root && parent.getParentDirectory() != root)
        return juce::Result::fail ("The preset is outside the preset folder.");

    juce::XmlElement xml (rootTag);
    xml.setAttribute ("format", formatIdentifier);
    xml.setAttribute ("version", formatVersion);
    xml.setAttribute ("name", cleanName);

    patternstate::writeToXml (pattern, selectedLane, xml);

    if (! xml.writeTo (target))
        return juce::Result::fail ("The preset could not be written to " + target.getFullPathName());

    if (savedFile != nullptr)
        *savedFile = target;

    reload();
    return juce::Result::ok();
}

bool Library::load (const Entry& entry, Pattern& pattern, int& selectedLane) const
{
    if (entry.isFactory)
    {
        // A fresh instance as the editor presents it: the default pattern with lane 1 playing.
        Pattern factory;
        factory.lanes[0].settings.enabled = true;

        pattern = factory;
        selectedLane = 0;
        return true;
    }

    const auto xml = juce::XmlDocument::parse (entry.file);

    if (xml == nullptr || ! isPresetElement (*xml))
        return false;

    Pattern loaded;
    auto lane = selectedLane;

    // readFromXml refuses a PATTERN written in a format version this build does not know, and
    // reads a document without a format attribute as version 1.
    if (! patternstate::readFromXml (*xml, loaded, lane))
        return false;

    pattern = loaded;
    selectedLane = juce::jlimit (0, maxLanes - 1, lane);
    return true;
}

juce::Result Library::remove (const Entry& entry)
{
    if (entry.isFactory)
        return juce::Result::fail ("The factory preset cannot be deleted.");

    if (! entry.file.existsAsFile())
        return juce::Result::fail ("The preset file is already gone.");

    if (! entry.file.deleteFile())
        return juce::Result::fail ("The preset could not be deleted.");

    reload();
    return juce::Result::ok();
}

juce::Result Library::rename (const Entry& entry, const juce::String& newName,
                              juce::File* renamedFile)
{
    if (entry.isFactory)
        return juce::Result::fail ("The factory preset cannot be renamed.");

    const auto cleanName = newName.trim();

    if (cleanName.isEmpty())
        return juce::Result::fail ("The preset name is empty.");

    Pattern pattern;
    auto lane = 0;

    if (! load (entry, pattern, lane))
        return juce::Result::fail ("The preset could not be read.");

    // The new document is written before the old one goes, so a failed write cannot lose the
    // pattern. Renaming to the same file name only rewrites its name attribute.
    const auto target = fileForName (cleanName, entry.category);

    if (const auto result = save (pattern, lane, cleanName, target, renamedFile, entry.category);
        result.failed())
        return result;

    if (target != entry.file)
        entry.file.deleteFile();

    reload();
    return juce::Result::ok();
}

juce::Result Library::moveToCategory (const Entry& entry, const juce::String& category,
                                      juce::File* movedFile)
{
    if (entry.isFactory)
        return juce::Result::fail ("The factory preset cannot be moved.");

    const auto destination = category.isEmpty() ? juce::String (defaultCategoryName) : category;

    if (destination == entry.category)
    {
        // Already there: nothing to write, and nothing to remove.
        if (movedFile != nullptr)
            *movedFile = entry.file;

        return juce::Result::ok();
    }

    Pattern pattern;
    auto lane = 0;

    if (! load (entry, pattern, lane))
        return juce::Result::fail ("The preset could not be read.");

    // Written into the destination before the old file goes, so a failed write cannot lose the
    // pattern. The name is kept; only the file name becomes unique when that name is taken there.
    juce::File target;

    if (const auto result = save (pattern, lane, entry.name, {}, &target, destination);
        result.failed())
        return result;

    if (target != entry.file)
        entry.file.deleteFile();

    if (movedFile != nullptr)
        *movedFile = target;

    reload();
    return juce::Result::ok();
}

juce::Result Library::exportPreset (const Entry& entry, const juce::File& target) const
{
    if (entry.isFactory)
        return juce::Result::fail ("The factory preset has no file to export.");

    const auto xml = juce::XmlDocument::parse (entry.file);

    if (xml == nullptr || ! isPresetElement (*xml))
        return juce::Result::fail ("The preset could not be read.");

    if (! xml->writeTo (target))
        return juce::Result::fail ("The preset could not be written to " + target.getFullPathName());

    return juce::Result::ok();
}

juce::Result Library::exportArchive (const juce::File& target, const juce::String& category) const
{
    const auto exportedCategory = category.isEmpty() ? juce::String (defaultCategoryName) : category;

    juce::XmlElement archive (archiveTag);
    archive.setAttribute ("format", archiveFormatIdentifier);
    archive.setAttribute ("version", archiveFormatVersion);
    archive.setAttribute ("name", exportedCategory);

    for (const auto& entry : list)
    {
        if (entry.isFactory || entry.category != exportedCategory)
            continue;

        if (auto xml = juce::XmlDocument::parse (entry.file))
            if (isPresetElement (*xml))
                archive.addChildElement (xml.release());
    }

    if (archive.getNumChildElements() == 0)
        return juce::Result::fail ("\"" + exportedCategory + "\" has no presets to export.");

    if (! archive.writeTo (target))
        return juce::Result::fail ("The archive could not be written to " + target.getFullPathName());

    return juce::Result::ok();
}

Library::ImportSummary Library::importFrom (const juce::Array<juce::File>& sources,
                                             ImportPolicy policy,
                                             const juce::String& fallbackCategory)
{
    ImportSummary summary;

    for (const auto& source : sources)
    {
        const auto xml = juce::XmlDocument::parse (source);

        if (xml == nullptr)
        {
            summary.failures.add (source.getFileName() + ": not a readable document");
            continue;
        }

        if (xml->hasTagName (archiveTag))
        {
            const auto archive = xml->getStringAttribute ("format") == archiveFormatIdentifier
                                     && xml->getIntAttribute ("version", 0) == archiveFormatVersion;

            if (! archive)
            {
                summary.failures.add (source.getFileName() + ": not a KINETIC category archive");
                continue;
            }

            // An archive restores the folder it came from: the category it is named after, which
            // is created when this machine does not have it yet.
            const auto category = sanitiseName (xml->getStringAttribute ("name"),
                                                defaultCategoryName);

            for (const auto* child : xml->getChildIterator())
            {
                if (child == nullptr || ! child->hasTagName (rootTag))
                    continue;

                auto imported = false;

                if (const auto result = importElement (*child, policy, category, imported);
                    result.failed())
                    summary.failures.add (source.getFileName() + ": " + result.getErrorMessage());
                else if (imported)
                    ++summary.imported;
                else
                    ++summary.skipped;
            }

            continue;
        }

        // A single preset belongs to the category the editor is showing.
        const auto category = fallbackCategory.isEmpty() ? juce::String (defaultCategoryName)
                                                        : fallbackCategory;
        auto imported = false;

        if (const auto result = importElement (*xml, policy, category, imported); result.failed())
            summary.failures.add (source.getFileName() + ": " + result.getErrorMessage());
        else if (imported)
            ++summary.imported;
        else
            ++summary.skipped;
    }

    return summary;
}

juce::Result Library::importElement (const juce::XmlElement& xml, ImportPolicy policy,
                                     const juce::String& category, bool& imported)
{
    imported = false;

    if (! isPresetElement (xml))
        return juce::Result::fail ("not a KINETIC preset");

    Pattern pattern;
    auto lane = 0;

    if (! patternstate::readFromXml (xml, pattern, lane))
        return juce::Result::fail ("its pattern is in a format this build does not read");

    auto name = xml.getStringAttribute ("name").trim();

    if (name.isEmpty())
        name = "Imported";

    if (const auto existing = entryNamed (name, category); existing >= 0)
    {
        switch (policy)
        {
            case ImportPolicy::skip:
                return juce::Result::ok();

            case ImportPolicy::overwrite:
            {
                // The preset keeps its name and its file - only the pattern is replaced, so a
                // project that refers to it by file name follows the new content.
                const auto existingName = list[existing].name;
                const auto existingFile = list[existing].file;

                if (const auto result = save (pattern, lane, existingName, existingFile, nullptr,
                                              category);
                    result.failed())
                    return result;

                imported = true;
                return juce::Result::ok();
            }

            case ImportPolicy::keepBoth:
            default:
                name = versionedName (name, category);
                break;
        }
    }

    if (const auto result = save (pattern, lane, name, {}, nullptr, category); result.failed())
        return result;

    imported = true;
    return juce::Result::ok();
}

int Library::entryNamed (const juce::String& name, const juce::String& category) const
{
    for (int i = 0; i < list.size(); ++i)
        if (! list[i].isFactory && list[i].category == category
             && list[i].name.equalsIgnoreCase (name))
            return i;

    return -1;
}

bool Library::nameExists (const juce::String& name, const juce::String& category) const
{
    return entryNamed (name, category) >= 0;
}

juce::String Library::versionedName (const juce::String& name, const juce::String& category) const
{
    auto candidate = name + " version";
    auto suffix = 2;

    while (nameExists (candidate, category))
    {
        candidate = name + " version " + juce::String (suffix);
        ++suffix;
    }

    return candidate;
}

juce::File Library::defaultDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Application Support")
             .getChildFile ("KINETIC")
             .getChildFile ("Presets");
}

juce::String Library::sanitiseName (const juce::String& name, const juce::String& fallback)
{
    auto result = name.trim();

    result = result.replaceCharacter ('/', '-')
                     .replaceCharacter ('\\', '-')
                     .replaceCharacter (':', '-')
                     .replaceCharacter ('*', '-')
                     .replaceCharacter ('?', '-');

    while (result.startsWithChar ('.'))
        result = result.substring (1);

    result = result.substring (0, 80).trim();

    return result.isEmpty() ? fallback : result;
}

juce::File Library::fileForName (const juce::String& name, const juce::String& category) const
{
    const auto base = sanitiseName (name);
    const auto directory = categoryDirectory (category);

    if (const auto created = directory.createDirectory(); created.failed())
        return {};

    auto candidate = directory.getChildFile (base).withFileExtension (fileExtension);
    auto suffix = 2;

    while (candidate.exists())
    {
        candidate = directory.getChildFile (base + " " + juce::String (suffix))
                          .withFileExtension (fileExtension);
        ++suffix;
    }

    return candidate;
}

} // namespace kinetic::presets
