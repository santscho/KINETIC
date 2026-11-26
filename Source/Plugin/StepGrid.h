#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/Sequencer/SharedPattern.h"

namespace kinetic
{

/**
    The step grid: one column per step, seven rows of parameters.

    Trigger and slide cells toggle and paint across columns; the value rows are knobs, turned by
    a vertical drag, except the pitch row, which is a note picker: each cell shows the note it
    lands on and opens a list of the notes of its octave, with a small minus and plus beside it
    to move the note an octave. A drag never changes row, and a knob drag stays on the cell it
    grabbed, so crossing a row border cannot edit the cell above or below. Every change is
    published immediately, so the audio thread picks it up at its next block boundary - the grid
    never touches the pattern the sequencer is reading.

    A loop longer than 32 steps does not squeeze the columns: they keep the width they have at
    32 steps and the grid scrolls sideways behind a scroll bar along its bottom edge, with the
    row labels staying in place in the gutter.
*/
class StepGrid final : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::ScrollBar::Listener
{
public:
    explicit StepGrid (SharedPattern& sharedPattern);

    void setLane (int laneIndex);

    /** Re-reads the pattern: the loop length may have changed under the scroll bar. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum Row { trigger, pitch, velocity, mod, gate, ratchet, slide, numRows };

    static juce::String rowLabel (int row);

    Lane& currentLane() noexcept { return shared.edit().lanes[static_cast<std::size_t> (laneIndex)]; }
    const Lane& currentLane() const noexcept { return shared.edit().lanes[static_cast<std::size_t> (laneIndex)]; }

    int numColumns() const noexcept;
    juce::Rectangle<int> cellArea() const noexcept;
    juce::Rectangle<int> rowBounds (int row) const noexcept;
    juce::Rectangle<float> cellBounds (int row, int step) const noexcept;
    int rowAt (int y) const noexcept;
    int stepAt (int x) const noexcept;

    float cellWidth() const noexcept;
    double maxScroll() const noexcept;
    int scrollBarHeight() const noexcept;
    void layOutScrollBar();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    static double normalisedValue (int row, const Step&) noexcept;
    static void setNormalisedValue (int row, Step&, double t) noexcept;

    void setRatchetFromPointer (int step, juce::Point<float> position);
    void shiftPitchOctave (int step, int direction);
    void showPitchMenu (int step, juce::Rectangle<float> anchor);

    void setPitch (int step, int note);

    SharedPattern& shared;
    int laneIndex = 0;

    // A loop longer than 32 steps scrolls sideways rather than squeezing its columns.
    juce::ScrollBar horizontalScrollBar { false };
    double scrollPixels = 0.0;

    // The row and cell a drag started in: a knob drag holds on to them until the mouse is up.
    int dragRow = -1;
    int dragStep = -1;
    double dragStartY = 0.0;
    double dragStartValue = 0.0;
    bool toggleTarget = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};

} // namespace kinetic
