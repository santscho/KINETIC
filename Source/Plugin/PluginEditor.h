#pragma once

#include "PatternEditor.h"
#include "PluginProcessor.h"
#include "PresetBar.h"

namespace kinetic
{

/**
    Step 4 editor: the pattern panel, the transport read-out and the panic button.

    The 20 Hz timer refreshes the telemetry line only - MIDI generation happens exclusively in
    processBlock.
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

    /**
        The header row, split once so paint() and resized() cannot disagree about it: the badge
        owns the right end, the preset browser ends to its left and the title takes the rest.
    */
    struct HeaderLayout
    {
        juce::Rectangle<int> accentBar, title, preset, badge;
    };

    HeaderLayout headerLayout() const;

    KineticAudioProcessor& audioProcessor;
    PatternEditor patternEditor;
    PresetBar presetBar;
    juce::TextButton panicButton { "PANIC" };
    juce::Rectangle<int> footerArea;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::uint32 lastPatternRevision = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KineticAudioProcessorEditor)
};

} // namespace kinetic
