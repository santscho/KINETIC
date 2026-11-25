#pragma once

#include "SequencerTypes.h"

#include <atomic>

namespace kinetic
{

/**
    Lock-free hand-off of the pattern being edited to the audio thread.

    The message thread edits its own private copy and publishes it; the audio thread takes the
    newest publication once per block and plays it in place, so no lock, no allocation and no
    copy of the pattern happens on the audio thread. Three slots and two index exchanges mean
    neither thread can touch the buffer the other is using:

      - the writer only writes its own slot, then swaps it into `shared`,
      - the reader only ever swaps in whatever `shared` holds, which is fully written.

    A slot is therefore never both being written and being read, and a published pattern is
    always read whole rather than half-updated.

    The reader may hold the slot takeLatest() returned for as long as it likes: that slot only
    goes back to the writer after the reader exchanges again, so a pointer taken from here
    stays valid until the next call. This is what lets the sequencer play the snapshot by
    reference instead of copying it into the audio callback.

    `dirty` records whether anything was published since the reader last looked, so an
    unchanged pattern costs one atomic load per block.
*/
class SharedPattern
{
public:
    SharedPattern() = default;   // JUCE_DECLARE_NON_COPYABLE suppresses the implicit one

    /** Message thread: the copy to edit. Call publish() to make the changes audible. */
    Pattern& edit() noexcept { return editable; }
    const Pattern& edit() const noexcept { return editable; }

    /** Message thread: hand the edited copy to the audio thread. */
    void publish() noexcept
    {
        slots[static_cast<std::size_t> (writerSlot)] = editable;
        writerSlot = shared.exchange (writerSlot, std::memory_order_acq_rel);
        dirty.store (true, std::memory_order_release);
    }

    /** Audio thread: the newest publication, or nullptr when nothing has changed. */
    const Pattern* takeLatest() noexcept
    {
        if (! dirty.load (std::memory_order_acquire))
            return nullptr;

        dirty.store (false, std::memory_order_release);
        readerSlot = shared.exchange (readerSlot, std::memory_order_acq_rel);

        return &slots[static_cast<std::size_t> (readerSlot)];
    }

private:
    Pattern editable {};
    Pattern slots[3] {};

    // The three indices must always be distinct; that is what guarantees neither thread can
    // reach the slot the other is using. `shared` therefore starts on the third slot rather
    // than on the reader's, and every exchange swaps two distinct values.
    std::atomic<int> shared { 2 };      // the published-but-unread slot
    std::atomic<bool> dirty { false };
    int writerSlot = 0;                 // message thread's private slot
    int readerSlot = 1;                 // audio thread's private slot

    JUCE_DECLARE_NON_COPYABLE (SharedPattern)
};

} // namespace kinetic
