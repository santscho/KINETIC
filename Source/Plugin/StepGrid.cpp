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

    juce::String formatPitch (int pitch)
    {
        return (pitch > 0 ? "+" : "") + juce::String (pitch);
    }
} // namespace

StepGrid::StepGrid (SharedPattern& sharedPattern)
    : shared (sharedPattern)
{
    setOpaque (false);
    setTooltip ("Click or drag: triggers and slides toggle, other rows follow the pointer height");
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

    // Step numbers, where they fit.
    if (cellWidth >= 16.0f)
    {
        g.setFont (theme::mono (9.0f));

        for (int step = 0; step < columns; ++step)
        {
            const auto cell = cellBounds (0, step);
            g.setColour (step >= loopLength ? theme::muted.withAlpha (0.4f) : theme::muted);
            g.drawText (juce::String (step + 1),
                        cell.withHeight (static_cast<float> (headerHeight)),
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
                {
                    const auto t = juce::jlimit (-1.0f, 1.0f,
                                                 static_cast<float> (value.pitch) / (float) pitchRange);
                    const auto centre = cell.getCentreY();
                    const auto half = cell.getHeight() * 0.5f - 2.0f;

                    g.setColour (theme::muted.withAlpha (dim * 0.5f));
                    g.fillRect (cell.getX(), centre, cell.getWidth(), 1.0f);

                    if (value.pitch != 0)
                    {
                        auto bar = cell.withY (t >= 0.0f ? centre - half * t : centre);
                        bar = bar.withHeight (half * std::abs (t));
                        g.setColour (theme::violet.withAlpha (dim));
                        g.fillRect (bar.reduced (0.5f, 0.0f));
                    }

                    if (cell.getWidth() >= 30.0f)
                    {
                        g.setColour (theme::text.withAlpha (dim * 0.9f));
                        g.setFont (theme::mono (8.5f));
                        g.drawText (formatPitch (value.pitch), cell, juce::Justification::centred, false);
                    }
                    break;
                }

                case velocity:
                case mod:
                {
                    const auto raw = row == velocity ? (int) value.velocity : (int) value.mod;
                    const auto t = juce::jlimit (0.0f, 1.0f, static_cast<float> (raw) / 127.0f);

                    g.setColour ((row == velocity ? theme::accent : theme::violet).withAlpha (dim * 0.85f));
                    g.fillRect (cell.withTrimmedTop (cell.getHeight() * (1.0f - t)));
                    break;
                }

                case gate:
                {
                    const auto t = juce::jlimit (0.0f, 1.0f, static_cast<float> (value.gatePercent) / 100.0f);

                    g.setColour (theme::amber.withAlpha (dim * 0.7f));
                    g.fillRect (cell.withWidth (cell.getWidth() * t));
                    break;
                }

                case ratchet:
                {
                    if (value.ratchets > 1)
                    {
                        g.setColour (theme::violet.withAlpha (dim));
                        g.fillRoundedRectangle (cell.reduced (1.0f), 2.5f);

                        if (cell.getWidth() >= 18.0f)
                        {
                            g.setColour (theme::background);
                            g.setFont (theme::mono (8.5f));
                            g.drawText ("x" + juce::String ((int) value.ratchets), cell,
                                        juce::Justification::centred, false);
                        }
                    }
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
    applyGesture (e, true);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    applyGesture (e, false);
}

void StepGrid::applyGesture (const juce::MouseEvent& e, bool firstOfGesture)
{
    const auto row = rowAt (static_cast<int> (e.position.y));
    const auto step = stepAt (static_cast<int> (e.position.x));

    if (row < 0 || step < 0)
        return;

    auto& value = currentLane().steps[static_cast<std::size_t> (step)];
    const auto band = rowBounds (row).toFloat();

    // 1 at the top of the row band, 0 at the bottom.
    const auto t = juce::jlimit (0.0, 1.0,
                                 1.0 - (static_cast<double> (e.position.y) - band.getY())
                                           / juce::jmax (1.0f, band.getHeight()));

    if (firstOfGesture)
    {
        if (row == trigger)
            toggleTarget = ! value.trigger;
        else if (row == slide)
            toggleTarget = ! value.slide;
    }

    switch (row)
    {
        case trigger:  value.trigger = toggleTarget; break;
        case slide:    value.slide = toggleTarget; break;
        case pitch:    value.pitch = static_cast<juce::int8> (std::lround (-pitchRange + t * pitchRange * 2.0)); break;
        case velocity: value.velocity = static_cast<juce::uint8> (std::lround (t * 127.0)); break;
        case mod:      value.mod = static_cast<juce::uint8> (std::lround (t * 127.0)); break;
        case gate:     value.gatePercent = static_cast<juce::uint8> (std::lround (2.0 + t * 98.0)); break;
        case ratchet:  value.ratchets = static_cast<juce::uint8> (juce::jlimit (1, 8, 1 + (int) std::floor (t * 8.0))); break;
        default:       return;
    }

    shared.publish();
    repaint();
}

} // namespace kinetic
