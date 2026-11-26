#pragma once

#include <atomic>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Sequencer/SharedPattern.h"
#include "StepGrid.h"

namespace kinetic
{

/**
    Step 4 editor panel: lane selection, the per-lane clock and direction controls, and the
    step grid.

    Every widget writes into SharedPattern's editor copy and publishes; nothing here ever
    touches the pattern the audio thread is reading, and no timer generates MIDI.
*/
class PatternEditor final : public juce::Component
{
public:
    PatternEditor (SharedPattern& sharedPattern, std::atomic<int>& selectedLane);

    void resized() override;

    /** Re-reads the pattern after the host has loaded a state. */
    void refreshAll();

private:
    /**
        Root note of the lane: a list of the twelve notes from C1 to B1 picks the note, and the
        buttons on its left move the note's octave, which the readout beside the list shows.
    */
    class RootNoteSelector final : public juce::Component
    {
    public:
        RootNoteSelector();

        void paint (juce::Graphics&) override;
        void resized() override;

        std::function<void (int)> onNoteChange;

        /** Sets the note without notifying. The list always shows C1 to B1. */
        void setNote (int newNote);

    private:
        void shiftOctave (int direction);
        void rebuildItems();

        juce::TextButton downButton { "-" }, upButton { "+" };
        juce::ComboBox noteBox;
        juce::Rectangle<int> octaveArea;
        int note = 60;
    };

    Lane& currentLane() noexcept { return shared.edit().lanes[static_cast<std::size_t> (laneIndex)]; }

    void selectLane (int index);
    void refreshControls();
    void commit();

    SharedPattern& shared;
    std::atomic<int>& selectedLane;
    StepGrid grid;

    juce::OwnedArray<juce::TextButton> laneButtons;
    juce::TextButton enableButton;
    juce::Slider lengthSlider;
    juce::ComboBox dividerBox, modifierBox, directionBox;
    RootNoteSelector rootNoteSelector;

    juce::TextButton scaleButton;
    juce::ComboBox keyBox, scaleBox;

    int laneIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternEditor)
};

} // namespace kinetic
