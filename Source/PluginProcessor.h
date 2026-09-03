#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "DSP/Voice.h"

class NoiseShaperAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numVoices = 4;

    NoiseShaperAudioProcessor();
    ~NoiseShaperAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return JucePlugin_Name; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Public so the editor can create attachments; the DSP below never
    // touches UI, and the UI never touches DSP -- they meet only here.
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct VoiceParamRefs
    {
        std::atomic<float>* on        = nullptr;
        std::atomic<float>* slope     = nullptr;
        std::atomic<float>* specOn    = nullptr;
        std::atomic<float>* specFunc  = nullptr;
        std::atomic<float>* specRate  = nullptr;
        std::atomic<float>* specDepth = nullptr;
        std::atomic<float>* specMix   = nullptr;
        std::atomic<float>* tempOn    = nullptr;
        std::atomic<float>* tempFunc  = nullptr;
        std::atomic<float>* tempRate  = nullptr;
        std::atomic<float>* tempDepth = nullptr;
        std::atomic<float>* tempMix   = nullptr;
    };

    VoiceParamRefs voiceParams[numVoices];
    std::atomic<float>* linkParam       = nullptr;
    std::atomic<float>* masterGainParam = nullptr;

    Voice voices[numVoices];
    juce::SmoothedValue<float> masterGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseShaperAudioProcessor)
};
