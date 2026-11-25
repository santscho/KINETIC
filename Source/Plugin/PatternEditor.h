#pragma once

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
    explicit PatternEditor (SharedPattern& sharedPattern);

    void resized() override;

private:
    Lane& currentLane() noexcept { return shared.edit().lanes[static_cast<std::size_t> (laneIndex)]; }

    void selectLane (int index);
    void refreshControls();
    void commit();

    SharedPattern& shared;
    StepGrid grid;

    juce::OwnedArray<juce::TextButton> laneButtons;
    juce::TextButton enableButton;
    juce::Slider lengthSlider;
    juce::ComboBox dividerBox, modifierBox, directionBox;

    int laneIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternEditor)
};

} // namespace kinetic
