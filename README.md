# NoiseShaper

Experimental noise-shaping / synthesis plugin (JUCE, C++17). Builds as a
**VST3** and as a **Standalone** application from the same CMake project.

Four parallel voices; each voice is an independent noise source feeding two
toggleable shaping paths, applied in a fixed order:

```
noise (adjustable spectral slope)
   └─> spectral path (pitch)  ── STFT gain curve over log-frequency
          └─> temporal path (rhythm) ── LFO-style amplitude modulation
                 └─> voice output ──> sum of active voices ──> master gain
```

## Signal chain details

**Noise source** — white noise through a cascade of first-order low-shelf
filters spaced one octave apart, giving a continuously adjustable spectral
slope in dB/octave: `0` = white, `-3` = pink, `-6` = brown (range −9…+3).
A slow RMS-tracking gain stage keeps output level constant across slopes,
and a ~5 Hz DC blocker removes infrasonic build-up at steep settings.

**Spectral path (pitch percept)** — an STFT overlap-add processor
(2048-point FFT, sqrt-Hann analysis/synthesis windows, 75% overlap). The
selected function (Sine / Log / Exp) defines a bipolar gain curve over a
logarithmic frequency axis (20 Hz…Nyquist); *Density* scales the curve's
argument (for Sine: ripples across the spectrum, which concentrates energy
into quasi-harmonic regions and produces the pitch-like percept), *Depth*
sets the curve's swing in dB, *Mix* interpolates bin gains between dry and
shaped. Each frame is renormalised to its input energy (Parseval /
constant-RMS constraint), so shaping changes spectral distribution, not
loudness.

**Temporal path (rhythm percept)** — a free-running modulator whose
function (Sine / Log ramp / Exp ramp) maps phase to a unipolar gain
envelope; *Rate* spans 0.05–200 Hz (rhythm at slow rates, approaching
roughness/pitch percepts above ~20 Hz), with *Depth* and *Mix* as usual.
The ramp shapes reset per cycle for a percussive onset, lightly smoothed
(~0.3 ms) to avoid clicks.

**Order and latency** — when both paths are enabled, spectral is applied
first, then temporal. The spectral STFT engine always runs (unity curve
when bypassed) so latency is constant — exactly **2048 samples**, reported
to the host — regardless of any toggle state; this keeps all four voices
sample-aligned when mixed.

**Voices and Link** — all 4 voices and all parameters are pre-allocated.
The **Link voices** toggle makes every voice follow Voice 1's shaping
configuration (noise slope + both paths); each voice's own *On* toggle
stays independent, so linked mode still lets you choose how many voices
run (their noise generators are independently seeded, so linked voices
decorrelate rather than duplicate). Unlinked, all four voices are fully
independent.

## Parameters / automation

All controls live in a single `AudioProcessorValueTreeState`
(`Source/PluginProcessor.cpp` → `createParameterLayout()`), so every knob
and toggle is host-automatable and included in saved state. IDs are
namespaced `v1_…v4_` plus global `link` and `master_gain`. The DSP
(`Source/DSP/`) never touches the parameter layer directly — the processor
reads atomic parameter values once per block and pushes them into the DSP
objects; the editor talks to parameters only through attachments. Function
type is an `AudioParameterChoice` (automatable), rendered as a dropdown in
the UI for legibility; swap the `ComboBox` for a detented rotary in
`PluginEditor.h` if you prefer knobs there too.

## Layout

```
CMakeLists.txt              targets: NoiseShaper (VST3 + Standalone), optional tests
Source/
  Params.h                  parameter ID scheme
  PluginProcessor.h/.cpp    parameter layout, link logic, voice summing, state
  PluginEditor.h            4 voice strips + link/master header
  DSP/
    NoiseSource.h           slope-adjustable noise (shelving cascade + AGC)
    SpectralShaper.h        STFT spectral path
    TemporalShaper.h        LFO temporal path
    Voice.h                 per-voice chain
Tests/DspTests.cpp          offline DSP self-tests (no audio hardware needed)
```

## Building

Requires CMake ≥ 3.22, a C++17 compiler, and internet on first configure
(JUCE 8.0.6 is fetched automatically via `FetchContent`; point the
`FetchContent_Declare` block at a local JUCE checkout to build offline).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artefacts land in `build/NoiseShaper_artefacts/Release/`:
`VST3/NoiseShaper.vst3` (copy into your VST3 folder, or set
`COPY_PLUGIN_AFTER_BUILD TRUE` in `juce_add_plugin`) and
`Standalone/NoiseShaper`.

Per-platform notes:

- **macOS** — Xcode command-line tools suffice; add `AU` to `FORMATS` in
  `CMakeLists.txt` if you also want an Audio Unit.
- **Windows** — Visual Studio 2022: `cmake -B build` then build the
  generated solution, or `cmake --build build --config Release`.
- **Linux** — install the JUCE dev packages first:
  `sudo apt install cmake pkg-config libasound2-dev libfreetype-dev
  libfontconfig1-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxcomposite-dev`

### DSP self-tests

```sh
cmake -B build -DNOISESHAPER_BUILD_TESTS=ON
cmake --build build --target NoiseShaperTests
./build/NoiseShaperTests_artefacts/Release/NoiseShaperTests
```

Verifies: (1) the bypassed spectral path is a pure 2048-sample delay,
(2) heavy spectral shaping preserves RMS (Parseval renormalisation),
(3) measured noise slopes match white/pink/brown targets, (4) temporal
modulation spans the expected gain range. All four pass as shipped.

## Max/MSP alternative (prototype only)

For rapid experimentation the same chain prototypes easily in Max:
`noise~` → `fffb~`/`pfft~` with a gain curve (spectral path) → `*~` driven
by a `phasor~`+function lookup (temporal path). That gets you audible
results in minutes, but it cannot provide a distributable VST3, host
automation, or the constant-latency/constant-RMS guarantees, which is why
the main implementation is a JUCE project.
