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
    // The host may ask for the program list before any editor exists, so it is built here.
    updateProgramNames();
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

//==============================================================================
// Program list: the preset folder as the host sees it
//==============================================================================

int KineticAudioProcessor::getNumPrograms()
{
    const juce::ScopedLock lock (programLock);

    return juce::jmax (1, programNames.size());
}

int KineticAudioProcessor::getCurrentProgram()
{
    const juce::ScopedLock lock (programLock);

    return juce::jlimit (0, juce::jmax (0, programNames.size() - 1),
                         currentProgram.load (std::memory_order_relaxed));
}

const juce::String KineticAudioProcessor::getProgramName (int index)
{
    const juce::ScopedLock lock (programLock);

    if (index < 0)      // the AudioProcessor contract allows -1 for the current program
        index = currentProgram.load (std::memory_order_relaxed);

    return juce::isPositiveAndBelow (index, programNames.size()) ? programNames[index]
                                                                 : juce::String();
}

void KineticAudioProcessor::setCurrentProgram (int index)
{
    applyPreset (index);
}

void KineticAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    // A host that renames a program is renaming the preset, so the file follows too.
    if (const auto result = renamePreset (index, newName); result.failed())
        juce::Logger::writeToLog ("KINETIC: " + result.getErrorMessage());
}

void KineticAudioProcessor::updateProgramNames()
{
    juce::StringArray names;

    for (const auto& entry : presetLibrary.entries())
        names.add (entry.name);

    const juce::ScopedLock lock (programLock);

    programNames = names;

    // A file removed behind the plug-in's back cannot leave the current program past the end.
    currentProgram.store (juce::jlimit (0, juce::jmax (0, names.size() - 1),
                                        currentProgram.load (std::memory_order_relaxed)),
                          std::memory_order_relaxed);
}

int KineticAudioProcessor::programIndexForFile (const juce::File& file) const
{
    if (file == juce::File())
        return 0;

    const auto& entries = presetLibrary.entries();

    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].file == file)
            return i;

    return 0;
}

void KineticAudioProcessor::refreshPresetList()
{
    presetLibrary.reload();
    updateProgramNames();

    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

bool KineticAudioProcessor::applyPreset (int index)
{
    auto entries = presetLibrary.entries();

    // A host can hold an index from before a file was added or removed; look at the folder again
    // before refusing it.
    if (! juce::isPositiveAndBelow (index, entries.size()))
    {
        presetLibrary.reload();
        entries = presetLibrary.entries();
        updateProgramNames();
    }

    if (! juce::isPositiveAndBelow (index, entries.size()))
        return false;

    Pattern loaded;
    auto lane = selectedLane.load();

    if (! presetLibrary.load (entries[index], loaded, lane))
        return false;

    sharedPattern.edit() = loaded;
    selectedLane.store (juce::jlimit (0, maxLanes - 1, lane));
    sharedPattern.publish();

    currentProgram.store (index, std::memory_order_relaxed);

    // The editor re-reads the pattern on its next tick, exactly as it does after the host loads a
    // session state, so a preset recalled from the host lands in the grid and the header bar.
    patternRevision.fetch_add (1, std::memory_order_relaxed);
    return true;
}

juce::Result KineticAudioProcessor::savePreset (const juce::String& name, int replacing,
                                                int* savedProgram)
{
    const auto entries = presetLibrary.entries();
    presets::Entry target;

    if (juce::isPositiveAndBelow (replacing, entries.size()))
        target = entries[replacing];

    if (target.isFactory)
        return juce::Result::fail ("The factory preset cannot be overwritten. Use Save As.");

    const auto replacingFile = target.file;
    const auto presetName = replacingFile == juce::File() ? name.trim() : target.name;

    juce::File saved;

    if (const auto result = presetLibrary.save (sharedPattern.edit(), selectedLane.load(),
                                                presetName, replacingFile, &saved);
        result.failed())
        return result;

    updateProgramNames();

    const auto program = programIndexForFile (saved);
    currentProgram.store (program, std::memory_order_relaxed);

    if (savedProgram != nullptr)
        *savedProgram = program;

    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    return juce::Result::ok();
}

juce::Result KineticAudioProcessor::deletePreset (int index)
{
    const auto entries = presetLibrary.entries();

    if (! juce::isPositiveAndBelow (index, entries.size()))
        return juce::Result::fail ("That preset is no longer in the folder.");

    const auto wasCurrent = index == currentProgram.load (std::memory_order_relaxed);

    if (const auto result = presetLibrary.remove (entries[index]);
        result.failed())
        return result;

    if (wasCurrent)
        currentProgram.store (0, std::memory_order_relaxed);

    updateProgramNames();
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    return juce::Result::ok();
}

juce::Result KineticAudioProcessor::renamePreset (int index, const juce::String& newName)
{
    const auto entries = presetLibrary.entries();

    if (! juce::isPositiveAndBelow (index, entries.size()))
        return juce::Result::fail ("That preset is no longer in the folder.");

    const auto currentFile = juce::isPositiveAndBelow (currentProgram.load(), entries.size())
                                 ? entries[currentProgram.load()].file
                                 : juce::File();

    if (const auto result = presetLibrary.rename (entries[index], newName);
        result.failed())
        return result;

    // The list re-sorts by name, so the current program is found again by the file it came from.
    currentProgram.store (programIndexForFile (currentFile), std::memory_order_relaxed);

    updateProgramNames();
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    return juce::Result::ok();
}

} // namespace kinetic
