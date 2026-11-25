#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace kinetic
{

/**
    Tracks which notes the engine currently has sounding, so a note-off can never be lost.

    The rule this class enforces: an engine-generated note-on is always paired with exactly one
    note-off, and any discontinuity in the host timeline (transport stop, locate, loop wrap,
    panic) flushes everything still held. Note-offs are only ever sent for notes the tracker
    believes are down, so hosts never see orphaned offs either.
*/
class NoteTracker
{
public:
    NoteTracker() = default;

    static constexpr int numChannels = 16;
    static constexpr int numNotes = 128;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        reset();
    }

    /** Forgets all state without emitting anything. */
    void reset() noexcept
    {
        for (auto& channel : slots)
            for (auto& slot : channel)
                slot = {};

        usedChannels.fill (false);
        activeCount_ = 0;
        orderCounter = 1;
    }

    /** Returns false when that channel/note is already sounding, in which case nothing is emitted. */
    bool noteOn (MidiEventSink& sink, int channel, int note, float velocity, int sampleOffset) noexcept
    {
        const auto ch = channelIndex (channel);
        const auto n = juce::jlimit (0, numNotes - 1, note);

        auto& slot = slots[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)];

        if (slot.active)
            return false;

        slot.active = true;
        slot.startOrder = orderCounter++;
        usedChannels[static_cast<std::size_t> (ch)] = true;
        ++activeCount_;

        sink.addNoteOn (ch + 1, n, juce::jlimit (0.0f, 1.0f, velocity), sampleOffset);
        return true;
    }

    /** Emits an off only if the note is genuinely held, so duplicates are impossible. */
    void noteOff (MidiEventSink& sink, int channel, int note, int sampleOffset) noexcept
    {
        const auto ch = channelIndex (channel);
        const auto n = juce::jlimit (0, numNotes - 1, note);

        auto& slot = slots[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)];

        if (! slot.active)
            return;

        slot = {};
        --activeCount_;

        sink.addNoteOff (ch + 1, n, sampleOffset);
    }

    /** Releases every note held on one channel (used when a lane's channel assignment changes). */
    void releaseChannel (MidiEventSink& sink, int channel, int sampleOffset) noexcept
    {
        const auto ch = channelIndex (channel);

        for (int note = 0; note < numNotes; ++note)
            noteOff (sink, ch + 1, note, sampleOffset);
    }

    /**
        Releases everything held and resets controllers.

        Explicit note-offs come first for notes we own, then sustain-off (CC64),
        all-notes-off (CC123) and reset-all-controllers (CC121) are sent on every channel that
        was in use, which also clears any note the host or a previous instance left hanging.
    */
    void panic (MidiEventSink& sink, int sampleOffset, int channel = 0) noexcept
    {
        if (channel > 0)
        {
            const auto ch = channelIndex (channel);
            releaseChannel (sink, ch + 1, sampleOffset);
            resetControllersOnChannel (sink, ch, sampleOffset);
            usedChannels[static_cast<std::size_t> (ch)] = false;
            return;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            releaseChannel (sink, ch + 1, sampleOffset);

            if (usedChannels[static_cast<std::size_t> (ch)])
            {
                resetControllersOnChannel (sink, ch, sampleOffset);
                usedChannels[static_cast<std::size_t> (ch)] = false;
            }
        }

        orderCounter = 1;
    }

    bool isActive (int channel, int note) const noexcept
    {
        const auto ch = channelIndex (channel);
        const auto n = juce::jlimit (0, numNotes - 1, note);
        return slots[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)].active;
    }

    int activeCount() const noexcept { return activeCount_; }

    int activeOnChannel (int channel) const noexcept
    {
        const auto ch = channelIndex (channel);
        int count = 0;

        for (const auto& slot : slots[static_cast<std::size_t> (ch)])
            count += slot.active ? 1 : 0;

        return count;
    }

private:
    static int channelIndex (int channel) noexcept { return juce::jlimit (1, numChannels, channel) - 1; }

    static void resetControllersOnChannel (MidiEventSink& sink, int zeroBasedChannel, int offset) noexcept
    {
        const auto oneBased = zeroBasedChannel + 1;
        sink.addController (oneBased, 64, 0, offset);    // sustain off
        sink.addController (oneBased, 123, 0, offset);   // all notes off
        sink.addController (oneBased, 121, 0, offset);   // reset all controllers
    }

    struct Slot
    {
        bool active = false;
        juce::uint32 startOrder = 0;   // for legato/slide handling in Step 3
    };

    std::array<std::array<Slot, numNotes>, numChannels> slots {};
    std::array<bool, numChannels> usedChannels {};
    int activeCount_ = 0;
    juce::uint32 orderCounter = 1;
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE (NoteTracker)
};

} // namespace kinetic
