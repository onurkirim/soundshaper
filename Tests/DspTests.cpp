/*
    DSP self-tests (console app, no audio hardware needed).

    Build with -DNOISESHAPER_BUILD_TESTS=ON and run NoiseShaperTests.

    1. Bypass transparency: with the spectral path disabled, the STFT
       overlap-add chain must be a pure delay. Finds the lag and residual.
    2. Constant RMS: with heavy spectral shaping engaged, output RMS must
       match input RMS (Parseval renormalisation).
    3. Noise slope: estimates the spectral slope of the noise source via
       Welch averaging in octave bands, for white / pink / brown settings.
*/

#include "../Source/DSP/NoiseSource.h"
#include "../Source/DSP/SpectralShaper.h"
#include "../Source/DSP/TemporalShaper.h"

#include <cstdio>
#include <vector>
#include <cmath>

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}

int main()
{
    constexpr double fs = 48000.0;

    // ------------------------------------------------------------------
    std::printf ("1) Spectral path bypass: pure delay?\n");
    {
        SpectralShaper s;
        s.prepare (fs);
        s.setParams (false, SpectralShaper::Func::sine, 3.0f, 18.0f, 1.0f);

        const int n = 48000;
        std::vector<float> in ((size_t) n), out ((size_t) n);
        juce::Random r (42);
        for (int i = 0; i < n; ++i)
        {
            in[(size_t) i]  = r.nextFloat() * 2.0f - 1.0f;
            out[(size_t) i] = s.processSample (in[(size_t) i]);
        }

        int bestLag = 0;
        double bestErr = 1.0e18;
        for (int lag = 0; lag <= 2 * SpectralShaper::fftSize; ++lag)
        {
            double err = 0.0;
            int cnt = 0;
            for (int i = 3 * SpectralShaper::fftSize; i < n; ++i)
            {
                const double d = (double) out[(size_t) i] - (double) in[(size_t) (i - lag)];
                err += d * d;
                ++cnt;
            }
            err /= cnt;
            if (err < bestErr) { bestErr = err; bestLag = lag; }
        }

        std::printf ("  best lag = %d samples (reported latency = %d), residual MSE = %.3e\n",
                     bestLag, SpectralShaper::latency, bestErr);
        check (bestErr < 1.0e-6, "bypass residual < 1e-6");
        check (bestLag == SpectralShaper::latency, "measured lag equals reported latency");
    }

    // ------------------------------------------------------------------
    std::printf ("2) Spectral shaping: constant RMS (Parseval renormalisation)?\n");
    {
        SpectralShaper s;
        s.prepare (fs);
        s.setParams (true, SpectralShaper::Func::sine, 4.0f, 30.0f, 1.0f);

        const int n = 4 * 48000, skip = 4 * SpectralShaper::fftSize;
        juce::Random r (7);
        double eIn = 0.0, eOut = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float x = r.nextFloat() * 2.0f - 1.0f;
            const float y = s.processSample (x);
            if (i >= skip) { eIn += (double) x * x; eOut += (double) y * y; }
        }
        const double ratioDb = 10.0 * std::log10 (eOut / eIn);
        std::printf ("  out/in energy ratio = %+.2f dB (depth 30 dB engaged)\n", ratioDb);
        check (std::abs (ratioDb) < 1.0, "output RMS within +/-1 dB of input");
    }

    // ------------------------------------------------------------------
    std::printf ("3) Noise source: spectral slope in dB/octave\n");
    {
        auto measureSlope = [] (float slopeDb)
        {
            NoiseSource ns;
            ns.prepare (fs);
            ns.setSlopeDbPerOct (slopeDb);

            for (int i = 0; i < 48000; ++i)   // let the AGC settle
                ns.nextSample();

            constexpr int frame = 8192, numFrames = 96;
            juce::dsp::FFT fft (13);
            std::vector<float> w ((size_t) frame), buf ((size_t) (2 * frame));
            std::vector<double> psd ((size_t) (frame / 2 + 1), 0.0);
            for (int i = 0; i < frame; ++i)
                w[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) frame));

            for (int f = 0; f < numFrames; ++f)
            {
                for (int i = 0; i < frame; ++i)
                    buf[(size_t) i] = ns.nextSample() * w[(size_t) i];
                std::fill (buf.begin() + frame, buf.end(), 0.0f);
                fft.performRealOnlyForwardTransform (buf.data(), true);
                for (int k = 0; k <= frame / 2; ++k)
                    psd[(size_t) k] += (double) buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)]
                                     + (double) buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)];
            }

            // mean level in octave bands 125..8000 Hz, then linear fit
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            int nb = 0;
            for (double fc = 125.0; fc <= 8000.0; fc *= 2.0)
            {
                const int k0 = (int) std::round (fc / std::sqrt (2.0) / fs * frame);
                const int k1 = (int) std::round (fc * std::sqrt (2.0) / fs * frame);
                double sum = 0.0;
                for (int k = k0; k <= k1; ++k)
                    sum += psd[(size_t) k];
                const double levelDb = 10.0 * std::log10 (sum / (k1 - k0 + 1));
                const double x = std::log2 (fc);
                sx += x; sy += levelDb; sxx += x * x; sxy += x * levelDb;
                ++nb;
            }
            return (nb * sxy - sx * sy) / (nb * sxx - sx * sx);   // dB per octave
        };

        for (float target : { 0.0f, -3.0f, -6.0f })
        {
            const double measured = measureSlope (target);
            std::printf ("  target %+5.1f dB/oct -> measured %+5.2f dB/oct\n", target, measured);
            check (std::abs (measured - target) < 0.8, "slope within +/-0.8 dB/oct of target");
        }
    }

    // ------------------------------------------------------------------
    std::printf ("4) Temporal path: modulation depth sanity\n");
    {
        TemporalShaper t;
        t.prepare (fs);
        t.setParams (true, TemporalShaper::Func::sine, 2.0f, 1.0f, 1.0f);

        float lo = 1.0e9f, hi = -1.0e9f;
        for (int i = 0; i < 48000; ++i)
        {
            const float g = t.processSample (1.0f);
            lo = std::min (lo, g);
            hi = std::max (hi, g);
        }
        std::printf ("  gain range at depth=1: [%.3f, %.3f]\n", lo, hi);
        check (lo < 0.02f && hi > 0.98f, "full-depth sine modulation spans ~[0, 1]");
    }

    std::printf ("\n%s (%d failure(s))\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
