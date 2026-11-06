#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

class KompaLeadProcessor : public juce::AudioProcessor
{
public:
    KompaLeadProcessor();
    ~KompaLeadProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kompa Lead"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Params
    {
        float level, oscMix, pw, detune, sub;
        float cutoff, reso, envAmt;
        float glide, vibDepth, vibRate;
        bool  octOn; float octDown, octUp;
        bool  driveOn; float drive;
        bool  distOn;  float dist, distTone;
        bool  chorusOn; float chRate, chDepth, chMix;
    };

    void readParams();
    void noteOn (int note, float vel);
    void noteOff (int note);
    void renderSynth (float* out, int start, int num);
    void applyPedals (juce::AudioBuffer<float>& buffer);

    Params p {};

    // synth state
    double sr = 44100.0;
    std::vector<int> noteStack;
    float velocity = 1.0f;
    float curFreq = 440.0f, tgtFreq = 440.0f, glideCoef = 1.0f;
    float ph1 = 0, ph2 = 0, phSub = 0, lfoPh = 0, vibFade = 0;
    float detuneRatio = 1.0f;
    float bendSemis = 0.0f, modWheel = 0.0f;
    int filterCounter = 0;

    juce::ADSR ampEnv, fltEnv;
    juce::dsp::StateVariableTPTFilter<float> filter;

    // pedalboard state
    struct OctState { float lp = 0, prevLp = 0, ff = 1, env = 0, dn = 0, dcX = 0, dcY = 0; };
    OctState oct[2];
    float octDetA = 0.1f, octEnvA = 0.01f, octSoftA = 0.1f, distA = 0.5f;
    float distLp[2] { 0, 0 };
    juce::dsp::Chorus<float> chorus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KompaLeadProcessor)
};
