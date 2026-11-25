#include "PluginEditor.h"

namespace kinetic
{

namespace
{
const auto backgroundColour = juce::Colour (0xff0e1014);
const auto panelColour      = juce::Colour (0xff171b22);
const auto accentColour     = juce::Colour (0xff4ff0c0);
const auto textColour       = juce::Colour (0xffe7ecf3);
const auto mutedColour      = juce::Colour (0xff7c8797);

juce::Font headingFont (float height)
{
    return juce::Font (juce::FontOptions (height).withStyle ("Bold"));
}

juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                          height, juce::Font::plain));
}
} // namespace

KineticAudioProcessorEditor::KineticAudioProcessorEditor (KineticAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p)
{
    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (420, 210, 1260, 630);

    panicButton.setTooltip ("Release every held note and reset controllers");
    panicButton.onClick = [this]
    {
        audioProcessor.postUiCommand ({ kinetic::UiCommandType::allNotesOff, 0 });
    };

    addAndMakeVisible (panicButton);

    setSize (560, 236);
    startTimerHz (20);
}

void KineticAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (backgroundColour);
    g.fillRoundedRectangle (area, 12.0f);

    auto content = area.reduced (20.0f, 18.0f);

    g.setColour (accentColour);
    g.fillRoundedRectangle (content.removeFromTop (3.0f).removeFromLeft (48.0f), 1.5f);

    content.removeFromTop (10.0f);

    g.setColour (textColour);
    g.setFont (headingFont (28.0f));
    g.drawText ("KINETIC", content.removeFromTop (34.0f), juce::Justification::topLeft, false);

    g.setColour (mutedColour);
    g.setFont (headingFont (12.0f));
    g.drawText ("sample-accurate pure MIDI FX sequencer",
                content.removeFromTop (18.0f), juce::Justification::topLeft, false);

    content.removeFromTop (12.0f);

    auto badge = content.removeFromTop (22.0f).removeFromLeft (176.0f);
    g.setColour (panelColour);
    g.fillRoundedRectangle (badge, 5.0f);
    g.setColour (accentColour);
    g.setFont (monoFont (11.0f));
    g.drawText (kinetic::formatName, badge.reduced (9.0f, 0.0f),
                juce::Justification::centredLeft, false);

    const auto panel = transportPanelBounds();

    if (! panel.isEmpty())
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (panel.toFloat(), 6.0f);

        g.setColour (textColour);
        g.setFont (monoFont (12.5f));
        g.drawText (transportLine(), panel.reduced (12, 0),
                    juce::Justification::centredLeft, false);
    }
}

juce::Rectangle<int> KineticAudioProcessorEditor::transportPanelBounds() const
{
    auto area = getLocalBounds().reduced (20, 18);
    area.removeFromBottom (34);                      // space for the panic row
    return area.removeFromBottom (46);
}

juce::String KineticAudioProcessorEditor::transportLine() const
{
    const auto& view = audioProcessor.getTransportView();

    const auto ppq = view.ppqPosition.load (std::memory_order_relaxed);
    const auto bpm = view.tempoBpm.load (std::memory_order_relaxed);
    const auto playing = view.isPlaying.load (std::memory_order_relaxed);
    const auto held = view.activeNotes.load (std::memory_order_relaxed);
    const auto generated = view.generatedEvents.load (std::memory_order_relaxed);
    const auto dropped = view.droppedEvents.load (std::memory_order_relaxed);

    juce::String line;
    line << "PPQ " << juce::String (ppq, 2)
         << "   " << juce::String (bpm, 1) << " BPM"
         << "   " << (playing ? "PLAYING" : "STOPPED")
         << "   held " << held
         << "   gen " << generated;

    if (dropped > 0)
        line << "   dropped " << dropped;

    return line;
}

void KineticAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 18);
    panicButton.setBounds (area.removeFromBottom (26).removeFromRight (78));
}

void KineticAudioProcessorEditor::timerCallback()
{
    repaint();
}

} // namespace kinetic
