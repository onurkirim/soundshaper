#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/*
    Parameter IDs
    -------------
    All parameters live in a single AudioProcessorValueTreeState, so every
    knob and toggle is exposed to host automation and saved/restored with
    the plugin state. Per-voice parameters are namespaced "v1_" .. "v4_".

    Per voice (12 parameters, pre-allocated for all 4 voices):
        on            voice enable
        noise_slope   spectral slope of the noise source, dB/octave
        spec_on / spec_func / spec_rate / spec_depth / spec_mix
        temp_on / temp_func / temp_rate / temp_depth / temp_mix

    Global:
        link          all voices share Voice 1's shaping configuration
        master_gain   output gain in dB
*/
namespace IDs
{
    inline const juce::String link       = "link";
    inline const juce::String masterGain = "master_gain";

    inline juce::String voice (int voiceIndex, const juce::String& suffix)
    {
        return "v" + juce::String (voiceIndex + 1) + "_" + suffix;
    }
}
