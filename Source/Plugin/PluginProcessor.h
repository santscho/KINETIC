#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/KineticConfig.h"
#include "Core/Sequencer/SharedPattern.h"
#include "Core/Timing/TransportEngine.h"

namespace kinetic
{

/**
    Pure MIDI FX processor.

    Everything timing-related lives in TransportEngine; this class only bridges the host:
    AudioPlayHead -> TransportInput, commands from the editor -> lock-free queue, and the
    engine's generated events -> the host MidiBuffer. No UI timer ever schedules MIDI.
*/
class KineticAudioProcessor final : public juce::AudioProcessor
{
public:
    KineticAudioProcessor();
    ~KineticAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override  { return kinetic::productName; }
    bool acceptsMidi() const override            { return true; }
    bool producesMidi() const override           { return true; }
    bool isMidiEffect() const override           { return kinetic::isAuMidiFx; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    /** Message thread only. Returns false if the queue is momentarily full. */
    bool postUiCommand (const kinetic::UiCommand& command) noexcept;

    /** Read-only transport view for the editor: written on the audio thread, read by the UI. */
    struct TransportView
    {
        std::atomic<double> ppqPosition { 0.0 };
        std::atomic<double> tempoBpm { 120.0 };
        std::atomic<bool>   isPlaying { false };
        std::atomic<int>    activeNotes { 0 };
        std::atomic<int>    generatedEvents { 0 };
        std::atomic<int>    droppedEvents { 0 };
    };

    const TransportView& getTransportView() const noexcept { return transportView; }

    /** Message thread: the pattern the editor edits; the audio thread adopts it per block. */
    kinetic::SharedPattern& getSharedPattern() noexcept { return sharedPattern; }

    /** Message thread: the lane the editor is showing, saved alongside the pattern. */
    std::atomic<int>& getSelectedLane() noexcept { return selectedLane; }

    /** Bumped when the host loads a state, so an open editor can refresh itself. */
    juce::uint32 getPatternRevision() const noexcept
    {
        return patternRevision.load (std::memory_order_relaxed);
    }

private:
    kinetic::TransportInput readTransportInput() const;
    void publishTelemetry (const kinetic::BlockTiming& timing) noexcept;

    kinetic::TransportEngine engine;
    kinetic::SharedPattern sharedPattern;
    std::atomic<int> selectedLane { 0 };
    std::atomic<juce::uint32> patternRevision { 0 };
    TransportView transportView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KineticAudioProcessor)
};

} // namespace kinetic
