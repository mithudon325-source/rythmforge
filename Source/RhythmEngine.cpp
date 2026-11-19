#include "RhythmEngine.h"

#include <algorithm>
#include <cmath>

namespace rf
{
    void RhythmEngine::prepare (double sampleRate) noexcept
    {
        kit.prepare (sampleRate);
        smoothCoeff = (float) (1.0 - std::exp (-1.0 / (0.005 * sampleRate)));

        // Fixed stereo placement: kick and snare centred, hats slightly apart.
        static constexpr double pans[numInstruments] = { 0.0, 0.0, -0.25, 0.25 };
        for (int i = 0; i < numInstruments; ++i)
        {
            const double angle = (pans[i] + 1.0) * kPi / 4.0;
            gainL[i] = (float) (std::sqrt (2.0) * std::cos (angle));
            gainR[i] = (float) (std::sqrt (2.0) * std::sin (angle));
        }

        smoothersInitialised = false;
    }

    void RhythmEngine::reset() noexcept
    {
        kit.reset();
        smoothersInitialised = false;
    }

    int RhythmEngine::collectEvents (const Pattern& pattern, const Settings& s, const BlockTiming& t, int numSamples) noexcept
    {
        const double from = t.trigFrom;
        const double to   = t.trigTo;
        if (! (to > from) || t.ppqPerSample <= 0.0) return 0;

        const int ts = clampIndex (s.timeSig, numTimeSignatures);
        const int sd = clampIndex (s.subdiv, numSubdivisions);
        const double barQ    = barLengthInQuarters (ts);
        const double stepLen = stepLengthInQuarters (sd);
        const int steps      = stepsPerBar (ts, sd);
        const int spq        = stepsPerQuarter (sd);
        const bool swingable = (spq == 2 || spq == 4);

        int count = 0;
        const long long firstBar = (long long) std::floor (from / barQ) - 1;   // -1: swung steps can land in the next bar
        const long long lastBar  = (long long) std::floor (to / barQ);

        for (long long bar = firstBar; bar <= lastBar; ++bar)
        {
            bool fillBar = false;
            if (s.fillEveryBars > 0)
            {
                long long m = bar % s.fillEveryBars;
                if (m < 0) m += s.fillEveryBars;
                fillBar = (m == s.fillEveryBars - 1);
            }

            for (int k = 0; k < steps; ++k)
            {
                double pos = (double) bar * barQ + (double) k * stepLen;
                if (swingable && (k & 1) != 0)
                    pos += (double) s.swing * 0.34 * stepLen;

                if (pos < from || pos >= to) continue;

                const int layer = (fillBar && k >= steps / 2) ? 1 : 0;

                int sample = (int) std::floor ((pos - t.ppqStart) / t.ppqPerSample);
                sample = std::max (0, std::min (numSamples - 1, sample));

                for (int inst = 0; inst < numInstruments; ++inst)
                {
                    const uint8_t cell = pattern.get (layer, inst, k);
                    if (cell != CellOff && count < maxEvents)
                        events[(size_t) count++] = { sample, inst, cellVelocity (cell) };
                }
            }
        }

        // Sort by time. At equal times the open hat goes first so a simultaneous closed hat chokes it.
        std::sort (events.begin(), events.begin() + count, [] (const Event& a, const Event& b)
        {
            if (a.sample != b.sample) return a.sample < b.sample;
            return a.inst > b.inst;
        });

        return count;
    }

    void RhythmEngine::renderSegment (const Settings& s, float* left, float* right, int from, int to) noexcept
    {
        const float masterTarget = s.masterGain;

        for (int i = from; i < to; ++i)
        {
            float voices[numInstruments];
            kit.process (voices);

            float l = 0.0f, r = 0.0f;
            for (int v = 0; v < numInstruments; ++v)
            {
                smoothedLevels[v] += smoothCoeff * (s.levels[v] - smoothedLevels[v]);
                const float x = voices[v] * smoothedLevels[v];
                l += x * gainL[v];
                r += x * gainR[v];
            }

            smoothedMaster += smoothCoeff * (masterTarget - smoothedMaster);

            // Soft clip so that a loud mix can never exceed full scale.
            left[i]  = std::tanh (l * smoothedMaster);
            right[i] = std::tanh (r * smoothedMaster);
        }
    }

    void RhythmEngine::renderBlock (const Pattern& pattern, const Settings& s, const BlockTiming& timing,
                                    float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0) return;

        if (! smoothersInitialised)
        {
            for (int i = 0; i < numInstruments; ++i) smoothedLevels[i] = s.levels[i];
            smoothedMaster = s.masterGain;
            smoothersInitialised = true;
        }

        const int numEvents = collectEvents (pattern, s, timing, numSamples);

        int pos = 0;
        for (int e = 0; e < numEvents; ++e)
        {
            const Event& ev = events[(size_t) e];
            if (ev.sample > pos)
            {
                renderSegment (s, left, right, pos, ev.sample);
                pos = ev.sample;
            }
            kit.trigger (ev.inst, ev.velocity);
        }

        renderSegment (s, left, right, pos, numSamples);
    }
}
