#pragma once

#include <cstdint>
#include "RhythmTypes.h"

namespace rf
{
    // Everything that influences pattern generation. Two equal settings structs always
    // produce exactly the same pattern on every platform (own PRNG, no <random> distributions).
    struct GeneratorSettings
    {
        int   timeSig   = 2;     // index into timeSignatures
        int   subdiv    = 1;     // index into subdivisions
        int   style     = 0;     // index into styleNames
        int   seed      = 1;
        int   fillEvery = 0;     // index into fillEveryValues (does not change the pattern itself)
        float density   = 0.5f;
        float accent    = 0.5f;
        float fillAmount = 0.5f;

        // Exact comparison is intended: we want to know whether *any* setting changed at all.
        bool operator== (const GeneratorSettings& o) const noexcept
        {
            return timeSig == o.timeSig && subdiv == o.subdiv && style == o.style && seed == o.seed
                   && fillEvery == o.fillEvery && ! (density < o.density || density > o.density)
                   && ! (accent < o.accent || accent > o.accent)
                   && ! (fillAmount < o.fillAmount || fillAmount > o.fillAmount);
        }
        bool operator!= (const GeneratorSettings& o) const noexcept { return ! (*this == o); }
    };

    struct GeneratedPattern
    {
        uint8_t cells[numLayers][numInstruments][maxSteps];
    };

    GeneratedPattern generatePattern (const GeneratorSettings& settings);
}
