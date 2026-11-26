#include "PresetBar.h"

#include "Theme.h"

namespace kinetic
{

namespace
{
    void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, theme::raised);
        button.setColour (juce::TextButton::textColourOffId, theme::text);
    }
}

PresetBar::PresetBar (SharedPattern& sharedPattern, std::atomic<int>& selectedLaneRef)
    : shared (sharedPattern), selectedLane (selectedLaneRef)
{
    previousButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    nameButton.setTooltip ("Click for the preset list; * means the pattern changed since it was "
                           "loaded");
    saveButton.setTooltip ("Overwrite this preset's file with the pattern as it is now");
    saveAsButton.setTooltip ("Write the pattern to a new preset file");
    deleteButton.setTooltip ("Delete this preset's file");

    for (auto* button : { &previousButton, &nextButton, &nameButton,
                          &saveButton, &saveAsButton, &deleteButton })
    {
        styleButton (*button);
        addAndMakeVisible (button);
    }

    nameButton.setColour (juce::TextButton::textColourOffId, theme::accent);

    previousButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    nameButton.onClick = [this] { showMenu(); };
    saveButton.onClick = [this] { saveCurrent(); };
    saveAsButton.onClick = [this] { promptSaveAs(); };
    deleteButton.onClick = [this] { confirmDelete(); };

    // Whatever the session already holds counts as the starting point, so opening the editor on a
    // pattern the host restored does not immediately look unsaved.
    savedRevision = shared.revision();
    refreshName();
}

void PresetBar::paint (juce::Graphics& g)
{
    g.setColour (theme::muted);
    g.setFont (theme::mono (9.0f));
    g.drawText ("PRESET", labelArea, juce::Justification::centredLeft, false);
}

void PresetBar::resized()
{
    auto row = getLocalBounds();

    labelArea = row.removeFromLeft (54);

    previousButton.setBounds (row.removeFromLeft (22).reduced (0, 2));
    row.removeFromLeft (1);
    nextButton.setBounds (row.removeFromLeft (22).reduced (0, 2));
    row.removeFromLeft (6);

    deleteButton.setBounds (row.removeFromRight (42).reduced (0, 2));
    row.removeFromRight (4);
    saveAsButton.setBounds (row.removeFromRight (64).reduced (0, 2));
    row.removeFromRight (4);
    saveButton.setBounds (row.removeFromRight (52).reduced (0, 2));
    row.removeFromRight (6);

    nameButton.setBounds (row.reduced (0, 2));
}

void PresetBar::reloadLibrary()
{
    const auto kept = library.entries()[currentIndexOrFactory()].file;

    library.reload();
    currentIndex = indexOfFile (kept);
    refreshName();
    repaint();
}

void PresetBar::refreshDirtyState()
{
    if (shared.revision() != savedRevision)
        setDirty (true);
}

void PresetBar::markModified()
{
    setDirty (true);
}

void PresetBar::setDirty (bool shouldBeDirty)
{
    if (dirty == shouldBeDirty)
        return;

    dirty = shouldBeDirty;
    refreshName();
    repaint();
}

int PresetBar::currentIndexOrFactory() const
{
    return juce::jlimit (0, juce::jmax (0, library.entries().size() - 1), currentIndex);
}

int PresetBar::indexOfFile (const juce::File& file) const
{
    if (file == juce::File())
        return 0;

    const auto& entries = library.entries();

    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].file == file)
            return i;

    return 0;
}

void PresetBar::refreshName()
{
    const auto& entries = library.entries();
    const auto index = currentIndexOrFactory();
    const auto factory = index >= entries.size() || entries[index].isFactory;

    auto name = index < entries.size() ? entries[index].name
                                       : juce::String (presets::Library::factoryName);

    nameButton.setButtonText (dirty ? name + "  *" : name);
    saveButton.setEnabled (! factory);
    deleteButton.setEnabled (! factory);
}

void PresetBar::applyEntry (int index)
{
    const auto& entries = library.entries();

    if (! juce::isPositiveAndBelow (index, entries.size()))
        return;

    Pattern loaded;
    auto lane = selectedLane.load();

    if (! library.load (entries[index], loaded, lane))
    {
        showError ("That file is not a preset this build can read.");
        return;
    }

    shared.edit() = loaded;
    selectedLane.store (juce::jlimit (0, maxLanes - 1, lane));
    shared.publish();

    currentIndex = index;
    savedRevision = shared.revision();
    setDirty (false);
    refreshName();
    repaint();

    if (onPresetLoaded != nullptr)
        onPresetLoaded();
}

