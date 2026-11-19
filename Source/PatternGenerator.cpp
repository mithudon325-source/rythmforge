#include "PatternGenerator.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

namespace rf
{
namespace
{
    // splitmix64: tiny, fast, identical results on every compiler / platform.
    struct Rng
    {
        explicit Rng (uint64_t seed) : state (seed) {}

        uint64_t next() noexcept
        {
            state += 0x9E3779B97F4A7C15ULL;
            uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }

        float uniform() noexcept { return (float) ((double) (next() >> 40) / 16777216.0); }

        uint64_t state;
    };

    // Where the important hits live inside one bar, in quarter notes.
    struct Meter
    {
        int nPulses = 0; double pulses[8] {};
        int nKick   = 0; double kick[4]   {};
        int nSnare  = 0; double snare[4]  {};
    };

    void fill (std::initializer_list<double> list, double* dst, int& count)
    {
        count = 0;
        for (double v : list) dst[count++] = v;
    }

    Meter meterFor (int ts)
    {
        Meter m;
        switch (ts)
        {
            case 0: /* 2/4  */ fill ({ 0, 1 },               m.pulses, m.nPulses); fill ({ 0 },       m.kick, m.nKick); fill ({ 1 },       m.snare, m.nSnare); break;
            case 1: /* 3/4  */ fill ({ 0, 1, 2 },            m.pulses, m.nPulses); fill ({ 0 },       m.kick, m.nKick); fill ({ 1, 2 },    m.snare, m.nSnare); break;
            case 2: /* 4/4  */ fill ({ 0, 1, 2, 3 },         m.pulses, m.nPulses); fill ({ 0, 2 },    m.kick, m.nKick); fill ({ 1, 3 },    m.snare, m.nSnare); break;
            case 3: /* 5/4  */ fill ({ 0, 1, 2, 3, 4 },      m.pulses, m.nPulses); fill ({ 0, 3 },    m.kick, m.nKick); fill ({ 2, 4 },    m.snare, m.nSnare); break;
            case 4: /* 6/8  */ fill ({ 0, 1.5 },             m.pulses, m.nPulses); fill ({ 0 },       m.kick, m.nKick); fill ({ 1.5 },     m.snare, m.nSnare); break;
            case 5: /* 7/8  */ fill ({ 0, 1, 2 },            m.pulses, m.nPulses); fill ({ 0, 2 },    m.kick, m.nKick); fill ({ 1 },       m.snare, m.nSnare); break;
            case 6: /* 9/8  */ fill ({ 0, 1.5, 3 },          m.pulses, m.nPulses); fill ({ 0, 3 },    m.kick, m.nKick); fill ({ 1.5 },     m.snare, m.nSnare); break;
            default:/* 12/8 */ fill ({ 0, 1.5, 3, 4.5 },     m.pulses, m.nPulses); fill ({ 0, 3 },    m.kick, m.nKick); fill ({ 1.5, 4.5 }, m.snare, m.nSnare); break;
        }
        return m;
    }

    bool onGrid (double t, const double* positions, int n)
    {
        for (int i = 0; i < n; ++i)
            if (std::abs (t - positions[i]) < 1.0e-6) return true;
        return false;
    }

    // 2 = main pulse, 1 = eighth-note-ish position, 0 = everything finer than that.
    int levelOf (double t, const Meter& m, int spq)
    {
        const double eps = 1.0e-6;
        for (int i = 0; i < m.nPulses; ++i)
            if (std::abs (t - m.pulses[i]) < eps) return 2;

        const double f = t - std::floor (t + eps);
        if (std::abs (f) < eps) return 1;
        if (std::abs (f - 0.5) < eps) return 1;
        if (spq == 3 || spq == 6)
        {
            if (std::abs (f - 2.0 / 3.0) < eps) return 1;
            if (spq == 6 && std::abs (f - 1.0 / 3.0) < eps) return 1;
        }
        return 0;
    }

    bool isOffbeat (double t)
    {
        const double eps = 1.0e-6;
        const double f = t - std::floor (t + eps);
        return std::abs (f - 0.5) < eps || std::abs (f - 2.0 / 3.0) < eps;
    }

