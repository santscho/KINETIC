#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Sequencer/SharedPattern.h"

namespace kinetic
{

/**
    The step grid: one column per step, seven rows of parameters.

    Trigger and slide cells toggle; the value rows map the pointer's height inside the row to
    the value, so a click and a drag both read the same way and a whole row can be painted with
    one gesture. Every change is published immediately, so the audio thread picks it up at its
    next block boundary - the grid never touches the pattern the sequencer is reading.
*/
class StepGrid final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit StepGrid (SharedPattern& sharedPattern);

    void setLane (int laneIndex);
    void refresh() { repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    enum Row { trigger, pitch, velocity, mod, gate, ratchet, slide, numRows };

    static juce::String rowLabel (int row);

    Lane& currentLane() noexcept { return shared.edit().lanes[static_cast<std::size_t> (laneIndex)]; }
    const Lane& currentLane() const noexcept { return shared.edit().lanes[static_cast<std::size_t> (laneIndex)]; }

    int numColumns() const noexcept;
    juce::Rectangle<int> rowBounds (int row) const noexcept;
    juce::Rectangle<float> cellBounds (int row, int step) const noexcept;
    int rowAt (int y) const noexcept;
    int stepAt (int x) const noexcept;

    void applyGesture (const juce::MouseEvent&, bool firstOfGesture);

    SharedPattern& shared;
    int laneIndex = 0;
    bool toggleTarget = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};

} // namespace kinetic
