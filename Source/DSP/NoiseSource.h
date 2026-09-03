#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

/*
    NoiseSource
    -----------
    White-noise generator followed by a cascade of first-order low-shelf
    filters spaced one octave apart. Each shelf contributes -slope dB of
    low-frequency gain, so the cascade approximates an arbitrary spectral
    slope in dB/octave:

        slope =  0  -> white  (flat)
        slope = -3  -> pink   (1/f power)
        slope = -6  -> brown  (1/f^2 power)

    The slope is continuously adjustable. A slow RMS-based automatic gain
    stage keeps the output level constant regardless of slope, and a
    one-pole DC blocker removes infrasonic build-up at steep slopes.
*/
class NoiseSource
{
public:
    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn;

        cornerFreqs.clear();
        for (double f = 40.0; f < sampleRate * 0.4; f *= 2.0)
            cornerFreqs.push_back (f);

        coeffs.assign (cornerFreqs.size(), {});
        states.assign (cornerFreqs.size(), {});

        dcR  = (float) std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sampleRate);
        dcX1 = dcY1 = 0.0f;

        meanSquare = targetRms * targetRms;
        agcGain    = 1.0f;

        rng.setSeedRandomly();
        updateCoefficients();
    }

    void setSlopeDbPerOct (float newSlopeDb)
    {
        if (! juce::approximatelyEqual (newSlopeDb, slopeDb))
        {
            slopeDb = newSlopeDb;
            updateCoefficients();
        }
    }

    float nextSample()
    {
        float x = rng.nextFloat() * 2.0f - 1.0f;

        // shelving cascade
        for (size_t i = 0; i < coeffs.size(); ++i)
        {
            const float y = coeffs[i].b0 * x + coeffs[i].b1 * states[i].x1
                                             - coeffs[i].a1 * states[i].y1;
            states[i].x1 = x;
            states[i].y1 = y;
            x = y;
        }

        // DC blocker (~5 Hz)
        const float hp = x - dcX1 + dcR * dcY1;
        dcX1 = x;
        dcY1 = hp;
        x = hp;

        // slow AGC: constant RMS for any slope setting
        meanSquare += rmsCoeff * (x * x - meanSquare);
        const float rms    = std::sqrt (juce::jmax (meanSquare, 1.0e-12f));
        const float target = juce::jlimit (0.01f, 100.0f, targetRms / rms);
        agcGain += agcCoeff * (target - agcGain);

        return x * agcGain;
    }

private:
    void updateCoefficients()
    {
        // Each low shelf boosts lows by (-slopeDb) dB, i.e. a negative
        // slope (pink/brown) means low frequencies gain energy relative
        // to highs, one shelf per octave.
        const double v0 = std::pow (10.0, -slopeDb / 20.0);

        for (size_t i = 0; i < cornerFreqs.size(); ++i)
        {
            const double K = std::tan (juce::MathConstants<double>::pi
                                       * cornerFreqs[i] / sampleRate);
            coeffs[i].b0 = (float) ((1.0 + K * v0) / (1.0 + K));
            coeffs[i].b1 = (float) ((K * v0 - 1.0)  / (1.0 + K));
            coeffs[i].a1 = (float) ((K - 1.0)       / (1.0 + K));
        }
    }

    struct Coef  { float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f; };
    struct State { float x1 = 0.0f, y1 = 0.0f; };

    std::vector<double> cornerFreqs;
    std::vector<Coef>   coeffs;
    std::vector<State>  states;

    juce::Random rng;
    double sampleRate = 48000.0;
    float  slopeDb    = 0.0f;

    float dcR = 0.999f, dcX1 = 0.0f, dcY1 = 0.0f;

    float meanSquare = 0.01f, agcGain = 1.0f;

    static constexpr float targetRms = 0.08f;   // per-voice level before master gain
    static constexpr float rmsCoeff  = 0.0001f; // ~200 ms energy tracker
    static constexpr float agcCoeff  = 0.0002f; // ~100 ms gain smoothing
};
