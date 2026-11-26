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

PresetBar::PresetBar (KineticAudioProcessor& processorRef)
    : processor (processorRef)
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

    // The folder may have gained or lost files while no editor was open, and the host's program
    // list is built from it: opening the editor is the moment to bring both up to date.
    processor.refreshPresetList();

    // Whatever the session already holds counts as the starting point, so opening the editor on a
    // pattern the host restored does not immediately look unsaved.
    savedRevision = processor.getSharedPattern().revision();
    currentIndex = processor.getCurrentProgram();
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
    processor.refreshPresetList();
    currentIndex = processor.getCurrentProgram();
    refreshName();
    repaint();
}

void PresetBar::refreshDirtyState()
{
    const auto program = processor.getCurrentProgram();

    if (program != currentIndex)
    {
        // The host moved its program selector: follow it, and take the pattern it just loaded as
        // the saved state rather than marking it modified.
        adoptProgram (program);
        return;
    }

    if (processor.getSharedPattern().revision() != savedRevision)
        setDirty (true);
}

void PresetBar::adoptProgram (int program)
{
    currentIndex = program;
    savedRevision = processor.getSharedPattern().revision();
    setDirty (false);
    refreshName();
    repaint();
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
    return juce::jlimit (0, juce::jmax (0, processor.getPresetEntries().size() - 1), currentIndex);
}

void PresetBar::refreshName()
{
    const auto entries = processor.getPresetEntries();
    const auto index = currentIndexOrFactory();
    const auto factory = ! juce::isPositiveAndBelow (index, entries.size())
                            || entries[index].isFactory;

    auto name = juce::isPositiveAndBelow (index, entries.size())
                    ? entries[index].name
                    : juce::String (presets::Library::factoryName);

    nameButton.setButtonText (dirty ? name + "  *" : name);
    saveButton.setEnabled (! factory);
    deleteButton.setEnabled (! factory);
}

void PresetBar::applyEntry (int index)
{
    if (! processor.applyPreset (index))
    {
        showError ("That preset could not be read. Its file may have been changed or removed.");
        return;
    }

    adoptProgram (processor.getCurrentProgram());

    if (onPresetLoaded != nullptr)
        onPresetLoaded();
}

void PresetBar::step (int offset)
{
    const auto count = processor.getPresetEntries().size();

    if (count <= 0)
        return;

    const auto current = currentIndexOrFactory();
    applyEntry (((current + offset) % count + count) % count);
}

void PresetBar::showMenu()
{
    // The folder can change behind the plug-in's back; opening the list is the moment to look.
    reloadLibrary();

    const auto entries = processor.getPresetEntries();
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
                                safeThis->processor.getPresetFolder().revealToUser();
                                return;
                            }

                            safeThis->applyEntry (result - 1);
                        });
}

void PresetBar::saveCurrent()
{
    const auto entries = processor.getPresetEntries();
    const auto index = currentIndexOrFactory();

    if (! juce::isPositiveAndBelow (index, entries.size()) || entries[index].isFactory)
    {
        promptSaveAs();
        return;
    }

    int program = 0;
    const auto result = processor.savePreset (entries[index].name, index, &program);

    if (result.failed())
    {
        showError (result.getErrorMessage());
        return;
    }

    adoptProgram (program);
}

void PresetBar::promptSaveAs()
{
    const auto entries = processor.getPresetEntries();
    const auto index = currentIndexOrFactory();
    const auto suggested = juce::isPositiveAndBelow (index, entries.size())
                                   && ! entries[index].isFactory
                               ? entries[index].name
                               : juce::String ("My Pattern");

    auto* alert = new juce::AlertWindow ("Save preset",
                                         "Name for this preset. It is written to "
                                             + processor.getPresetFolder().getFullPathName(),
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
    int program = 0;
    const auto result = processor.savePreset (name, -1, &program);

    if (result.failed())
    {
        showError (result.getErrorMessage());
        return;
    }

    adoptProgram (program);
}

void PresetBar::confirmDelete()
{
    const auto entries = processor.getPresetEntries();
    const auto index = currentIndexOrFactory();

    if (! juce::isPositiveAndBelow (index, entries.size()) || entries[index].isFactory)
        return;

    const auto name = entries[index].name;

    auto* alert = new juce::AlertWindow ("Delete preset",
                                         "Delete \"" + name + "\"? The file is removed from disk.",
                                         juce::MessageBoxIconType::WarningIcon);
    alert->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBar> safeThis (this);

    alert->enterModalState (true, juce::ModalCallbackFunction::create (
                                [safeThis, index] (int result)
                                {
                                    if (safeThis == nullptr || result != 1)
                                        return;

                                    if (const auto removal = safeThis->processor.deletePreset (index);
                                        removal.failed())
                                    {
                                        safeThis->showError (removal.getErrorMessage());
                                        return;
                                    }

                                    // The pattern itself is left alone: the file is gone, but
                                    // nothing the player was working on is discarded.
                                    safeThis->currentIndex = safeThis->processor.getCurrentProgram();
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
