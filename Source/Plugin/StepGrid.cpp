#include "StepGrid.h"

#include "Theme.h"

#include <cmath>

namespace kinetic
{

namespace
{
    constexpr int gutterWidth = 58;
    constexpr int headerHeight = 16;

    constexpr double pitchRange = 24.0;   // +/- semitones a pitch cell can reach
    constexpr double knobTravel = 160.0;  // pixels of vertical drag for a knob's full range

    juce::String formatPitch (int pitch)
    {
        return (pitch > 0 ? "+" : "") + juce::String (pitch);
    }

    /** The strip along the bottom of a RATCH cell that switches the ratchet off.

        It takes a third of the cell at every size, so it stays a comfortable click target when
        the editor is enlarged, with a small floor for the shortest rows. The ratchet level
        above it keeps the other two thirds.
    */
    juce::Rectangle<float> ratchetClearStrip (juce::Rectangle<float> cell) noexcept
    {
        const auto height = juce::jmax (5.0f, cell.getHeight() * 0.34f);
        return cell.withTop (cell.getBottom() - height).reduced (1.0f, 0.5f);
    }

    /** A rotary knob filling `cell`, its arc running from 7:30 to 4:30 for `t` in 0..1. */
    void drawKnob (juce::Graphics& g, juce::Rectangle<float> cell, double t,
                   juce::Colour colour, float dim)
    {
        const auto size = juce::jmin (cell.getWidth(), cell.getHeight()) - 2.0f;

        if (size < 6.0f)
            return;

        const auto knob = cell.withSizeKeepingCentre (size, size);
        const auto radius = size * 0.5f - 1.0f;
        const auto centre = knob.getCentre();

        constexpr auto startAngle = -2.3561945f;   // 7:30, JUCE measures from 12 o'clock clockwise
        constexpr auto sweep = 4.712389f;          // 270 degrees

        const auto position = static_cast<float> (juce::jlimit (0.0, 1.0, t));

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             startAngle, startAngle + sweep, true);

