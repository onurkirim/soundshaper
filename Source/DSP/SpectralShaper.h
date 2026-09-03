#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <memory>
#include <vector>

/*
    SpectralShaper (the "spectral path" / pitch percept)
    ----------------------------------------------------
    STFT overlap-add processor (sqrt-Hann analysis + synthesis windows,
    75% overlap). A gain curve G(f), defined over a logarithmic frequency
    axis (20 Hz .. Nyquist), reshapes the magnitude spectrum:

        gainDb(f) = depthDb * shape( p(f) ),   p in [0, 1] (log-f position)

        shape (bipolar, in [-1, 1]):
          Sine : sin(2*pi * rate * p)                      -> spectral ripple
          Log  : 2 * ln(1 + rate*p) / ln(1 + rate) - 1     -> concave tilt
          Exp  : 2 * (e^(rate*p) - 1) / (e^rate - 1) - 1   -> convex tilt

    "Mix" interpolates the linear bin gain between 1 (dry) and the shaped
    value (wet). After shaping, each frame is renormalised so its total
    energy matches the input frame (Parseval / constant-RMS constraint),
    with the correction gain smoothed across frames.

    The processor ALWAYS runs, even when disabled (unity curve), so that
    plugin latency stays constant no matter which paths are toggled.
*/
class SpectralShaper
{
public:
    enum class Func { sine = 0, log = 1, exp = 2 };

    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;   // 2048
    static constexpr int hopSize  = fftSize / 4;     // 75% overlap
    static constexpr int numBins  = fftSize / 2 + 1;

    // Verified empirically by Tests/DspTests.cpp (bypass path lag).
    static constexpr int latency  = fftSize;

    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn;
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        window.resize ((size_t) fftSize);
        for (int n = 0; n < fftSize; ++n)   // periodic sqrt-Hann
            window[(size_t) n] = std::sqrt (0.5f * (1.0f - std::cos (
                juce::MathConstants<float>::twoPi * (float) n / (float) fftSize)));

        // COLA normalisation: sum of (analysis * synthesis) windows over hops
        colaNorm = 0.0f;
        for (int k = 0; k < fftSize / hopSize; ++k)
            colaNorm += window[(size_t) (k * hopSize)] * window[(size_t) (k * hopSize)];

        inFifo.assign  ((size_t) fftSize, 0.0f);
        outFifo.assign ((size_t) fftSize, 0.0f);
        fftData.assign ((size_t) (2 * fftSize), 0.0f);
        gainCurve.assign ((size_t) numBins, 1.0f);

        pos = 0;
        hopCount = 0;
        energyGain = 1.0f;
        curveDirty = true;
    }

    void setParams (bool enabledIn, Func funcIn, float rateIn, float depthDbIn, float mixIn)
    {
        if (enabledIn != enabled || funcIn != func
            || ! juce::approximatelyEqual (rateIn, rate)
            || ! juce::approximatelyEqual (depthDbIn, depthDb)
            || ! juce::approximatelyEqual (mixIn, mix))
        {
            enabled = enabledIn;
            func    = funcIn;
            rate    = rateIn;
            depthDb = depthDbIn;
            mix     = mixIn;
            curveDirty = true;
        }
    }

    float processSample (float x)
    {
        inFifo[(size_t) pos] = x;
        const float y = outFifo[(size_t) pos];
        outFifo[(size_t) pos] = 0.0f;

        pos = (pos + 1) % fftSize;

        if (++hopCount >= hopSize)
        {
            hopCount = 0;
            processFrame();
        }
        return y;
    }

private:
    void updateCurve()
    {
        const float fLo     = 20.0f;
        const float nyquist = (float) (sampleRate * 0.5);
        const float octaves = std::log2 (nyquist / fLo);

        for (int k = 0; k < numBins; ++k)
        {
            float g = 1.0f;

            if (enabled)
            {
                const float f = juce::jmax (fLo, (float) k * (float) sampleRate / (float) fftSize);
                const float p = juce::jlimit (0.0f, 1.0f, std::log2 (f / fLo) / octaves);

                float s = 0.0f; // bipolar shape in [-1, 1]
                switch (func)
                {
                    case Func::sine: s = std::sin (juce::MathConstants<float>::twoPi * rate * p); break;
                    case Func::log:  s = 2.0f * (std::log1p (rate * p) / std::log1p (rate)) - 1.0f; break;
                    case Func::exp:  s = 2.0f * ((std::exp (rate * p) - 1.0f)
                                                 / (std::exp (rate) - 1.0f)) - 1.0f; break;
                }

                const float wet = juce::Decibels::decibelsToGain (depthDb * s);
                g = (1.0f - mix) + mix * wet;
            }

            gainCurve[(size_t) k] = g;
        }
        curveDirty = false;
    }

    void processFrame()
    {
        if (curveDirty)
            updateCurve();

        // unwrap FIFO oldest-first, apply analysis window
        for (int n = 0; n < fftSize; ++n)
            fftData[(size_t) n] = inFifo[(size_t) ((pos + n) % fftSize)] * window[(size_t) n];

        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft->performRealOnlyForwardTransform (fftData.data(), true);

        // apply gain curve, tracking energies for constant-RMS renormalisation
        double eIn = 1.0e-12, eOut = 1.0e-12;
        for (int k = 0; k < numBins; ++k)
        {
            float& re = fftData[(size_t) (2 * k)];
            float& im = fftData[(size_t) (2 * k + 1)];

            const double e = (double) re * re + (double) im * im;
            eIn += e;

            const float g = gainCurve[(size_t) k];
            re *= g;
            im *= g;
            eOut += e * (double) g * (double) g;
        }

        const float targetGain = enabled ? (float) std::sqrt (eIn / eOut) : 1.0f;
        energyGain += 0.3f * (targetGain - energyGain);   // smooth across frames

        for (int k = 0; k < numBins; ++k)
        {
            fftData[(size_t) (2 * k)]     *= energyGain;
            fftData[(size_t) (2 * k + 1)] *= energyGain;
        }

        // rebuild the conjugate-symmetric half so the inverse is engine-agnostic
        for (int k = 1; k < fftSize / 2; ++k)
        {
            fftData[(size_t) (2 * (fftSize - k))]     =  fftData[(size_t) (2 * k)];
            fftData[(size_t) (2 * (fftSize - k) + 1)] = -fftData[(size_t) (2 * k + 1)];
        }

        fft->performRealOnlyInverseTransform (fftData.data());

        // synthesis window + overlap-add
        for (int n = 0; n < fftSize; ++n)
            outFifo[(size_t) ((pos + n) % fftSize)] += fftData[(size_t) n] * window[(size_t) n] / colaNorm;
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, inFifo, outFifo, fftData, gainCurve;

    double sampleRate = 48000.0;
    int    pos = 0, hopCount = 0;
    float  colaNorm = 1.0f, energyGain = 1.0f;

    bool  enabled = false;
    Func  func    = Func::sine;
    float rate    = 3.0f;
    float depthDb = 18.0f;
    float mix     = 1.0f;
    bool  curveDirty = true;
};
