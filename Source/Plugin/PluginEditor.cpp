#include "PluginEditor.h"

#include "Theme.h"

namespace kinetic
{

KineticAudioProcessorEditor::KineticAudioProcessorEditor (KineticAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p),
      patternEditor (p.getSharedPattern(), p.getSelectedLane())
{
    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (860, 470, 1600, 900);
    setSize (1040, 580);

    panicButton.setTooltip ("Release every held note and reset controllers");
    panicButton.onClick = [this]
    {
        audioProcessor.postUiCommand ({ kinetic::UiCommandType::allNotesOff, 0 });
    };

    addAndMakeVisible (panicButton);
    addAndMakeVisible (patternEditor);

    startTimerHz (20);
}

void KineticAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto area = getLocalBounds().reduced (16, 14);
    auto header = area.removeFromTop (28);

    g.setColour (theme::accent);
    g.fillRoundedRectangle (header.removeFromLeft (3).toFloat(), 1.5f);
    header.removeFromLeft (10);

    auto badge = header.removeFromRight (132);

    g.setColour (theme::text);
    g.setFont (theme::heading (19.0f));
    g.drawText ("KINETIC", header, juce::Justification::centredLeft, false);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (badge.toFloat().reduced (0.0f, 2.0f), 5.0f);

    g.setColour (theme::accent);
    g.setFont (theme::mono (10.0f));
    g.drawText (kinetic::formatName, badge, juce::Justification::centred, false);

    if (! footerArea.isEmpty())
    {
        g.setColour (theme::muted);
        g.setFont (theme::mono (11.0f));
        g.drawText (transportLine(), footerArea, juce::Justification::centredLeft, false);
    }
}

void KineticAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 14);
    area.removeFromTop (38);

    footerArea = area.removeFromBottom (26);
    panicButton.setBounds (footerArea.removeFromRight (74));

    patternEditor.setBounds (area);
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

void KineticAudioProcessorEditor::timerCallback()
{
    // The host can load a state while the editor is open; re-read the pattern when it does.
    if (const auto revision = audioProcessor.getPatternRevision(); revision != lastPatternRevision)
    {
        lastPatternRevision = revision;
        patternEditor.refreshAll();
    }

    repaint (0, getHeight() - 62, getWidth(), 62);
}

} // namespace kinetic
