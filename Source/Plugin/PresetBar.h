#pragma once

#include <atomic>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Preset/PresetLibrary.h"
#include "Core/Sequencer/SharedPattern.h"

namespace kinetic
{

/**
    The preset browser in the editor header.

    The name button opens the list of every preset in ~/Library/Application Support/KINETIC/Presets
    with the factory reset at the top; the arrows step through the same list and wrap. Loading a
    preset writes it into the editor's pattern and publishes it like any other edit, so the audio
    thread picks it up at its next block.

    Save, save as and delete work on files only. The host session still stores the pattern as it
    always did, so a preset is a starting point rather than something the session depends on.

    A `*` after the name means the pattern has been edited since it was loaded or saved; it is
    driven by SharedPattern's publish counter, so every edit - from this bar, the grid or the
    host restoring a state - reaches it without the widgets having to report anything.
*/
class PresetBar final : public juce::Component
{
public:
    PresetBar (SharedPattern& sharedPattern, std::atomic<int>& selectedLane);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called after a preset has been applied and published, so the editor can re-read it. */
    std::function<void()> onPresetLoaded;

    /** Re-reads the preset folder, keeping the current preset when its file still exists. */
    void reloadLibrary();

    /** Shows the modified marker once the pattern has been published since it was loaded. */
    void refreshDirtyState();

    /** Marks the current preset modified, for changes made outside this bar (a host state load). */
    void markModified();

private:
    void applyEntry (int index);
    void step (int offset);
    void showMenu();
    void saveCurrent();
    void promptSaveAs();
    void saveAs (const juce::String& name);
    void confirmDelete();
    void showError (const juce::String& message) const;

    void setDirty (bool shouldBeDirty);
    void refreshName();
    int currentIndexOrFactory() const;
    int indexOfFile (const juce::File& file) const;

    SharedPattern& shared;
    std::atomic<int>& selectedLane;
    presets::Library library;

    int currentIndex = 0;
    juce::uint32 savedRevision = 0;
    bool dirty = false;

    juce::TextButton previousButton { "<" }, nextButton { ">" };
    juce::TextButton nameButton;
    juce::TextButton saveButton { "SAVE" }, saveAsButton { "SAVE AS" }, deleteButton { "DEL" };

    juce::Rectangle<int> labelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace kinetic