void PresetBar::step (int offset)
{
    const auto count = library.entries().size();

    if (count <= 0)
        return;

    const auto current = currentIndexOrFactory();
    applyEntry (((current + offset) % count + count) % count);
}

void PresetBar::showMenu()
{
    // The folder can change behind the plug-in's back; opening the list is the moment to look.
    reloadLibrary();

    const auto& entries = library.entries();
    const auto selected = currentIndexOrFactory();

    juce::PopupMenu menu;

    for (int i = 0; i < entries.size(); ++i)
        menu.addItem (i + 1, entries[i].name, true, i == selected);

    menu.addSeparator();
    menu.addItem (1000, "Reveal folder in Finder");

    juce::Component::SafePointer<PresetBar> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton)
                            .withParentComponent (getTopLevelComponent()),
                        [safeThis] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            if (result == 1000)
                            {
                                safeThis->library.directory().revealToUser();
                                return;
                            }

                            safeThis->applyEntry (result - 1);
                        });
}

void PresetBar::saveCurrent()
{
    const auto index = currentIndexOrFactory();
    const auto& entries = library.entries();

    if (index >= entries.size() || entries[index].isFactory)
    {
        promptSaveAs();
        return;
    }

    // Copy before saving: the library reloads its list, which replaces this entry.
    const auto name = entries[index].name;
    const auto file = entries[index].file;

    const auto result = library.save (shared.edit(), selectedLane.load(), name, file);

    if (result.failed())
    {
        showError (result.getErrorMessage());
        return;
    }

    currentIndex = indexOfFile (file);
    savedRevision = shared.revision();
    setDirty (false);
    refreshName();
    repaint();
}

void PresetBar::promptSaveAs()
{
    const auto index = currentIndexOrFactory();
    const auto& entries = library.entries();
    const auto suggested = index < entries.size() && ! entries[index].isFactory
                               ? entries[index].name
                               : juce::String ("My Pattern");

    auto* alert = new juce::AlertWindow ("Save preset",
                                         "Name for this preset. It is written to "
                                             + library.directory().getFullPathName(),
                                         juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor ("name", suggested, "Name");
    alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::AlertWindow> alertRef (alert);
    juce::Component::SafePointer<PresetBar> safeThis (this);

    alert->enterModalState (true, juce::ModalCallbackFunction::create (
                                [safeThis, alertRef] (int result)
                                {
                                    if (safeThis == nullptr || alertRef == nullptr || result != 1)
                                        return;

                                    safeThis->saveAs (alertRef->getTextEditorContents ("name"));
                                }), true);
}

void PresetBar::saveAs (const juce::String& name)
{
    juce::File saved;
    const auto result = library.save (shared.edit(), selectedLane.load(), name, {}, &saved);

    if (result.failed())
    {
        showError (result.getErrorMessage());
        return;
    }

    currentIndex = indexOfFile (saved);
    savedRevision = shared.revision();
    setDirty (false);
    refreshName();
    repaint();
}

void PresetBar::confirmDelete()
{
    const auto index = currentIndexOrFactory();
    const auto& entries = library.entries();

    if (index >= entries.size() || entries[index].isFactory)
        return;

    const auto name = entries[index].name;
    const auto file = entries[index].file;

    auto* alert = new juce::AlertWindow ("Delete preset",
                                         "Delete \"" + name + "\"? The file is removed from disk.",
                                         juce::MessageBoxIconType::WarningIcon);
    alert->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBar> safeThis (this);

    alert->enterModalState (true, juce::ModalCallbackFunction::create (
                                [safeThis, file, name] (int result)
                                {
                                    if (safeThis == nullptr || result != 1)
                                        return;

                                    presets::Entry entry;
                                    entry.name = name;
                                    entry.file = file;

                                    if (const auto removal = safeThis->library.remove (entry);
                                        removal.failed())
                                    {
                                        safeThis->showError (removal.getErrorMessage());
                                        return;
                                    }

                                    // The pattern itself is left alone: the file is gone, but
                                    // nothing the player was working on is discarded.
                                    safeThis->currentIndex = 0;
                                    safeThis->setDirty (true);
                                    safeThis->refreshName();
                                    safeThis->repaint();
                                }), true);
}

void PresetBar::showError (const juce::String& message) const
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Presets")
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (const_cast<PresetBar*> (this)),
                                  nullptr);
}

} // namespace kinetic
