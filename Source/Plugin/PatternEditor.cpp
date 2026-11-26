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

    const juce::StringArray keyNames { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };

    // Display order of the scales, which is not the enum order the state format stores.
    constexpr ScaleType scaleOrder[] = { ScaleType::chromatic, ScaleType::major, ScaleType::minor,
                                         ScaleType::dorian, ScaleType::phrygianDominant,
                                         ScaleType::pentatonic };
    constexpr int numScaleTypes = static_cast<int> (sizeof (scaleOrder) / sizeof (scaleOrder[0]));

        const juce::StringArray scaleNames { "CHROMATIC", "MAJOR", "MINOR", "DORIAN",
                                         "PHRYGIAN DOM", "PENTATONIC" };
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
        shared.edit().rootNote = note;
        commit();
    };
    addAndMakeVisible (rootNoteSelector);

    // Scale aware: one switch for the whole pattern, then the key and the scale it snaps to.
    scaleButton.setClickingTogglesState (true);
    scaleButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    scaleButton.setColour (juce::TextButton::textColourOnId, theme::background);
    scaleButton.setTooltip ("Scale aware: snap every generated note to the key and scale below");
    scaleButton.onClick = [this]
    {
        shared.edit().scaleEnabled = scaleButton.getToggleState();
        commit();
        refreshControls();
    };
    addAndMakeVisible (scaleButton);

    for (int i = 0; i < keyNames.size(); ++i)
        keyBox.addItem (keyNames[i], i + 1);

    keyBox.setTooltip ("Key every generated note is snapped to");
    keyBox.onChange = [this]
    {
        shared.edit().globalRoot = juce::jlimit (0, 11, keyBox.getSelectedId() - 1);
        commit();
    };
    addAndMakeVisible (keyBox);

    for (int i = 0; i < numScaleTypes; ++i)
        scaleBox.addItem (scaleNames[i], i + 1);

    scaleBox.setTooltip ("Scale every generated note is snapped to");
    scaleBox.onChange = [this]
    {
        const auto index = juce::jlimit (0, numScaleTypes - 1, scaleBox.getSelectedId() - 1);
        shared.edit().globalScale = scaleOrder[index];
        commit();
    };
    addAndMakeVisible (scaleBox);

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

    const auto& pattern = shared.edit();

    rootNoteSelector.setNote (pattern.rootNote);
    scaleButton.setToggleState (pattern.scaleEnabled, juce::dontSendNotification);
    scaleButton.setButtonText (pattern.scaleEnabled ? "SCALE ON" : "SCALE OFF");

    keyBox.setSelectedId (juce::jlimit (0, 11, pattern.globalRoot) + 1, juce::dontSendNotification);

    auto scaleId = 1;

    for (int i = 0; i < numScaleTypes; ++i)
        if (scaleOrder[i] == pattern.globalScale)
            scaleId = i + 1;

    scaleBox.setSelectedId (scaleId, juce::dontSendNotification);
}

void PatternEditor::commit()
{
    shared.publish();
    grid.refresh();
}

void PatternEditor::resized()
{
    auto area = getLocalBounds();

    // Transpose and scale aware apply to the whole pattern, so they sit above the lane row,
    // which only selects and enables the lane being edited.
    auto globalRow = area.removeFromTop (24);

    rootNoteSelector.setBounds (globalRow.removeFromLeft (100));
    globalRow.removeFromLeft (14);
    scaleButton.setBounds (globalRow.removeFromLeft (100).reduced (0, 1));
    globalRow.removeFromLeft (8);
    keyBox.setBounds (globalRow.removeFromLeft (66));
    globalRow.removeFromLeft (6);
    scaleBox.setBounds (globalRow.removeFromLeft (152));

    area.removeFromTop (10);

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

    area.removeFromTop (8);
    grid.setBounds (area);
}

PatternEditor::RootNoteSelector::RootNoteSelector()
{
    downButton.setTooltip ("Move the root note one octave down, every lane follows");
    upButton.setTooltip ("Move the root note one octave up, every lane follows");

    downButton.onClick = [this] { shiftOctave (-1); };
    upButton.onClick = [this] { shiftOctave (1); };

    addAndMakeVisible (downButton);
    addAndMakeVisible (upButton);
}

void PatternEditor::RootNoteSelector::paint (juce::Graphics& g)
{
    if (octaveArea.isEmpty())
        return;

    g.setColour (theme::raised);
    g.fillRoundedRectangle (octaveArea.toFloat(), 3.0f);

    g.setColour (theme::text);
    g.setFont (theme::mono (10.0f));
    g.drawText ("OCT " + juce::String (note / 12 - 2), octaveArea,
                juce::Justification::centred, false);
}

void PatternEditor::RootNoteSelector::resized()
{
    auto area = getLocalBounds();
    constexpr int buttonWidth = 24;

    downButton.setBounds (area.removeFromLeft (buttonWidth).reduced (0, 1));
    upButton.setBounds (area.removeFromLeft (buttonWidth).reduced (1, 1));

    octaveArea = area.reduced (6, 1);
}

void PatternEditor::RootNoteSelector::setNote (int newNote)
{
    note = juce::jlimit (0, 127, newNote);
    repaint();
}

void PatternEditor::RootNoteSelector::shiftOctave (int direction)
{
    // Refused rather than clamped at the ends, so the root keeps its note letter at the top.
    const auto shifted = note + 12 * direction;

    if (shifted < 0 || shifted > 127)
        return;

    note = shifted;
    repaint();

    if (onNoteChange != nullptr)
        onNoteChange (note);
}

} // namespace kinetic
