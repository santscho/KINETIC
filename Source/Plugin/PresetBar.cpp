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

juce::String PresetBar::currentCategory() const
{
    const auto entries = processor.getPresetEntries();
    const auto index = currentIndexOrFactory();

    if (juce::isPositiveAndBelow (index, entries.size()) && ! entries[index].isFactory)
        return entries[index].category;

    return presets::Library::defaultCategoryName;
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
    const auto factorySelected = ! juce::isPositiveAndBelow (selected, entries.size())
                                    || entries[selected].isFactory;

    juce::PopupMenu menu;
    juce::String lastHeader;

    for (int i = 0; i < entries.size(); ++i)
    {
        // Presets are read by category, so a header per folder keeps a long list navigable.
        const auto header = entries[i].isFactory ? juce::String ("FACTORY") : entries[i].category;

        if (header != lastHeader)
        {
            lastHeader = header;
            menu.addSectionHeader (header);
        }

        menu.addItem (i + 1, entries[i].name, true, i == selected);
    }

    menu.addSeparator();

    const auto categories = processor.getCategories();

    // The import policy is chosen up front rather than asked after the file picker, so what will
    // happen to a name that is already in the category is never a surprise.
    juce::PopupMenu importMenu;
    importMenu.addItem (1010, "Keep Both Presets");
    importMenu.addItem (1011, "Replace Existing Presets");
    importMenu.addItem (1012, "Skip Existing Presets");
    menu.addSubMenu ("Import presets...", importMenu);

    // Moving is offered for every category but the one the preset is already in, so the submenu
    // is a list of destinations rather than a list with one entry that does nothing.
    const auto shownCategory = currentCategory();
    juce::PopupMenu moveMenu;

    for (int i = 0; i < categories.size(); ++i)
        if (categories[i] != shownCategory)
            moveMenu.addItem (3001 + i, categories[i]);

    menu.addSubMenu ("Move to...", moveMenu,
                     ! factorySelected && moveMenu.getNumItems() > 0);

    juce::PopupMenu exportMenu;

    for (int i = 0; i < categories.size(); ++i)
        exportMenu.addItem (2001 + i, categories[i]);

    menu.addItem (1002, "Export this preset...", ! factorySelected);
    menu.addSubMenu ("Export category...", exportMenu);
    menu.addItem (1004, "New category...");
    menu.addItem (1000, "Reveal folder in Finder");

    juce::Component::SafePointer<PresetBar> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton)
                            .withParentComponent (getTopLevelComponent()),
                        [safeThis, categories] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            if (result == 1000)
                            {
                                safeThis->processor.getPresetFolder().revealToUser();
                                return;
                            }

                            if (result == 1004)
                            {
                                safeThis->promptNewCategory();
                                return;
                            }

                            if (result >= 1010 && result <= 1012)
                            {
                                safeThis->importPresets (
                                    result == 1011 ? presets::Library::ImportPolicy::overwrite
                                  : result == 1012 ? presets::Library::ImportPolicy::skip
                                                   : presets::Library::ImportPolicy::keepBoth);
                                return;
                            }

                            if (result >= 2001 && result <= 2000 + categories.size())
                            {
                                safeThis->exportCategory (categories[result - 2001]);
                                return;
                            }

                            if (result >= 3001 && result <= 3000 + categories.size())
                            {
                                safeThis->moveToCategory (categories[result - 3001]);
                                return;
                            }

                            if (result == 1002)
                            {
                                safeThis->exportCurrentPreset();
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
    const auto result = processor.savePreset (entries[index].name, index, entries[index].category,
                                             &program);

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
    const auto categories = processor.getCategories();
    const auto category = currentCategory();

    const auto suggested = juce::isPositiveAndBelow (index, entries.size())
                                   && ! entries[index].isFactory
                               ? entries[index].name
                               : juce::String ("My Pattern");

    auto* alert = new juce::AlertWindow ("Save preset",
                                         "Name and category for this preset. It is written to "
                                             + processor.getPresetFolder().getFullPathName(),
                                         juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor ("name", suggested, "Name");
    alert->addComboBox ("category", categories, "Category");

    if (auto* box = alert->getComboBoxComponent ("category"))
        box->setSelectedItemIndex (juce::jmax (0, categories.indexOf (category)),
                                   juce::dontSendNotification);

    alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::AlertWindow> alertRef (alert);
    juce::Component::SafePointer<PresetBar> safeThis (this);

    alert->enterModalState (true, juce::ModalCallbackFunction::create (
                                [safeThis, alertRef, categories, category] (int result)
                                {
                                    if (safeThis == nullptr || alertRef == nullptr || result != 1)
                                        return;

                                    auto chosenCategory = category;

                                    if (auto* box = alertRef->getComboBoxComponent ("category"))
                                        if (juce::isPositiveAndBelow (box->getSelectedItemIndex(),
                                                                      categories.size()))
                                            chosenCategory = categories[box->getSelectedItemIndex()];

                                    safeThis->saveAs (alertRef->getTextEditorContents ("name"),
                                                      chosenCategory);
                                }), true);
}

void PresetBar::promptNewCategory()
{
    auto* alert = new juce::AlertWindow ("New category",
                                         "Name for a new preset category. It becomes a folder in "
                                             + processor.getPresetFolder().getFullPathName()
                                             + ", so presets can be moved one category at a time.",
                                         juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor ("name", "Category", "Name");
    alert->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::AlertWindow> alertRef (alert);
    juce::Component::SafePointer<PresetBar> safeThis (this);

    alert->enterModalState (true, juce::ModalCallbackFunction::create (
                                [safeThis, alertRef] (int result)
                                {
                                    if (safeThis == nullptr || alertRef == nullptr || result != 1)
                                        return;

                                    if (const auto created = safeThis->processor.createCategory (
                                            alertRef->getTextEditorContents ("name"));
                                        created.failed())
                                    {
                                        safeThis->showError (created.getErrorMessage());
                                        return;
                                    }

                                    safeThis->reloadLibrary();
                                }), true);
}

void PresetBar::moveToCategory (const juce::String& category)
{
    if (const auto moved = processor.movePreset (currentIndexOrFactory(), category); moved.failed())
    {
        showError (moved.getErrorMessage());
        return;
    }

    // It is still the same preset, so the pattern and the modified marker stay as they are; only
    // its place in the list changed. The processor has already moved the program with it, which
    // is also what the session stores.
    currentIndex = processor.getCurrentProgram();
    refreshName();
    repaint();
}

void PresetBar::saveAs (const juce::String& name, const juce::String& category)
{
    int program = 0;
    const auto result = processor.savePreset (name, -1, category, &program);

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

void PresetBar::importPresets (presets::Library::ImportPolicy policy)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import presets",
        processor.getPresetFolder().getParentDirectory(),
        juce::String ("*.") + presets::Library::fileExtension + ";*."
            + presets::Library::archiveExtension);

    juce::Component::SafePointer<PresetBar> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safeThis, policy] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  const auto files = chooser.getResults();

                                  if (files.isEmpty())
                                      return;

                                  const auto summary = safeThis->processor.importPresets (
                                      files, policy, safeThis->currentCategory());
                                  safeThis->reloadLibrary();

                                  juce::String message;
                                  message << "Imported " << summary.imported
                                          << (summary.imported == 1 ? " preset" : " presets");

                                  if (summary.skipped > 0)
                                      message << ", skipped " << summary.skipped;

                                  message << ".";

                                  if (! summary.failures.isEmpty())
                                      message << "\n\nNot imported:\n"
                                              << summary.failures.joinIntoString ("\n");

                                  safeThis->showInfo ("Import presets", message);
                              });
}

