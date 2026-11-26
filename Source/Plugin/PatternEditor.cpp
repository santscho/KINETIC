#include "PatternEditor.h"

#include "Theme.h"

namespace kinetic
{

namespace
{
    constexpr int denominators[] = { 1, 2, 4, 8, 16, 32, 64 };
    constexpr int numDenominators = 7;

    const juce::StringArray directionNames { "FORWARD", "REVERSE", "PING-PONG",
                                             "PENDULUM", "RANDOM WALK" };

    // C3 is note 60, the naming the manual and every host use.
    juce::String noteName (int note)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };

        return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
    }
} // namespace

PatternEditor::PatternEditor (SharedPattern& sharedPattern, std::atomic<int>& selectedLaneRef)
    : shared (sharedPattern), selectedLane (selectedLaneRef), grid (sharedPattern)
{
    addAndMakeVisible (grid);

    for (int i = 0; i < maxLanes; ++i)
    {
        auto* button = laneButtons.add (new juce::TextButton (juce::String (i + 1)));

        button->setClickingTogglesState (true);
        button->setRadioGroupId (0x4b494e54);
        button->setColour (juce::TextButton::buttonOnColourId, theme::accent);
        button->setColour (juce::TextButton::textColourOnId, theme::background);
        button->setTooltip ("Edit lane " + juce::String (i + 1));
        button->onClick = [this, i] { selectLane (i); };

        addAndMakeVisible (button);
    }

    enableButton.setClickingTogglesState (true);
    enableButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    enableButton.setColour (juce::TextButton::textColourOnId, theme::background);
    enableButton.setTooltip ("Enable this lane - a disabled lane never sounds");
    enableButton.onClick = [this]
    {
        currentLane().settings.enabled = enableButton.getToggleState();
        commit();
        refreshControls();
    };
    addAndMakeVisible (enableButton);

    lengthSlider.setSliderStyle (juce::Slider::IncDecButtons);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 20);
    lengthSlider.setRange (1.0, (double) maxSteps, 1.0);
    lengthSlider.setTooltip ("Length of this lane's loop, 1-64 steps");
    lengthSlider.onValueChange = [this]
    {
        currentLane().settings.gateLength = (int) lengthSlider.getValue();
        commit();
    };
    addAndMakeVisible (lengthSlider);

    for (int i = 0; i < numDenominators; ++i)
        dividerBox.addItem ("1/" + juce::String (denominators[i]), i + 1);

    dividerBox.setTooltip ("Step duration, then dotted or triplet below");
    dividerBox.onChange = [this]
    {
        const auto index = juce::jlimit (0, numDenominators - 1, dividerBox.getSelectedId() - 1);
        auto& settings = currentLane().settings;

        settings.clock.numerator = 1;
        settings.clock.denominator = denominators[index];
        commit();
    };
    addAndMakeVisible (dividerBox);

    modifierBox.addItem ("STRAIGHT", 1);
    modifierBox.addItem ("DOTTED", 2);
    modifierBox.addItem ("TRIPLET", 3);
    modifierBox.setTooltip ("Straight, dotted or triplet");
    modifierBox.onChange = [this]
    {
        auto& settings = currentLane().settings;

        switch (modifierBox.getSelectedId())
        {
            case 2:  settings.clock.modifier = ClockModifier::dotted; break;
            case 3:  settings.clock.modifier = ClockModifier::triplet; break;
            default: settings.clock.modifier = ClockModifier::straight; break;
        }

        commit();
    };
    addAndMakeVisible (modifierBox);

    for (int i = 0; i < directionNames.size(); ++i)
        directionBox.addItem (directionNames[i], i + 1);

    directionBox.setTooltip ("Play direction, applied to every sub-lane");
    directionBox.onChange = [this]
    {
        const auto index = juce::jlimit (0, directionNames.size() - 1, directionBox.getSelectedId() - 1);
        currentLane().settings.direction = static_cast<PlayDirection> (index);
        commit();
    };
    addAndMakeVisible (directionBox);

    rootNoteSelector.onNoteChange = [this] (int note)
    {
        currentLane().settings.rootNote = note;
        commit();
    };
    addAndMakeVisible (rootNoteSelector);

    // A pattern with no enabled lane is silent, so open on something that can play.
    bool anyEnabled = false;

    for (const auto& lane : shared.edit().lanes)
        anyEnabled = anyEnabled || lane.settings.enabled;

    if (! anyEnabled)
    {
        shared.edit().lanes[0].settings.enabled = true;
        shared.publish();
    }

    selectLane (selectedLane.load());
}

void PatternEditor::refreshAll()
{
    selectLane (juce::jlimit (0, maxLanes - 1, selectedLane.load()));
    grid.refresh();
    repaint();
}

