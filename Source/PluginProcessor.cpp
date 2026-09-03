#include "PluginProcessor.h"
#include "PluginEditor.h"

NoiseShaperAudioProcessor::NoiseShaperAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    linkParam       = apvts.getRawParameterValue (IDs::link);
    masterGainParam = apvts.getRawParameterValue (IDs::masterGain);

    for (int v = 0; v < numVoices; ++v)
    {
        auto& p = voiceParams[v];
        p.on        = apvts.getRawParameterValue (IDs::voice (v, "on"));
        p.slope     = apvts.getRawParameterValue (IDs::voice (v, "noise_slope"));
        p.specOn    = apvts.getRawParameterValue (IDs::voice (v, "spec_on"));
        p.specFunc  = apvts.getRawParameterValue (IDs::voice (v, "spec_func"));
        p.specRate  = apvts.getRawParameterValue (IDs::voice (v, "spec_rate"));
        p.specDepth = apvts.getRawParameterValue (IDs::voice (v, "spec_depth"));
        p.specMix   = apvts.getRawParameterValue (IDs::voice (v, "spec_mix"));
        p.tempOn    = apvts.getRawParameterValue (IDs::voice (v, "temp_on"));
        p.tempFunc  = apvts.getRawParameterValue (IDs::voice (v, "temp_func"));
        p.tempRate  = apvts.getRawParameterValue (IDs::voice (v, "temp_rate"));
        p.tempDepth = apvts.getRawParameterValue (IDs::voice (v, "temp_depth"));
        p.tempMix   = apvts.getRawParameterValue (IDs::voice (v, "temp_mix"));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout NoiseShaperAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { IDs::link, 1 }, "Link Voices", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { IDs::masterGain, 1 }, "Master Gain",
        NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f));

    const StringArray funcNames { "Sine", "Log", "Exp" };

    for (int v = 0; v < numVoices; ++v)
    {
        auto id = [v] (const String& s) { return ParameterID { IDs::voice (v, s), 1 }; };
        auto nm = [v] (const String& s) { return "V" + String (v + 1) + " " + s; };

        layout.add (std::make_unique<AudioParameterBool> (id ("on"), nm ("On"), v == 0));

        // white = 0, pink = -3, brown = -6 dB/oct; continuously adjustable
        layout.add (std::make_unique<AudioParameterFloat> (
            id ("noise_slope"), nm ("Noise Slope"),
            NormalisableRange<float> (-9.0f, 3.0f, 0.01f), -3.0f));

        // ----- spectral path (pitch) -----
        layout.add (std::make_unique<AudioParameterBool>   (id ("spec_on"),   nm ("Spectral On"), true));
        layout.add (std::make_unique<AudioParameterChoice> (id ("spec_func"), nm ("Spectral Function"), funcNames, 0));
        layout.add (std::make_unique<AudioParameterFloat>  (id ("spec_rate"), nm ("Spectral Rate"),
            NormalisableRange<float> (0.25f, 16.0f, 0.01f, 0.5f), 3.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (id ("spec_depth"), nm ("Spectral Depth"),
            NormalisableRange<float> (0.0f, 36.0f, 0.1f), 18.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (id ("spec_mix"), nm ("Spectral Mix"),
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

        // ----- temporal path (rhythm) -----
        layout.add (std::make_unique<AudioParameterBool>   (id ("temp_on"),   nm ("Temporal On"), true));
        layout.add (std::make_unique<AudioParameterChoice> (id ("temp_func"), nm ("Temporal Function"), funcNames, 0));
        layout.add (std::make_unique<AudioParameterFloat>  (id ("temp_rate"), nm ("Temporal Rate"),
            NormalisableRange<float> (0.05f, 200.0f, 0.0f, 0.3f), 2.0f));
        layout.add (std::make_unique<AudioParameterFloat>  (id ("temp_depth"), nm ("Temporal Depth"),
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
        layout.add (std::make_unique<AudioParameterFloat>  (id ("temp_mix"), nm ("Temporal Mix"),
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    }

    return layout;
}

void NoiseShaperAudioProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& v : voices)
        v.prepare (sampleRate);

    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (masterGainParam->load()));

    setLatencySamples (SpectralShaper::latency);
}

bool NoiseShaperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void NoiseShaperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // "Link" makes every voice read Voice 1's shaping configuration
    // (noise slope + both paths). Voice on/off stays individual so you can
    // still choose how many linked voices run in parallel.
    const bool link = linkParam->load() > 0.5f;

    bool active[numVoices];
    for (int v = 0; v < numVoices; ++v)
    {
        const int s = link ? 0 : v;
        const auto& p = voiceParams[s];

        active[v] = voiceParams[v].on->load() > 0.5f;

        voices[v].noise.setSlopeDbPerOct (p.slope->load());

        voices[v].spectral.setParams (p.specOn->load() > 0.5f,
                                      (SpectralShaper::Func) (int) p.specFunc->load(),
                                      p.specRate->load(),
                                      p.specDepth->load(),
                                      p.specMix->load());

        voices[v].temporal.setParams (p.tempOn->load() > 0.5f,
                                      (TemporalShaper::Func) (int) p.tempFunc->load(),
                                      p.tempRate->load(),
                                      p.tempDepth->load(),
                                      p.tempMix->load());
    }

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterGainParam->load()));

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sum = 0.0f;
        for (int v = 0; v < numVoices; ++v)
            if (active[v])
                sum += voices[v].processSample();

        sum *= masterGain.getNextValue();

        left[i] = sum;
        if (right != nullptr)
            right[i] = sum;
    }
}

void NoiseShaperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NoiseShaperAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* NoiseShaperAudioProcessor::createEditor()
{
    return new NoiseShaperAudioProcessorEditor (*this);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoiseShaperAudioProcessor();
}