void PresetBar::exportCurrentPreset()
{
    const auto entries = processor.getPresetEntries();
    const auto index = currentIndexOrFactory();

    if (! juce::isPositiveAndBelow (index, entries.size()) || entries[index].isFactory)
        return;

    fileChooser = std::make_unique<juce::FileChooser> (
        "Export preset",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
            .getChildFile (entries[index].name)
            .withFileExtension (presets::Library::fileExtension),
        juce::String ("*.") + presets::Library::fileExtension);

    juce::Component::SafePointer<PresetBar> safeThis (this);
    const auto presetIndex = index;

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis, presetIndex] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  const auto target = chooser.getResult();

                                  if (target == juce::File())
                                      return;

                                  if (const auto result = safeThis->processor.exportPreset (presetIndex,
                                                                                            target);
                                      result.failed())
                                      safeThis->showError (result.getErrorMessage());
                              });
}

void PresetBar::exportCategory (const juce::String& category)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Export category",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
            .getChildFile (category)
            .withFileExtension (presets::Library::archiveExtension),
        juce::String ("*.") + presets::Library::archiveExtension);

    juce::Component::SafePointer<PresetBar> safeThis (this);
    const auto exportedCategory = category;

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis, exportedCategory] (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  const auto target = chooser.getResult();

                                  if (target == juce::File())
                                      return;

                                  if (const auto result = safeThis->processor.exportCategory (
                                          exportedCategory, target);
                                      result.failed())
                                      safeThis->showError (result.getErrorMessage());
                              });
}

void PresetBar::showInfo (const juce::String& title, const juce::String& message) const
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (const_cast<PresetBar*> (this)),
                                  nullptr);
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
