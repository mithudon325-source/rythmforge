#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "RhythmTypes.h"

// Small, allocation-free drum synthesiser. Everything is computed from oscillators,
// noise and envelopes: no samples are used.
namespace rf
{
    constexpr double kPi    = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;

    // Deterministic white noise (xorshift32). Same noise every render -> repeatable exports.
    class NoiseSource
    {
    public:
        void reseed (uint32_t s) noexcept { state = s != 0 ? s : 1u; }

        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) (int32_t) state * (1.0f / 2147483648.0f);
        }

    private:
        uint32_t state = 0x12345678u;
    };

    // Topology-preserving state variable filter.
    class SvFilter
    {
    public:
        void set (double cutoffHz, double q, double sampleRate) noexcept
        {
            const double fc = std::min (cutoffHz, 0.45 * sampleRate);
            const double g  = std::tan (kPi * fc / sampleRate);
            k  = (float) (1.0 / q);
            a1 = (float) (1.0 / (1.0 + g * (g + (double) k)));
            a2 = (float) g * a1;
            a3 = (float) g * a2;
        }

        void reset() noexcept { ic1 = ic2 = 0.0f; }

        // returns the high-pass output, writes band-pass to bpOut
        float processHigh (float x, float& bpOut) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            bpOut = v1;
            return x - k * v1 - v2;
        }

    private:
        float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    };

    // 808-style kick: sine with a fast downward pitch sweep, plus a short click.
    class KickVoice
    {
    public:
        void prepare (double sr) noexcept
        {
            sampleRate = sr;
            ampDecay   = (float) std::exp (-1.0 / (0.110 * sr));
            pitchDecay = (float) std::exp (-1.0 / (0.030 * sr));
            clickDecay = (float) std::exp (-1.0 / (0.004 * sr));
            noise.reseed (0xA341316Cu);
            reset();
        }

        void reset() noexcept { active = false; ampEnv = pitchEnv = clickEnv = 0; phase = 0; clickLp = 0; }

        void trigger (float vel) noexcept
        {
            velocity = vel; ampEnv = 1.0f; pitchEnv = 1.0f; clickEnv = 1.0f; phase = 0.0; active = true;
        }

        float process() noexcept
        {
            if (! active) return 0.0f;

            const double freq = 46.0 + 130.0 * (double) pitchEnv;
            phase += freq / sampleRate;
            if (phase >= 1.0) phase -= 1.0;

            const float body = (float) std::sin (kTwoPi * phase);
            clickLp += 0.35f * (noise.next() - clickLp);

            const float out = std::tanh (1.7f * body) * ampEnv + 0.35f * clickLp * clickEnv;

            ampEnv   *= ampDecay;
            pitchEnv *= pitchDecay;
            clickEnv *= clickDecay;
            if (ampEnv < 1.0e-4f) active = false;

            return out * velocity * 0.9f;
        }

    private:
        double sampleRate = 44100.0, phase = 0.0;
        float ampEnv = 0, pitchEnv = 0, clickEnv = 0, clickLp = 0, velocity = 0;
        float ampDecay = 0.999f, pitchDecay = 0.99f, clickDecay = 0.9f;
        bool active = false;
        NoiseSource noise;
    };

    // Snare: two pitched sine "shell" tones plus high-passed noise "wires".
    class SnareVoice
    {
    public:
        void prepare (double sr) noexcept
        {
            sampleRate = sr;
            toneDecay  = (float) std::exp (-1.0 / (0.050 * sr));
            noiseDecay = (float) std::exp (-1.0 / (0.085 * sr));
            filter.set (1800.0, 0.8, sr);
            noise.reseed (0x9E3779B9u);
            reset();
        }

        void reset() noexcept { active = false; toneEnv = noiseEnv = 0; p1 = p2 = 0; filter.reset(); }

        void trigger (float vel) noexcept
        {
            velocity = vel; toneEnv = 1.0f; noiseEnv = 1.0f; p1 = p2 = 0.0; active = true;
        }

        float process() noexcept
        {
            if (! active) return 0.0f;

            const double pitch = 1.0 + 0.5 * (double) toneEnv;
            p1 += 185.0 * pitch / sampleRate; if (p1 >= 1.0) p1 -= 1.0;
            p2 += 330.0 * pitch / sampleRate; if (p2 >= 1.0) p2 -= 1.0;

            const float tone = 0.6f * (float) std::sin (kTwoPi * p1) + 0.4f * (float) std::sin (kTwoPi * p2);

            float bp = 0.0f;
            const float wires = filter.processHigh (noise.next(), bp);

            const float out = 0.55f * tone * toneEnv + 0.80f * wires * noiseEnv;

            toneEnv  *= toneDecay;
            noiseEnv *= noiseDecay;
            if (noiseEnv < 1.0e-4f && toneEnv < 1.0e-4f) active = false;

            return out * velocity * 0.6f;
        }

    private:
        double sampleRate = 44100.0, p1 = 0, p2 = 0;
        float toneEnv = 0, noiseEnv = 0, velocity = 0, toneDecay = 0.99f, noiseDecay = 0.99f;
        bool active = false;
        SvFilter filter;
        NoiseSource noise;
    };

    // Hi-hat: six detuned square oscillators (classic metallic hat recipe) plus noise, high-passed.
    // The same class makes the closed and open hat; they only differ in decay time.
    class HatVoice
    {
    public:
        void prepare (double sr, bool isOpen) noexcept
        {
            sampleRate = sr;
            decay      = (float) std::exp (-1.0 / ((isOpen ? 0.140 : 0.018) * sr));
            chokeDecay = (float) std::exp (-1.0 / (0.004 * sr));
            filter.set (7000.0, 1.1, sr);
            noise.reseed (isOpen ? 0xC0FFEE11u : 0xBADC0DE5u);
            reset();
        }

        void reset() noexcept
        {
            active = false; env = 0; choking = false; filter.reset();
            for (auto& p : phases) p = 0.0;
        }

        void trigger (float vel) noexcept { velocity = vel; env = 1.0f; choking = false; active = true; }

        // Called when a closed hat (or another open hat) cuts this voice off.
        void choke() noexcept { if (active) choking = true; }

        float process() noexcept
        {
            if (! active) return 0.0f;

            static constexpr double freqs[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            float metal = 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                phases[i] += freqs[i] / sampleRate;
                if (phases[i] >= 1.0) phases[i] -= 1.0;
                metal += phases[i] < 0.5 ? 1.0f : -1.0f;
            }

            float bp = 0.0f;
            const float sig = filter.processHigh (metal * 0.17f + noise.next() * 0.35f, bp);
            const float out = sig * env * velocity * 0.45f;

            env *= choking ? chokeDecay : decay;
            if (env < 1.0e-4f) active = false;

            return out;
        }

    private:
        double sampleRate = 44100.0, phases[6] {};
        float env = 0, velocity = 0, decay = 0.99f, chokeDecay = 0.9f;
        bool active = false, choking = false;
        SvFilter filter;
        NoiseSource noise;
    };

    class DrumKit
    {
    public:
        void prepare (double sr) noexcept
        {
            kick.prepare (sr);
            snare.prepare (sr);
            closedHat.prepare (sr, false);
            openHat.prepare (sr, true);
        }

        void reset() noexcept { kick.reset(); snare.reset(); closedHat.reset(); openHat.reset(); }

        void trigger (int inst, float vel) noexcept
        {
            switch (inst)
            {
                case Kick:      kick.trigger (vel); break;
                case Snare:     snare.trigger (vel); break;
                case ClosedHat: openHat.choke(); closedHat.trigger (vel); break;
                case OpenHat:   openHat.trigger (vel); break;
                default: break;
            }
        }

        void process (float* out) noexcept
        {
            out[Kick]      = kick.process();
            out[Snare]     = snare.process();
            out[ClosedHat] = closedHat.process();
            out[OpenHat]   = openHat.process();
        }

    private:
        KickVoice kick;
        SnareVoice snare;
        HatVoice closedHat, openHat;
    };
}
