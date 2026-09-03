#pragma once

#include "NoiseSource.h"
#include "SpectralShaper.h"
#include "TemporalShaper.h"

/*
    Voice
    -----
    One voice = its own noise source + its own spectral and temporal paths.

    Chain (fixed order, per the design spec):

        noise -> spectral path (pitch) -> temporal path (rhythm) -> out

    The spectral path always runs (unity curve when bypassed) so the STFT
    latency is identical for all voices and all toggle states; the temporal
    path is zero-latency, so its bypass is a plain unity gain.
*/
class Voice
{
public:
    void prepare (double sampleRate)
    {
        noise.prepare (sampleRate);
        spectral.prepare (sampleRate);
        temporal.prepare (sampleRate);
    }

    float processSample()
    {
        float x = noise.nextSample();
        x = spectral.processSample (x);   // spectral first...
        x = temporal.processSample (x);   // ...then temporal
        return x;
    }

    NoiseSource    noise;
    SpectralShaper spectral;
    TemporalShaper temporal;
};
