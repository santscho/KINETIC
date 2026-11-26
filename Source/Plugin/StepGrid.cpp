#include "StepGrid.h"

#include "Core/Sequencer/SequenceMath.h"
#include "Theme.h"

#include <cmath>

namespace kinetic
{

namespace
{
    constexpr int gutterWidth = 58;
    constexpr int headerHeight = 16;

    constexpr double knobTravel = 160.0;    // pixels of vertical drag for a knob's full range
    constexpr float pitchButtonWidth = 13.0f;

    juce::String noteName (int note)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };

        note = juce::jlimit (0, 127, note);

        return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
    }

    juce::String pitchClassName (int note)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };

        return names[juce::jlimit (0, 127, note) % 12];
    }

    /** The note a pitch cell plays: its offset from the pattern root, clamped to the MIDI range. */
    int absoluteNote (int rootNote, int pitchOffset) noexcept
    {
        return juce::jlimit (0, 127, rootNote + pitchOffset);
    }

    /** Absolute pitch classes the active key and scale allow; 0x0fff while scale aware is off. */
    juce::uint16 allowedPitchClasses (const Pattern& pattern) noexcept
    {
        if (! pattern.scaleEnabled)
            return 0x0fff;

        const auto mask = math::scaleMask (pattern.globalScale, pattern.customScaleMask);

        if ((mask & 0x0fffu) == 0x0fffu)
            return 0x0fff;

        juce::uint16 allowed = 0;

        for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        {
            const auto degree = (((pitchClass - pattern.globalRoot) % 12) + 12) % 12;

            if (((mask >> degree) & 1) != 0)
                allowed = static_cast<juce::uint16> (allowed | (1u << pitchClass));
        }

        return allowed;
    }

    /** The pitch cell turns into a note picker once it is wide enough for its two buttons. */
    bool showsPitchButtons (juce::Rectangle<float> cell) noexcept
    {
        return cell.getWidth() >= 40.0f && cell.getHeight() >= 20.0f;
    }

    juce::Rectangle<float> pitchOctaveButton (juce::Rectangle<float> cell, bool up) noexcept
    {
        const auto width = juce::jmin (pitchButtonWidth, cell.getWidth() * 0.34f);
        const auto height = juce::jmin (cell.getHeight(), 18.0f);
        const auto x = up ? cell.getRight() - width : cell.getX();

        return { x, cell.getCentreY() - height * 0.5f, width, height };
    }

    /** A minus or plus glyph, drawn rather than typed so it stays sharp in a 13 pixel button. */
    void drawPitchButton (juce::Graphics& g, juce::Rectangle<float> area, bool up, float dim)
    {
        g.setColour (theme::raised.brighter (0.14f).withAlpha (dim));
        g.fillRoundedRectangle (area.reduced (0.5f), 1.5f);

        const auto arm = juce::jmax (1.5f, juce::jmin (area.getWidth(), area.getHeight()) * 0.26f);
        const auto centre = area.getCentre();

        g.setColour (theme::muted.withAlpha (dim * 0.95f));
        g.fillRect (juce::Rectangle<float> (arm * 2.0f, 1.2f).withCentre (centre));

        if (up)
            g.fillRect (juce::Rectangle<float> (1.2f, arm * 2.0f).withCentre (centre));
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
                "click a pitch cell for its notes, the strip under a ratchet switches it off");
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
    const auto& pattern = shared.edit();
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
                {
                    // A note picker rather than a knob: the cell names the note it plays, the
                    // small buttons beside it move that note an octave, and clicking the name
                    // opens the notes of its octave.
                    const auto note = absoluteNote (pattern.rootNote, value.pitch);

                    g.setColour (theme::violet.withAlpha (dim * 0.3f));
                    g.drawRoundedRectangle (cell.reduced (0.5f), 2.0f, 1.0f);

                    g.setColour (theme::text.withAlpha (dim * 0.92f));

                    if (showsPitchButtons (cell))
                    {
                        drawPitchButton (g, pitchOctaveButton (cell, false), false, dim);
                        drawPitchButton (g, pitchOctaveButton (cell, true), true, dim);

                        g.setFont (theme::mono (9.0f));
                        g.drawText (noteName (note),
                                    cell.withTrimmedLeft (pitchButtonWidth + 1.0f)
                                        .withTrimmedRight (pitchButtonWidth + 1.0f),
                                    juce::Justification::centred, false);
                    }
                    else
                    {
                        // Too narrow for buttons: the note alone, then only its pitch class.
                        const auto wide = cell.getWidth() >= 20.0f;

                        g.setFont (theme::mono (wide ? 8.0f : 6.5f));
                        g.drawText (wide ? noteName (note) : pitchClassName (note),
                                    cell, juce::Justification::centred, false);
                    }
                    break;
                }

                case velocity:
                case mod:
                case gate:
                {
                    const auto colour = row == velocity ? theme::accent
                                       : row == gate     ? theme::amber
                                                         : theme::violet;
                    const auto text = row == gate ? juce::String ((int) value.gatePercent) + "%"
                                    : row == mod  ? juce::String ((int) value.mod)
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
                            g.setFont (theme::mono (10.5f));
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

        case pitch:
        {
            const auto cell = cellBounds (pitch, dragStep);
            const auto buttons = showsPitchButtons (cell);

            if (buttons && pitchOctaveButton (cell, false).contains (e.position))
                shiftPitchOctave (dragStep, -1);
            else if (buttons && pitchOctaveButton (cell, true).contains (e.position))
                shiftPitchOctave (dragStep, 1);
            else
                showPitchMenu (dragStep, cell);

            dragRow = -1;       // a pitch cell is clicked, never dragged
            dragStep = -1;
            return;
        }

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
        case velocity: step.velocity = static_cast<juce::uint8> (std::lround (t * 127.0)); break;
        case mod:      step.mod = static_cast<juce::uint8> (std::lround (t * 127.0)); break;
        case gate:     step.gatePercent = static_cast<juce::uint8> (std::lround (2.0 + t * 98.0)); break;
        default:       break;
    }
}