    // Probabilities indexed by level (0 = fine, 1 = eighth, 2 = pulse).
    struct StyleDef
    {
        float kick[3]  { 0, 0, 0 };   // extra kicks away from the kick grid
        float ghost[3] { 0, 0, 0 };   // ghost snares
        float hat[3]   { 0, 0, 0 };   // closed hat
        float open[3]  { 0, 0, 0 };   // open hat
        bool  fourOnFloor = false;
        bool  halfTime = false;
        bool  hatsOnPulsesOnly = false;
        bool  openOffbeats = false;
    };

    void set3 (float* dst, float a, float b, float c) { dst[0] = a; dst[1] = b; dst[2] = c; }

    StyleDef styleFor (int style, Rng& rng)
    {
        StyleDef d;
        switch (style)
        {
            case 0: // Rock
                set3 (d.kick,  0.04f, 0.18f, 0.10f);
                set3 (d.ghost, 0.02f, 0.04f, 0.00f);
                set3 (d.hat,   0.35f, 1.00f, 1.00f);
                set3 (d.open,  0.00f, 0.12f, 0.00f);
                break;
            case 1: // Funk
                set3 (d.kick,  0.25f, 0.30f, 0.12f);
                set3 (d.ghost, 0.30f, 0.20f, 0.00f);
                set3 (d.hat,   0.80f, 1.00f, 1.00f);
                set3 (d.open,  0.05f, 0.12f, 0.00f);
                break;
            case 2: // Disco
                set3 (d.kick,  0.00f, 0.00f, 0.00f);
                set3 (d.ghost, 0.00f, 0.00f, 0.00f);
                set3 (d.hat,   0.10f, 0.00f, 1.00f);
                d.fourOnFloor = true;
                d.openOffbeats = true;
                break;
            case 3: // Hip-Hop (half time)
                set3 (d.kick,  0.18f, 0.25f, 0.10f);
                set3 (d.ghost, 0.10f, 0.05f, 0.00f);
                set3 (d.hat,   0.45f, 1.00f, 1.00f);
                set3 (d.open,  0.00f, 0.06f, 0.00f);
                d.halfTime = true;
                break;
            case 4: // Minimal
                set3 (d.hat,   0.00f, 0.00f, 0.85f);
                d.hatsOnPulsesOnly = true;
                break;
            default: // Chaos: every seed gets its own random personality
                for (int i = 0; i < 3; ++i)
                {
                    d.kick[i]  = 0.50f * rng.uniform();
                    d.ghost[i] = 0.40f * rng.uniform();
                    d.hat[i]   = 0.20f + 0.80f * rng.uniform();
                    d.open[i]  = 0.30f * rng.uniform();
                }
                break;
        }
        return d;
    }
}

GeneratedPattern generatePattern (const GeneratorSettings& gs)
{
    GeneratedPattern out;
    std::memset (&out, 0, sizeof (out));

    const int ts  = clampIndex (gs.timeSig, numTimeSignatures);
    const int sd  = clampIndex (gs.subdiv, numSubdivisions);
    const int spq = stepsPerQuarter (sd);
    const int steps = stepsPerBar (ts, sd);
    const double stepLen = 1.0 / (double) spq;

    Meter meter = meterFor (ts);
    Rng rng ((uint64_t) 0x5EEDC0DE + (uint64_t) (gs.seed < 0 ? 0 : gs.seed) * (uint64_t) 7919);
    const StyleDef style = styleFor (clampIndex (gs.style, numStyles), rng);

    if (style.halfTime)
    {
        if (meter.nKick > 1) meter.snare[0] = meter.kick[1];
        meter.nSnare = 1;
        meter.nKick = 1;
    }

    const float density = std::max (0.0f, std::min (1.0f, gs.density));
    const float accent  = std::max (0.0f, std::min (1.0f, gs.accent));
    const float f = 0.3f + 1.4f * density;     // density scale for everything that is not a core hit

    // The same number of random numbers is consumed per step no matter which branch is taken,
    // so raising density only ever adds hits to the same underlying pattern.
    for (int k = 0; k < steps; ++k)
    {
        const double t = (double) k * stepLen;
        const int level = levelOf (t, meter, spq);

        const float uKick = rng.uniform(), uGhost = rng.uniform(), uHat = rng.uniform(),
                    uOpen = rng.uniform(), uA1 = rng.uniform(), uA2 = rng.uniform(), uA3 = rng.uniform();

        bool kickGrid  = style.fourOnFloor ? (level == 2) : onGrid (t, meter.kick, meter.nKick);
        const bool snareGrid = onGrid (t, meter.snare, meter.nSnare);
        if (k == 0) kickGrid = true;

        bool kick = false, snare = false, ghostSnare = false, chh = false, ohh = false;

        if (kickGrid) kick = (density >= 0.08f) || k == 0;
        else          kick = uKick < style.kick[level] * f;

        if (snareGrid)
            snare = density >= 0.05f;
        else if (! kick && uGhost < style.ghost[level] * f)
            snare = ghostSnare = true;

        if (style.hatsOnPulsesOnly)
            chh = (level == 2) && uHat < (0.55f + 0.45f * density);
        else
        {
            float p = style.hat[level];
            if (level < 2) p = std::min (1.0f, p * f);
            chh = uHat < p;
        }

        if (style.openOffbeats)
            ohh = isOffbeat (t) && density > 0.25f;
        else
            ohh = level < 2 && uOpen < style.open[level] * f;

        if (ohh) chh = false;

        if (kick)  out.cells[0][Kick][k]  = (kickGrid ? uA1 < accent * 0.7f : uA1 < accent * 0.2f) ? CellAccent : CellNormal;
        if (snare) out.cells[0][Snare][k] = ghostSnare ? CellGhost : (uA2 < accent ? CellAccent : CellNormal);
        if (chh)
        {
            if      (level == 2) out.cells[0][ClosedHat][k] = (uA3 < accent * 0.6f) ? CellAccent : CellNormal;
            else if (level == 0) out.cells[0][ClosedHat][k] = (uA3 < 0.6f) ? CellGhost : CellNormal;
            else                 out.cells[0][ClosedHat][k] = CellNormal;
        }
        if (ohh) out.cells[0][OpenHat][k] = (uA3 < accent) ? CellAccent : CellNormal;
    }

    // ---- fill layer: copy of main, with the second half of the bar replaced ----------------------
    for (int i = 0; i < numInstruments; ++i)
        for (int k = 0; k < maxSteps; ++k)
            out.cells[1][i][k] = out.cells[0][i][k];

    const float fillAmt = std::max (0.0f, std::min (1.0f, gs.fillAmount));
    if (fillAmt > 0.001f)
    {
        Rng frng ((uint64_t) 0xF111 + (uint64_t) (gs.seed < 0 ? 0 : gs.seed) * (uint64_t) 104729);
        const int fillStart = steps / 2;

        for (int k = fillStart; k < steps; ++k)
        {
            for (int i = 0; i < numInstruments; ++i) out.cells[1][i][k] = 0;

            const double t = (double) k * stepLen;
            const int level = levelOf (t, meter, spq);
            const float uSn = frng.uniform(), uKk = frng.uniform(), uHh = frng.uniform(), uAc = frng.uniform();
            const float progress = (float) (k - fillStart + 1) / (float) (steps - fillStart);

            float pSnare = fillAmt * (0.40f + 0.60f * progress);
            if (level == 2) pSnare = std::min (1.0f, pSnare * 1.3f);
            if (level == 0) pSnare *= 0.8f;

            const bool lastStep = (k == steps - 1);
            const bool snare = (uSn < pSnare) || (lastStep && fillAmt >= 0.3f);

            if (snare)
                out.cells[1][Snare][k] = (lastStep || (progress > 0.7f && uAc < 0.5f)) ? CellAccent : CellNormal;
            else
            {
                if (level == 2 && uKk < 0.35f * fillAmt) out.cells[1][Kick][k] = CellNormal;
                if (level == 2 && uHh < 0.6f * (1.0f - fillAmt)) out.cells[1][ClosedHat][k] = CellNormal;
            }
        }
    }

    return out;
}

} // namespace rf
