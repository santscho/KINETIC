#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/KineticConfig.h"
#include "Core/Preset/PresetLibrary.h"
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
    // The host's program list is the preset folder: one program per preset file with the factory
    // reset first. Recalling a program is the same operation as choosing a preset in the editor's
    // header, so the host menu, the header and the pattern in the session cannot drift apart.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Presets. Every one of these is message thread only.

    /** The preset list as the program list shows it: the factory entry first. */
    juce::Array<presets::Entry> getPresetEntries() const { return presetLibrary.entries(); }

    juce::File getPresetFolder() const { return presetLibrary.directory(); }

    /** Loads preset `index` into the pattern and makes it the current program. */
    bool applyPreset (int index);

    /** Writes the pattern as a preset. `replacing` is a program index, or -1 for a new file. */
    juce::Result savePreset (const juce::String& name, int replacing,
                             int* savedProgram = nullptr);

    juce::Result deletePreset (int index);
    juce::Result renamePreset (int index, const juce::String& newName);

    /** Re-reads the folder and tells the host the program list changed. */
    void refreshPresetList();

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
        std::atomic<int>    generatedSinceStart { 0 };   // restarts with the transport
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

    void updateProgramNames();
    int programIndexForFile (const juce::File& file) const;

    kinetic::TransportEngine engine;
    kinetic::SharedPattern sharedPattern;
    std::atomic<int> selectedLane { 0 };
    std::atomic<juce::uint32> patternRevision { 0 };
    TransportView transportView;

    // The library is only ever touched on the message thread; the host may ask for program names
    // from any thread, so what it reads is a snapshot guarded by programLock.
    presets::Library presetLibrary;
    std::atomic<int> currentProgram { 0 };
    juce::StringArray programNames;
    mutable juce::CriticalSection programLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KineticAudioProcessor)
};

} // namespace kinetic