        g.setColour (theme::muted.withAlpha (dim * 0.35f));
        g.strokePath (track, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        if (position > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                               startAngle, startAngle + sweep * position, true);

            g.setColour (colour.withAlpha (dim));
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        const auto angle = startAngle + sweep * position;
        const auto inner = radius * 0.2f;
        const auto outer = radius * 0.8f;

        g.setColour (colour.withAlpha (dim * 0.95f));
        g.drawLine (centre.x + std::sin (angle) * inner, centre.y - std::cos (angle) * inner,
                    centre.x + std::sin (angle) * outer, centre.y - std::cos (angle) * outer,
                    1.6f);
    }
} // namespace

StepGrid::StepGrid (SharedPattern& sharedPattern)
    : shared (sharedPattern)
{
    setOpaque (false);
    setTooltip ("Click or drag to toggle triggers and slides, drag a knob up or down to turn it, "
                "the strip under a ratchet switches it off");
}

void StepGrid::setLane (int newLaneIndex)
{
    laneIndex = juce::jlimit (0, maxLanes - 1, newLaneIndex);
    repaint();
}

juce::String StepGrid::rowLabel (int row)
{
    switch (row)
    {
        case trigger:  return "TRIG";
        case pitch:    return "PITCH";
        case velocity: return "VEL";
        case mod:      return "MOD";
        case gate:     return "GATE";
        case ratchet:  return "RATCH";
        case slide:    return "SLIDE";
        default:       break;
    }

    return {};
}

int StepGrid::numColumns() const noexcept
{
    return juce::jmax (16, juce::jlimit (1, maxSteps, currentLane().settings.gateLength));
}

juce::Rectangle<int> StepGrid::rowBounds (int row) const noexcept
{
    auto area = getLocalBounds();
    area.removeFromLeft (gutterWidth);
    area.removeFromTop (headerHeight);

    const auto rowHeight = area.getHeight() / numRows;

    return { area.getX(), area.getY() + row * rowHeight, area.getWidth(), rowHeight };
}

juce::Rectangle<float> StepGrid::cellBounds (int row, int step) const noexcept
{
    const auto band = rowBounds (row).toFloat();
    const auto width = band.getWidth() / static_cast<float> (numColumns());

    return { band.getX() + width * static_cast<float> (step) + 1.0f,
             band.getY() + 1.0f,
             width - 2.0f,
             juce::jmax (1.0f, band.getHeight() - 2.0f) };
}

int StepGrid::rowAt (int y) const noexcept
{
    const auto band = rowBounds (0);

    if (band.getHeight() <= 0 || y < band.getY())
        return -1;

    const auto row = (y - band.getY()) / band.getHeight();

    return row >= 0 && row < numRows ? row : -1;
}

int StepGrid::stepAt (int x) const noexcept
{
    const auto band = rowBounds (0);
    const auto width = static_cast<float> (band.getWidth()) / static_cast<float> (numColumns());

    if (x < band.getX() || width <= 0.0f)
        return -1;

    const auto step = static_cast<int> (static_cast<float> (x - band.getX()) / width);

    return step >= 0 && step < numColumns() ? step : -1;
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto& lane = currentLane();
    const auto columns = numColumns();
    const auto loopLength = juce::jlimit (1, maxSteps, lane.settings.gateLength);
    const auto cellWidth = static_cast<float> (rowBounds (0).getWidth()) / static_cast<float> (columns);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // Step numbers in the header strip above the rows; every fourth one when they are narrow.
    const auto labelEvery = cellWidth >= 16.0f ? 1 : 4;

    if (cellWidth >= 8.0f)
    {
        g.setFont (theme::mono (9.0f));

        for (int step = 0; step < columns; step += labelEvery)
        {
            const auto cell = cellBounds (0, step);

            g.setColour (step >= loopLength ? theme::muted.withAlpha (0.4f) : theme::muted);
            g.drawText (juce::String (step + 1),
                        juce::Rectangle<float> (cell.getX(), 0.0f, cell.getWidth(),
                                                static_cast<float> (headerHeight)),
                        juce::Justification::centred, false);
        }
    }

    for (int row = 0; row < numRows; ++row)
    {
        const auto band = rowBounds (row);

        g.setColour (theme::muted);
        g.setFont (theme::mono (9.5f));
        g.drawText (rowLabel (row),
                    juce::Rectangle<int> (2, band.getY(), gutterWidth - 8, band.getHeight()),
                    juce::Justification::centredRight, false);

        for (int step = 0; step < columns; ++step)
        {
            auto cell = cellBounds (row, step);
            const auto& value = lane.steps[static_cast<std::size_t> (step)];
            const auto inLoop = step < loopLength;
            const auto dim = inLoop ? 1.0f : 0.33f;

            // Every fourth step is a beat: those cells sit on a slightly lighter plate.
            g.setColour (theme::raised.withAlpha ((step / 4) % 2 == 0 ? dim : dim * 0.6f));
            g.fillRoundedRectangle (cell, 2.5f);

            switch (row)
            {
                case trigger:
                    if (value.trigger)
                    {
                        g.setColour (theme::accent.withAlpha (dim));
                        g.fillRoundedRectangle (cell.reduced (1.0f), 2.5f);
                    }
                    break;

                case slide:
                    if (value.slide)
                    {
                        g.setColour (theme::amber.withAlpha (dim));
                        g.fillRoundedRectangle (cell.reduced (1.0f), 2.5f);
                    }
                    break;

                case pitch:
                case velocity:
                case mod:
                case gate:
                {
                    const auto colour = row == velocity ? theme::accent
                                       : row == gate     ? theme::amber
                                                         : theme::violet;
                    const auto text = row == pitch  ? formatPitch (value.pitch)
                                    : row == gate   ? juce::String ((int) value.gatePercent) + "%"
                                    : row == mod    ? juce::String ((int) value.mod)
                                                    : juce::String ((int) value.velocity);

                    // The reading only fits beside the knob once the cell is big enough.
                    auto knobArea = cell;
                    const auto showValue = cell.getWidth() >= 22.0f && cell.getHeight() >= 26.0f;
                    const auto textArea = showValue ? knobArea.removeFromBottom (10.0f)
                                                    : juce::Rectangle<float>();

                    drawKnob (g, knobArea, normalisedValue (row, value), colour, dim);

                    if (showValue)
                    {
                        g.setColour (theme::text.withAlpha (dim * 0.9f));
                        g.setFont (theme::mono (8.0f));
                        g.drawText (text, textArea, juce::Justification::centred, false);
                    }
                    break;
                }

                case ratchet:
                {
                    const auto hasRatchet = value.ratchets > 1;
                    const auto strip = ratchetClearStrip (cell);

                    if (hasRatchet)
                    {
                        g.setColour (theme::violet.withAlpha (dim));
                        g.fillRoundedRectangle (cell.reduced (1.0f), 2.5f);

                        if (cell.getWidth() >= 18.0f)
                        {
                            g.setColour (theme::background);
                            g.setFont (theme::mono (8.5f));
                            g.drawText ("x" + juce::String ((int) value.ratchets),
                                        cell.withTrimmedBottom (strip.getHeight()),
                                        juce::Justification::centred, false);
                        }
                    }

                    // "Off" is one ratchet, which used to be the lowest eighth of the cell and
                    // looked like nothing at all. The strip along the bottom is that target, and
                    // the bar in it is the minus sign, drawn rather than typed so it stays
                    // legible when cells are only a few pixels tall.
                    g.setColour (hasRatchet ? theme::background.withAlpha (0.3f)
                                            : theme::raised.brighter (0.12f).withAlpha (dim));
                    g.fillRoundedRectangle (strip, 1.5f);

                    g.setColour (hasRatchet ? theme::background.withAlpha (0.95f)
                                            : theme::muted.withAlpha (dim * 0.85f));
                    g.fillRect (strip.withSizeKeepingCentre (
                        juce::jmax (7.0f, strip.getWidth() * 0.22f),
                        juce::jmax (1.0f, std::round (strip.getHeight() * 0.2f))));
                    break;
                }

                default:
                    break;
            }
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    dragRow = rowAt (static_cast<int> (e.position.y));
    dragStep = stepAt (static_cast<int> (e.position.x));

    if (dragRow < 0 || dragStep < 0)
        return;

    auto& value = currentLane().steps[static_cast<std::size_t> (dragStep)];

    dragStartY = e.position.y;
    dragStartValue = normalisedValue (dragRow, value);

    switch (dragRow)
    {
        case trigger:
        case slide:
            toggleTarget = dragRow == trigger ? ! value.trigger : ! value.slide;

            if (dragRow == trigger)
                value.trigger = toggleTarget;
            else
                value.slide = toggleTarget;
            break;

        case ratchet:
            setRatchetFromPointer (dragStep, e.position);
            break;

        default:
            return;     // a knob turns with the drag rather than jumping to the click
    }

    shared.publish();
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0 || dragStep < 0)
        return;

    // Toggles paint across columns; a knob stays on the cell it grabbed, so dragging past the
    // row border never edits the cell above or below.
    const auto paintsColumns = dragRow == trigger || dragRow == slide || dragRow == ratchet;
    const auto step = paintsColumns ? stepAt (static_cast<int> (e.position.x)) : dragStep;

    if (step < 0)
        return;

    auto& value = currentLane().steps[static_cast<std::size_t> (step)];

    switch (dragRow)
    {
        case trigger: value.trigger = toggleTarget; break;
        case slide:   value.slide = toggleTarget; break;

        case ratchet:
            setRatchetFromPointer (step, e.position);
            break;

        default:
            setNormalisedValue (dragRow, value,
                                dragStartValue + (dragStartY - e.position.y) / knobTravel);
            break;
    }

    shared.publish();
    repaint();
}

double StepGrid::normalisedValue (int row, const Step& step) noexcept
{
    switch (row)
    {
        case pitch:    return (static_cast<double> (step.pitch) + pitchRange) / (pitchRange * 2.0);
        case velocity: return static_cast<double> (step.velocity) / 127.0;
        case mod:      return static_cast<double> (step.mod) / 127.0;
        case gate:     return (static_cast<double> (juce::jlimit (2, 100, (int) step.gatePercent)) - 2.0) / 98.0;
        default:       break;
    }

    return 0.0;
}

void StepGrid::setNormalisedValue (int row, Step& step, double t) noexcept
{
    t = juce::jlimit (0.0, 1.0, t);

    switch (row)
    {
        case pitch:    step.pitch = static_cast<juce::int8> (std::lround (-pitchRange + t * pitchRange * 2.0)); break;
        case velocity: step.velocity = static_cast<juce::uint8> (std::lround (t * 127.0)); break;
        case mod:      step.mod = static_cast<juce::uint8> (std::lround (t * 127.0)); break;
        case gate:     step.gatePercent = static_cast<juce::uint8> (std::lround (2.0 + t * 98.0)); break;
        default:       break;
    }
}

void StepGrid::setRatchetFromPointer (int step, juce::Point<float> position)
{
    auto& value = currentLane().steps[static_cast<std::size_t> (step)];
    const auto strip = ratchetClearStrip (cellBounds (ratchet, step));
    const auto band = rowBounds (ratchet).toFloat();

    // 1 at the top of the row band, 0 at the bottom.
    const auto t = juce::jlimit (0.0, 1.0,
                                 1.0 - (static_cast<double> (position.y) - band.getY())
                                           / juce::jmax (1.0f, band.getHeight()));

    value.ratchets = strip.contains (position)
                       ? 1
                       : static_cast<juce::uint8> (juce::jlimit (1, 8, 1 + (int) std::floor (t * 8.0)));
}

} // namespace kinetic