void StepGrid::setPitch (int step, int note)
{
    // The cell stores an offset from the pattern root, so a note picked here survives a
    // transpose; the whole MIDI range is reachable, the engine clamps nothing further.
    auto& value = currentLane().steps[static_cast<std::size_t> (step)];

    value.pitch = static_cast<juce::int8> (juce::jlimit (0, 127, note) - shared.edit().rootNote);
    shared.publish();
    repaint();
}

void StepGrid::shiftPitchOctave (int step, int direction)
{
    auto& value = currentLane().steps[static_cast<std::size_t> (step)];
    const auto shifted = static_cast<int> (value.pitch) + 12 * direction;
    const auto note = shared.edit().rootNote + shifted;

    // Refused at the ends rather than clamped, so the note keeps the pitch class it was picked
    // with - the same rule the root note's octave buttons follow.
    if (note < 0 || note > 127)
        return;

    value.pitch = static_cast<juce::int8> (shifted);
    shared.publish();
    repaint();
}

void StepGrid::showPitchMenu (int step, juce::Rectangle<float> anchor)
{
    const auto& pattern = shared.edit();
    const auto note = absoluteNote (pattern.rootNote,
                                   currentLane().steps[static_cast<std::size_t> (step)].pitch);
    const auto octave = note / 12;                  // the octave the cell currently sits in
    const auto allowed = allowedPitchClasses (pattern);
    const auto buttons = showsPitchButtons (cellBounds (pitch, step));

    juce::PopupMenu menu;
    menu.addSectionHeader ("Octave " + juce::String (octave - 2));

    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        if (((allowed >> pitchClass) & 1) != 0)
        {
            const auto candidate = octave * 12 + pitchClass;

            // JUCE 8 takes isEnabled before isTicked: every note stays selectable, the
            // current one just carries the tick.
            menu.addItem (pitchClass + 1, noteName (candidate), true, candidate == note);
        }

    // With the buttons hidden by a narrow grid the menu carries the octave moves instead.
    if (! buttons)
    {
        menu.addSeparator();
        menu.addItem (100, "Octave up");
        menu.addItem (101, "Octave down");
    }

    juce::Component::SafePointer<StepGrid> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea (localAreaToGlobal (anchor.toNearestInt()))
                            .withParentComponent (getTopLevelComponent()),
                        [safeThis, step, octave] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            if (result == 100 || result == 101)
                            {
                                safeThis->shiftPitchOctave (step, result == 100 ? 1 : -1);
                                return;
                            }

                            safeThis->setPitch (step, octave * 12 + (result - 1));
                        });
}

void StepGrid::setRatchetFromPointer (int step, juce::Point<float> position)
{
    auto& value = currentLane().steps[static_cast<std::size_t> (step)];
    const auto strip = ratchetClearStrip (cellBounds (ratchet, step));
    const auto band = rowBounds (ratchet).toFloat();

    // 1 at the top of the band above the strip, 0 at the strip's top edge. The strip is the
    // only way back to "off", so the seven ratchet factors share the rest of the cell and x2,
    // the default, sits directly on top of the strip.
    const auto height = juce::jmax (1.0f, strip.getY() - band.getY());
    const auto t = juce::jlimit (0.0, 1.0,
                                 1.0 - (static_cast<double> (position.y) - band.getY())
                                           / static_cast<double> (height));

    value.ratchets = strip.contains (position)
                       ? 1
                       : static_cast<juce::uint8> (juce::jlimit (2, 8,
                                                                 2 + (int) std::floor (t * 7.0)));
}

} // namespace kinetic
