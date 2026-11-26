#include "PresetLibrary.h"

#include <algorithm>

namespace kinetic::presets
{

namespace
{
    constexpr const char* rootTag = "KINETIC_PRESET";
    constexpr const char* formatIdentifier = "kinetic.preset";
    constexpr int formatVersion = 1;

    /** The document a preset file holds: format, version, name, then the PATTERN element. */
    bool isPresetElement (const juce::XmlElement& xml) noexcept
    {
        return xml.hasTagName (rootTag)
                && xml.getStringAttribute ("format") == formatIdentifier
                && xml.getIntAttribute ("version", 0) == formatVersion;
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
    list.clearQuick();

    Entry factory;
    factory.name = factoryName;
    factory.isFactory = true;
    list.add (factory);

    if (root.isDirectory())
        for (const auto& file : root.findChildFiles (juce::File::findFiles, false,
                                                     juce::String ("*.") + fileExtension))
        {
            const auto name = readName (file);

            if (name.isEmpty())
                continue;       // not a KINETIC preset, so not a broken entry either

            Entry entry;
            entry.name = name;
            entry.file = file;
            list.add (entry);
        }

    // The factory reset stays first; the rest read alphabetically, whatever the files are called.
    std::sort (list.begin(), list.end(), [] (const Entry& a, const Entry& b)
    {
        if (a.isFactory != b.isFactory)
            return a.isFactory;

        return a.name.compareIgnoreCase (b.name) < 0;
    });
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

juce::String Library::readName (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! isPresetElement (*xml))
        return {};

    return xml->getStringAttribute ("name").trim();
}

juce::Result Library::save (const Pattern& pattern, int selectedLane, const juce::String& name,
                            const juce::File& replacing, juce::File* savedFile)
{
    const auto cleanName = name.trim();

    if (cleanName.isEmpty())
        return juce::Result::fail ("The preset name is empty.");

    if (const auto created = root.createDirectory(); created.failed())
        return juce::Result::fail ("The preset folder could not be created: "
                                   + created.getErrorMessage());

    const auto target = replacing == juce::File() ? fileForName (cleanName) : replacing;

    // Only ever write a direct child of the library: a stale entry from a moved folder must not
    // turn a save into a write somewhere else on disk. The parent is compared rather than tested
    // with isAChildOf(), which does not accept a path that has not been written yet.
    if (target.getParentDirectory() != root)
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
    const auto target = fileForName (cleanName);

    if (const auto result = save (pattern, lane, cleanName, target, renamedFile); result.failed())
        return result;

    if (target != entry.file)
        entry.file.deleteFile();

    reload();
    return juce::Result::ok();
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

juce::File Library::fileForName (const juce::String& name) const
{
    const auto base = sanitiseName (name);
    auto candidate = root.getChildFile (base).withFileExtension (fileExtension);
    auto suffix = 2;

    while (candidate.exists())
    {
        candidate = root.getChildFile (base + " " + juce::String (suffix))
                        .withFileExtension (fileExtension);
        ++suffix;
    }

    return candidate;
}

} // namespace kinetic::presets
