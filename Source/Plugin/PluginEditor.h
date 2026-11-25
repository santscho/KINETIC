#pragma once

#include "PluginProcessor.h"

namespace kinetic
{

/**
    Step 2 editor.

    Still a placeholder for the Step 4 grid, but it now reads live timing telemetry and owns
    the only producer on the UI -> audio command queue (PANIC). The 20 Hz timer only refreshes
    the display; MIDI generation happens exclusively in processBlock.
*/
class KineticAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit KineticAudioProcessorEditor (KineticAudioProcessor&);
    ~KineticAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::String transportLine() const;
    juce::Rectangle<int> transportPanelBounds() const;

    KineticAudioProcessor& audioProcessor;
    juce::TextButton panicButton { "PANIC" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KineticAudioProcessorEditor)
};

} // namespace kinetic
