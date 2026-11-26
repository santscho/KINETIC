#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Sequencer/SharedPattern.h"

namespace kinetic
{

/**
    The step grid: one column per step, seven rows of parameters.

    Trigger and slide cells toggle and paint across columns; the value rows are knobs, turned by
    a vertical drag. A drag never changes row, and a knob drag stays on the cell it grabbed, so
    crossing a row border cannot edit the cell above or below. Every change is published
    immediately, so the audio thread picks it up at its next block boundary - the grid never
    touches the pattern the sequencer is reading.
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

    static double normalisedValue (int row, const Step&) noexcept;
    static void setNormalisedValue (int row, Step&, double t) noexcept;

    void setRatchetFromPointer (int step, juce::Point<float> position);

    SharedPattern& shared;
    int laneIndex = 0;

    // The row and cell a drag started in: a knob drag holds on to them until the mouse is up.
    int dragRow = -1;
    int dragStep = -1;
    double dragStartY = 0.0;
    double dragStartValue = 0.0;
    bool toggleTarget = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};

} // namespace kinetic