void PatternEditor::selectLane (int index)
{
    laneIndex = juce::jlimit (0, maxLanes - 1, index);
    selectedLane.store (laneIndex);
    laneButtons[laneIndex]->setToggleState (true, juce::dontSendNotification);

    grid.setLane (laneIndex);
    refreshControls();
    repaint();
}

void PatternEditor::refreshControls()
{
    const auto& settings = currentLane().settings;

    enableButton.setToggleState (settings.enabled, juce::dontSendNotification);
    enableButton.setButtonText (settings.enabled ? "ON" : "OFF");

    lengthSlider.setValue (juce::jlimit (1, maxSteps, settings.gateLength), juce::dontSendNotification);

    auto denominatorId = 1;

    for (int i = 0; i < numDenominators; ++i)
        if (denominators[i] == settings.clock.denominator)
            denominatorId = i + 1;

    dividerBox.setSelectedId (denominatorId, juce::dontSendNotification);

    auto modifierId = 1;

    if (settings.clock.modifier == ClockModifier::dotted)
        modifierId = 2;
    else if (settings.clock.modifier == ClockModifier::triplet)
        modifierId = 3;

    modifierBox.setSelectedId (modifierId, juce::dontSendNotification);
    directionBox.setSelectedId (static_cast<int> (settings.direction) + 1, juce::dontSendNotification);
    rootNoteSelector.setNote (settings.rootNote);
}

void PatternEditor::commit()
{
    shared.publish();
    grid.refresh();
}

void PatternEditor::resized()
{
    auto area = getLocalBounds();

    auto lanes = area.removeFromTop (26);

    for (auto* button : laneButtons)
        button->setBounds (lanes.removeFromLeft (30).reduced (1));

    enableButton.setBounds (lanes.removeFromRight (52).reduced (1, 0));

    area.removeFromTop (6);

    auto controls = area.removeFromTop (24);
    lengthSlider.setBounds (controls.removeFromLeft (152));
    controls.removeFromLeft (8);
    dividerBox.setBounds (controls.removeFromLeft (84));
    controls.removeFromLeft (6);
    modifierBox.setBounds (controls.removeFromLeft (104));
    controls.removeFromLeft (10);
    directionBox.setBounds (controls.removeFromLeft (136));
    controls.removeFromLeft (10);
    rootNoteSelector.setBounds (controls.removeFromLeft (124));

    area.removeFromTop (8);
    grid.setBounds (area);
}

PatternEditor::RootNoteSelector::RootNoteSelector()
{
    downButton.setTooltip ("Root note one octave down");
    upButton.setTooltip ("Root note one octave up");

    downButton.onClick = [this] { shiftOctave (-1); };
    upButton.onClick = [this] { shiftOctave (1); };

    addAndMakeVisible (downButton);
    addAndMakeVisible (upButton);

    noteBox.setTooltip ("Root note of this lane, within the current octave");
    noteBox.onChange = [this]
    {
        const auto base = note - (note % 12);
        const auto selected = juce::jlimit (0, 11, noteBox.getSelectedId() - 1);

        note = juce::jlimit (0, 127, base + selected);

        if (onNoteChange != nullptr)
            onNoteChange (note);
    };
    addAndMakeVisible (noteBox);

    rebuildItems();
}

void PatternEditor::RootNoteSelector::resized()
{
    auto area = getLocalBounds();
    const auto buttonWidth = juce::jlimit (20, 26, area.getWidth() / 5);

    downButton.setBounds (area.removeFromLeft (buttonWidth).reduced (0, 1));
    upButton.setBounds (area.removeFromLeft (buttonWidth).reduced (1, 1));
    noteBox.setBounds (area.reduced (2, 0));
}

void PatternEditor::RootNoteSelector::setNote (int newNote)
{
    note = juce::jlimit (0, 127, newNote);
    rebuildItems();
}

void PatternEditor::RootNoteSelector::rebuildItems()
{
    // One octave wide, starting on the C of the octave the note is in, so `+` on C1 relabels the
    // same twelve entries as C2 and `+` again gives C3.
    const auto base = note - (note % 12);

    noteBox.clear (juce::dontSendNotification);

    for (int i = 0; i < 12; ++i)
        noteBox.addItem (noteName (base + i), i + 1);

    noteBox.setSelectedId ((note % 12) + 1, juce::dontSendNotification);
}

void PatternEditor::RootNoteSelector::shiftOctave (int direction)
{
    const auto shifted = juce::jlimit (0, 127, note + 12 * direction);

    if (shifted == note)
        return;

    note = shifted;
    rebuildItems();

    if (onNoteChange != nullptr)
        onNoteChange (note);
}

} // namespace kinetic
