#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace kinetic
{

/**
    The preset browser in the editor header.

    The name button opens the list of every preset in ~/Library/Application Support/KINETIC/Presets
    with the factory reset at the top; the arrows step through the same list and wrap.

    The bar holds no state of its own beyond which program it is showing: loading, saving,
    renaming and deleting all go through the processor, which owns the library and the program
    index the host sees. That is what keeps the header, the host's program list and the pattern
    in the session from drifting apart - a program the host selects is picked up here on the next
    timer tick, and a preset saved here becomes the current program.

    A `*` after the name means the pattern has been edited since it was loaded or saved; it is
    driven by SharedPattern's publish counter, so every edit - from this bar, the grid or the
    host restoring a state - reaches it without the widgets having to report anything.
*/
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (KineticAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called after a preset has been applied, so the editor can re-read the pattern at once. */
    std::function<void()> onPresetLoaded;

    /** Re-reads the preset folder and follows whatever program the processor now has. */
    void reloadLibrary();

    /** Follows a program the host selected, and shows the modified marker after an edit. */
    void refreshDirtyState();

private:
    void applyEntry (int index);
    void step (int offset);
    void showMenu();
    void saveCurrent();
    void promptSaveAs();
    void saveAs (const juce::String& name);
    void confirmDelete();
    void importPresets (presets::Library::ImportPolicy policy);
    void exportCurrentPreset();
    void exportFolder();
    void showError (const juce::String& message) const;
    void showInfo (const juce::String& title, const juce::String& message) const;

    void adoptProgram (int program);
    void setDirty (bool shouldBeDirty);
    void refreshName();
    int currentIndexOrFactory() const;

    KineticAudioProcessor& processor;

    int currentIndex = 0;
    juce::uint32 savedRevision = 0;
    bool dirty = false;

    juce::TextButton previousButton { "<" }, nextButton { ">" };
    juce::TextButton nameButton;
    juce::TextButton saveButton { "SAVE" }, saveAsButton { "SAVE AS" }, deleteButton { "DEL" };

    juce::Rectangle<int> labelArea;

    // The choosers run asynchronously, so one has to stay alive until it calls back.
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace kinetic
