#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

/*
    TemporalShaper (the "temporal path" / rhythm percept)
    -----------------------------------------------------
    Time-domain amplitude modulator driven by a free-running phase in
    [0, 1). At slow rates (< ~10 Hz) the modulation is perceived as
    rhythm; above ~20 Hz it approaches roughness / pitch percepts.

        shape (unipolar, in [0, 1], per cycle):
          Sine : 0.5 * (1 + sin(2*pi*phase))
          Log  : ln(1 + 9*phase) / ln(10)        -> logarithmic ramp
          Exp  : (e^(4*phase) - 1) / (e^4 - 1)   -> exponential ramp

        gain = (1 - depth) + depth * shape
        out  = x * ((1 - mix) + mix * gain)

    The ramp shapes reset each cycle, giving a percussive onset; a light
    ~0.3 ms one-pole smoother rounds the discontinuity just enough to
    avoid harsh clicks without destroying fast (>20 Hz) modulation.
    The phase keeps running while bypassed so re-enabling stays in time.
*/
class TemporalShaper
{
public:
    enum class Func { sine = 0, log = 1, exp = 2 };

    void prepare (double sampleRateIn)
    {
        sampleRate  = sampleRateIn;
        phase       = 0.0;
        smoothCoeff = 1.0f - std::exp (-1.0f / (0.0003f * (float) sampleRate));
        gainSmoothed = 1.0f;
    }

    void setParams (bool enabledIn, Func funcIn, float rateHz, float depth01, float mix01)
    {
        enabled = enabledIn;
        func    = funcIn;
        rate    = rateHz;
        depth   = juce::jlimit (0.0f, 1.0f, depth01);
        mix     = juce::jlimit (0.0f, 1.0f, mix01);
    }

    float processSample (float x)
    {
        float g = 1.0f;

        if (enabled)
        {
            const float p = (float) phase;
            float shape = 0.0f;

            switch (func)
            {
                case Func::sine: shape = 0.5f * (1.0f + std::sin (juce::MathConstants<float>::twoPi * p)); break;
                case Func::log:  shape = std::log1p (9.0f * p) * invLn10; break;
                case Func::exp:  shape = (std::exp (expK * p) - 1.0f) * invExpKm1; break;
            }

            const float wet = (1.0f - depth) + depth * shape;
            g = (1.0f - mix) + mix * wet;
        }

        phase += rate / sampleRate;
        while (phase >= 1.0)
            phase -= 1.0;

        gainSmoothed += smoothCoeff * (g - gainSmoothed);
        return x * gainSmoothed;
    }

private:
    static constexpr float expK = 4.0f;
    inline static const float invLn10   = 1.0f / std::log (10.0f);
    inline static const float invExpKm1 = 1.0f / (std::exp (expK) - 1.0f);

    double sampleRate = 48000.0;
    double phase = 0.0;

    bool  enabled = false;
    Func  func    = Func::sine;
    float rate    = 2.0f;
    float depth   = 0.8f;
    float mix     = 1.0f;

    float smoothCoeff = 0.1f, gainSmoothed = 1.0f;
};
