#pragma once

#include "PluginProcessor.h"

// -------------------------------------------------------------------------
// A rotary slider with a caption, bound to an APVTS parameter.
// -------------------------------------------------------------------------
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& apvts,
                  const juce::String& paramID, const juce::String& caption)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        addAndMakeVisible (slider);

        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, paramID, slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
    }

private:
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// -------------------------------------------------------------------------
// One shaping path (spectral or temporal): enable toggle, function
// selector, and rate / depth / mix knobs.
// -------------------------------------------------------------------------
class PathSection : public juce::Component
{
public:
    PathSection (juce::AudioProcessorValueTreeState& apvts, int voiceIndex,
                 const juce::String& prefix, const juce::String& titleText,
                 const juce::String& rateCaption)
        : rate  (apvts, IDs::voice (voiceIndex, prefix + "_rate"),  rateCaption),
          depth (apvts, IDs::voice (voiceIndex, prefix + "_depth"), "Depth"),
          mix   (apvts, IDs::voice (voiceIndex, prefix + "_mix"),   "Mix")
    {
        title.setText (titleText, juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centredLeft);
        title.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        addAndMakeVisible (title);

        onButton.setButtonText ("On");
        addAndMakeVisible (onButton);
        onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, IDs::voice (voiceIndex, prefix + "_on"), onButton);

        funcBox.addItemList ({ "Sine", "Log", "Exp" }, 1);
        addAndMakeVisible (funcBox);
        funcAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, IDs::voice (voiceIndex, prefix + "_func"), funcBox);

        addAndMakeVisible (rate);
        addAndMakeVisible (depth);
        addAndMakeVisible (mix);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);

        auto top = r.removeFromTop (22);
        onButton.setBounds (top.removeFromRight (52));
        title.setBounds (top);

        funcBox.setBounds (r.removeFromTop (24).reduced (0, 1));
        r.removeFromTop (4);

        const int w = r.getWidth() / 3;
        rate.setBounds  (r.removeFromLeft (w));
        depth.setBounds (r.removeFromLeft (w));
        mix.setBounds   (r);
    }

private:
    juce::Label        title;
    juce::ToggleButton onButton;
    juce::ComboBox     funcBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   onAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> funcAttachment;

public:
    LabelledKnob rate, depth, mix;
};

// -------------------------------------------------------------------------
// One voice strip: voice-on toggle, noise slope knob, spectral + temporal
// path sections.
// -------------------------------------------------------------------------
class VoiceStrip : public juce::Component
{
public:
    VoiceStrip (juce::AudioProcessorValueTreeState& apvts, int v)
        : slope    (apvts, IDs::voice (v, "noise_slope"), "Noise slope (dB/oct)"),
          spectral (apvts, v, "spec", "Spectral (pitch)",  "Density"),
          temporal (apvts, v, "temp", "Temporal (rhythm)", "Rate (Hz)")
    {
        header.setText ("Voice " + juce::String (v + 1), juce::dontSendNotification);
        header.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        addAndMakeVisible (header);

        onButton.setButtonText ("On");
        addAndMakeVisible (onButton);
        onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, IDs::voice (v, "on"), onButton);

        addAndMakeVisible (slope);
        addAndMakeVisible (spectral);
        addAndMakeVisible (temporal);
    }

    // In link mode voices 2-4 follow Voice 1's shaping, so their shaping
    // controls are greyed out; the per-voice On toggle stays live.
    void setShapingEnabled (bool e)
    {
        for (auto* c : { (juce::Component*) &slope,
                         (juce::Component*) &spectral,
                         (juce::Component*) &temporal })
        {
            c->setEnabled (e);
            c->setAlpha (e ? 1.0f : 0.4f);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);

        auto top = r.removeFromTop (24);
        onButton.setBounds (top.removeFromRight (56));
        header.setBounds (top);

        slope.setBounds (r.removeFromTop (96).reduced (28, 0));
        r.removeFromTop (6);

        spectral.setBounds (r.removeFromTop ((r.getHeight() - 6) / 2));
        r.removeFromTop (6);
        temporal.setBounds (r);
    }

private:
    juce::Label        header;
    juce::ToggleButton onButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

public:
    LabelledKnob slope;
    PathSection  spectral, temporal;
};

// -------------------------------------------------------------------------
// Main editor: header (title, link toggle, master gain) + 4 voice strips.
// -------------------------------------------------------------------------
class NoiseShaperAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit NoiseShaperAudioProcessorEditor (NoiseShaperAudioProcessor& p)
        : AudioProcessorEditor (&p), processor (p),
          masterKnob (p.apvts, IDs::masterGain, "Master (dB)")
    {
        title.setText ("NoiseShaper", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
        addAndMakeVisible (title);

        linkButton.setButtonText ("Link voices (Voice 1 controls all shaping)");
        addAndMakeVisible (linkButton);
        linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            p.apvts, IDs::link, linkButton);

        addAndMakeVisible (masterKnob);

        for (int v = 0; v < NoiseShaperAudioProcessor::numVoices; ++v)
        {
            strips.push_back (std::make_unique<VoiceStrip> (p.apvts, v));
            addAndMakeVisible (*strips.back());
        }

        setSize (1020, 640);
        startTimerHz (10);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff181a20));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);

        auto top = r.removeFromTop (70);
        title.setBounds (top.removeFromLeft (180));
        masterKnob.setBounds (top.removeFromRight (110));
        linkButton.setBounds (top.reduced (10, 22));

        r.removeFromTop (6);

        const int gap = 8;
        const int w   = (r.getWidth() - 3 * gap) / 4;
        for (int v = 0; v < (int) strips.size(); ++v)
            strips[(size_t) v]->setBounds (r.getX() + v * (w + gap), r.getY(), w, r.getHeight());
    }

private:
    void timerCallback() override
    {
        const bool link = processor.apvts.getRawParameterValue (IDs::link)->load() > 0.5f;
        for (size_t v = 1; v < strips.size(); ++v)
            strips[v]->setShapingEnabled (! link);
    }

    NoiseShaperAudioProcessor& processor;

    juce::Label        title;
    juce::ToggleButton linkButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttachment;
    LabelledKnob       masterKnob;

    std::vector<std::unique_ptr<VoiceStrip>> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseShaperAudioProcessorEditor)
};
