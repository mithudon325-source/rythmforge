#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Shared constants, lookup tables and small helpers used by the generator,
// the audio engine, the processor and the editor.
namespace rf
{
    constexpr int numInstruments = 4;
    constexpr int maxSteps       = 64;   // 12/8 with 1/16 triplets needs 36, so 64 is safe
    constexpr int numLayers      = 2;    // 0 = main pattern, 1 = fill pattern

    enum Instrument { Kick = 0, Snare = 1, ClosedHat = 2, OpenHat = 3 };

    enum CellState : uint8_t { CellOff = 0, CellNormal = 1, CellAccent = 2, CellGhost = 3 };

    inline constexpr const char* instrumentNames[numInstruments] = { "Kick", "Snare", "Closed HH", "Open HH" };

    // ---- time signatures --------------------------------------------------
    struct TimeSignature { const char* name; int numerator; int denominator; };

    inline constexpr TimeSignature timeSignatures[] =
    {
        { "2/4", 2, 4 }, { "3/4", 3, 4 }, { "4/4", 4, 4 }, { "5/4", 5, 4 },
        { "6/8", 6, 8 }, { "7/8", 7, 8 }, { "9/8", 9, 8 }, { "12/8", 12, 8 }
    };
    inline constexpr int numTimeSignatures = 8;

    // ---- subdivisions -----------------------------------------------------
    struct Subdivision { const char* name; int stepsPerQuarter; };

    inline constexpr Subdivision subdivisions[] =
    {
        { "1/4  Quarter notes",        1 },
        { "1/8  Eighth notes",         2 },
        { "1/8T  Eighth triplets",     3 },
        { "1/16  Sixteenth notes",     4 },
        { "1/16T  Sixteenth triplets", 6 }
    };
    inline constexpr int numSubdivisions = 5;

    // ---- styles -----------------------------------------------------------
    inline constexpr const char* styleNames[] = { "Rock", "Funk", "Disco", "Hip-Hop", "Minimal", "Chaos" };
    inline constexpr int numStyles = 6;
    inline constexpr int styleChaos = 5;

    // ---- fills ------------------------------------------------------------
    inline constexpr const char* fillEveryNames[] = { "Off", "Every 2 bars", "Every 4 bars", "Every 8 bars" };
    inline constexpr int fillEveryValues[] = { 0, 2, 4, 8 };
    inline constexpr int numFillEvery = 4;

    // ---- export choices ---------------------------------------------------
    inline constexpr int exportSampleRates[] = { 44100, 48000, 88200, 96000 };
    inline constexpr int numExportSampleRates = 4;
    inline constexpr const char* exportBitDepthNames[] = { "16-bit PCM", "24-bit PCM", "32-bit float" };
    inline constexpr int exportBitDepths[] = { 16, 24, 32 };
    inline constexpr int numExportBitDepths = 3;

    // ---- helpers ----------------------------------------------------------
    inline int clampIndex (int i, int count) noexcept { return std::max (0, std::min (i, count - 1)); }

    inline double barLengthInQuarters (int tsIndex) noexcept
    {
        const auto& t = timeSignatures[clampIndex (tsIndex, numTimeSignatures)];
        return 4.0 * (double) t.numerator / (double) t.denominator;
    }

    inline int stepsPerQuarter (int sdIndex) noexcept
    {
        return subdivisions[clampIndex (sdIndex, numSubdivisions)].stepsPerQuarter;
    }

    inline double stepLengthInQuarters (int sdIndex) noexcept { return 1.0 / (double) stepsPerQuarter (sdIndex); }

    // Number of grid steps in one bar. When the bar is not a whole number of steps
    // (e.g. 7/8 with quarter-note steps = 3.5 steps) the last step is simply cut by the bar line.
    inline int stepsPerBar (int tsIndex, int sdIndex) noexcept
    {
        const double steps = barLengthInQuarters (tsIndex) * (double) stepsPerQuarter (sdIndex);
        return std::max (1, std::min (maxSteps, (int) std::ceil (steps - 1.0e-9)));
    }

    inline bool isCompoundMeter (int tsIndex) noexcept
    {
        const auto& t = timeSignatures[clampIndex (tsIndex, numTimeSignatures)];
        return t.denominator == 8 && (t.numerator % 3) == 0;
    }

    inline float cellVelocity (uint8_t c) noexcept
    {
        switch (c)
        {
            case CellAccent: return 1.0f;
            case CellNormal: return 0.72f;
            case CellGhost:  return 0.32f;
            default:         return 0.0f;
        }
    }

    // Parameter IDs
    namespace pid
    {
        inline constexpr const char* sync      = "sync";
        inline constexpr const char* play      = "play";
        inline constexpr const char* bpm       = "bpm";
        inline constexpr const char* timeSig   = "timesig";
        inline constexpr const char* subdiv    = "subdiv";
        inline constexpr const char* style     = "style";
        inline constexpr const char* density   = "density";
        inline constexpr const char* accent    = "accent";
        inline constexpr const char* swing     = "swing";
        inline constexpr const char* fillAmt   = "fillamt";
        inline constexpr const char* fillEvery = "fillevery";
        inline constexpr const char* seed      = "seed";
        inline constexpr const char* lock      = "lock";
        inline constexpr const char* kickLvl   = "kicklvl";
        inline constexpr const char* snareLvl  = "snarelvl";
        inline constexpr const char* chhLvl    = "chhlvl";
        inline constexpr const char* ohhLvl    = "ohhlvl";
        inline constexpr const char* master    = "master";
    }
}
