#pragma once

#include <atomic>
#include "RhythmTypes.h"

namespace rf
{
    // Step pattern shared between the message thread (editor / generator) and the audio thread.
    // Every cell is a std::atomic<uint8_t>, so the audio thread can read while the UI writes,
    // without locks or allocation. Layer 0 is the main pattern, layer 1 is the fill pattern.
    class Pattern
    {
    public:
        Pattern() noexcept { clearAll(); }

        Pattern (const Pattern&) = delete;
        Pattern& operator= (const Pattern&) = delete;

        uint8_t get (int layer, int inst, int step) const noexcept
        {
            if (! inRange (layer, inst, step)) return 0;
            return cells[layer][inst][step].load (std::memory_order_relaxed);
        }

        void set (int layer, int inst, int step, uint8_t value) noexcept
        {
            if (inRange (layer, inst, step))
                cells[layer][inst][step].store (value, std::memory_order_relaxed);
        }

        void clearAll() noexcept
        {
            for (int l = 0; l < numLayers; ++l)
                clearLayer (l);
        }

        void clearLayer (int layer) noexcept
        {
            for (int i = 0; i < numInstruments; ++i)
                for (int s = 0; s < maxSteps; ++s)
                    set (layer, i, s, 0);
        }

        void copyFrom (const Pattern& other) noexcept
        {
            for (int l = 0; l < numLayers; ++l)
                for (int i = 0; i < numInstruments; ++i)
                    for (int s = 0; s < maxSteps; ++s)
                        set (l, i, s, other.get (l, i, s));
        }

    private:
        static bool inRange (int layer, int inst, int step) noexcept
        {
            return layer >= 0 && layer < numLayers && inst >= 0 && inst < numInstruments
                   && step >= 0 && step < maxSteps;
        }

        std::atomic<uint8_t> cells[numLayers][numInstruments][maxSteps];
    };
}
