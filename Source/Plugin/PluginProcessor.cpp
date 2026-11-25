#include "PluginProcessor.h"

#include "Core/Sequencer/PatternState.h"
#include "PluginEditor.h"

namespace kinetic
{

namespace
{
    /** The only place in the codebase that knows about the host's AudioPlayHead API. */
    TransportInput toTransportInput (const juce::AudioPlayHead::PositionInfo& position)
    {
        TransportInput input;

        input.isPlaying = position.getIsPlaying();

        if (const auto bpm = position.getBpm())
            input.bpm = *bpm;

        if (const auto ppq = position.getPpqPosition())
        {
            input.ppqPosition = *ppq;
            input.hasPosition = true;
        }

        if (const auto loop = position.getLoopPoints())
        {
            input.isLooping = true;
            input.loopStartPpq = loop->ppqStart;
            input.loopEndPpq = loop->ppqEnd;
        }

        return input;
    }
} // namespace

KineticAudioProcessor::KineticAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void KineticAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    transportView.ppqPosition.store (0.0, std::memory_order_relaxed);
    transportView.isPlaying.store (false, std::memory_order_relaxed);
    transportView.activeNotes.store (0, std::memory_order_relaxed);
    transportView.generatedSinceStart.store (0, std::memory_order_relaxed);
}

bool KineticAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // A MIDI effect AU must expose exactly one output bus (never written to) so the host can
    // pass a sample rate in; the VST3 build reuses that same bus as its silent audio fallback,
    // which is what lets hosts like Live route MIDI through without extra tracks. No audio
    // input is ever accepted.
    if (! layouts.inputBuses.isEmpty())
        return false;

    if (layouts.outputBuses.size() != 1)
        return false;

    const auto& output = layouts.outputBuses.getReference (0);

    return ! output.isDisabled() && output.size() <= 2;
}

void KineticAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // The plug-in never produces audio: the output bus exists purely for host compatibility.
    buffer.clear();

    // Adopt whatever the editor published since the last block, before anything is generated.
    // The hand-off is a pointer swap: nothing is copied and nothing is allocated here.
    if (const auto* published = sharedPattern.takeLatest())
        engine.usePattern (published);

    // Derives sample offsets from host PPQ and flushes any note left hanging by a stop,
    // locate or loop wrap - all inside the audio callback, before anything is generated.
    const auto& timing = engine.beginBlock (readTransportInput(), buffer.getNumSamples());

    // beginBlock() already ran the sequencer against this block's timing; the generated
    // events are waiting in the engine's sink.

    engine.flushGeneratedMidi (midiMessages);

    publishTelemetry (timing);
}

void KineticAudioProcessor::publishTelemetry (const kinetic::BlockTiming& timing) noexcept
{
    const auto generatedThisBlock = engine.events().size();

    // The footer counts the events of the whole run rather than of one block: a block only
    // carries events on step boundaries, so a per-block sample reads zero almost all the time
    // and says nothing about whether the sequencer is producing. The count restarts when the
    // transport starts, so it always describes the run the user is looking at.
    if (timing.didStart)
        transportView.generatedSinceStart.store (generatedThisBlock, std::memory_order_relaxed);
    else if (timing.isPlaying)
        transportView.generatedSinceStart.fetch_add (generatedThisBlock, std::memory_order_relaxed);

    transportView.ppqPosition.store (timing.blockStartPpq, std::memory_order_relaxed);
    transportView.tempoBpm.store (timing.bpm, std::memory_order_relaxed);
    transportView.isPlaying.store (timing.isPlaying, std::memory_order_relaxed);
    transportView.activeNotes.store (engine.activeNoteCount(), std::memory_order_relaxed);
    transportView.droppedEvents.store (engine.events().droppedCount(), std::memory_order_relaxed);
}

kinetic::TransportInput KineticAudioProcessor::readTransportInput() const
{
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            return toTransportInput (*position);

    return {};
}

bool KineticAudioProcessor::postUiCommand (const kinetic::UiCommand& command) noexcept
{
    return engine.postCommand (command);
}

juce::AudioProcessorEditor* KineticAudioProcessor::createEditor()
{
    return new KineticAudioProcessorEditor (*this);
}

void KineticAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("KINETIC_STATE");
    state.setAttribute ("version", kinetic::version);
    state.setAttribute ("format", kinetic::formatName);

    // Read the message-thread copy, which is the one the editor writes.
    kinetic::patternstate::writeToXml (sharedPattern.edit(), selectedLane.load(), state);

    copyXmlToBinary (state, destData);
}

void KineticAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName ("KINETIC_STATE"))
        {
            juce::Logger::writeToLog ("KINETIC: ignoring state with unexpected root element");
            return;
        }

        auto lane = selectedLane.load();

        // A state saved before patterns existed has no PATTERN element: the pattern stays as
        // it is and the load still succeeds.
        if (kinetic::patternstate::readFromXml (*xml, sharedPattern.edit(), lane))
        {
            sharedPattern.publish();          // the audio thread adopts it at its next block
            selectedLane.store (lane);
            patternRevision.fetch_add (1, std::memory_order_relaxed);
        }
    }
}

} // namespace kinetic
