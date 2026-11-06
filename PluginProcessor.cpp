#include "PluginProcessor.h"
#include <cmath>
#include <algorithm>

namespace
{
    constexpr float kTwoPi = 6.28318530718f;

    inline float polyBlep (float t, float dt)
    {
        if (t < dt)        { t /= dt;             return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    inline float wrap01 (float x) { return x - std::floor (x); }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout KompaLeadProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addF = [&] (const juce::String& id, const juce::String& name,
                     float lo, float hi, float def, float skew = 1.0f, const juce::String& unit = {})
    {
        juce::NormalisableRange<float> range (lo, hi, 0.0f, skew);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addB = [&] (const juce::String& id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };

    // --- Synth (defaults = bright, slightly plucky kompa lead) ---
    addF ("level",   "Synth Level",          0.0f, 1.0f, 0.7f);
    addF ("oscMix",  "Synth Saw <-> Pulse",  0.0f, 1.0f, 0.45f);
    addF ("pw",      "Synth Pulse Width",    0.1f, 0.9f, 0.5f);
    addF ("detune",  "Synth Detune",         0.0f, 50.0f, 9.0f, 1.0f, "cents");
    addF ("sub",     "Synth Sub Octave",     0.0f, 1.0f, 0.2f);
    addF ("cutoff",  "Filter Cutoff",        100.0f, 16000.0f, 3200.0f, 0.3f, "Hz");
    addF ("reso",    "Filter Resonance",     0.0f, 1.0f, 0.2f);
    addF ("envAmt",  "Filter Env Amount",    0.0f, 1.0f, 0.55f);
    addF ("attack",  "Env Attack",           0.001f, 2.0f, 0.004f, 0.4f, "s");
    addF ("decay",   "Env Decay",            0.01f, 3.0f, 0.35f, 0.4f, "s");
    addF ("sustain", "Env Sustain",          0.0f, 1.0f, 0.65f);
    addF ("release", "Env Release",          0.01f, 3.0f, 0.22f, 0.4f, "s");
    addF ("glide",   "Glide Time",           0.0f, 500.0f, 40.0f, 0.5f, "ms");
    addF ("vibDepth","Vibrato Depth",        0.0f, 1.0f, 0.2f);
    addF ("vibRate", "Vibrato Rate",         2.0f, 9.0f, 5.5f, 1.0f, "Hz");

    // --- Pedalboard: Octave -> Drive -> Distortion -> Chorus ---
    addB ("octOn",   "Pedal Octave On",      false);
    addF ("octDown", "Pedal Octave Down",    0.0f, 1.0f, 0.5f);
    addF ("octUp",   "Pedal Octave Up",      0.0f, 1.0f, 0.0f);

    addB ("driveOn", "Pedal Drive On",       true);
    addF ("drive",   "Pedal Drive Amount",   0.0f, 1.0f, 0.3f);

    addB ("distOn",  "Pedal Distortion On",  false);
    addF ("dist",    "Pedal Distortion Amount", 0.0f, 1.0f, 0.4f);
    addF ("distTone","Pedal Distortion Tone",   500.0f, 12000.0f, 5000.0f, 0.4f, "Hz");

    addB ("chorusOn","Pedal Chorus On",      true);
    addF ("chRate",  "Pedal Chorus Rate",    0.1f, 5.0f, 0.9f, 0.5f, "Hz");
    addF ("chDepth", "Pedal Chorus Depth",   0.0f, 1.0f, 0.45f);
    addF ("chMix",   "Pedal Chorus Mix",     0.0f, 1.0f, 0.5f);

    return layout;
}

KompaLeadProcessor::KompaLeadProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KOMPA_STATE", createLayout())
{
}

bool KompaLeadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void KompaLeadProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    ampEnv.setSampleRate (sr);
    fltEnv.setSampleRate (sr);
    ampEnv.reset();
    fltEnv.reset();

    juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    filter.prepare (mono);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    filter.reset();

    juce::dsp::ProcessSpec stereo { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    chorus.prepare (stereo);
    chorus.reset();

    const float fs = (float) sr;
    octDetA  = 1.0f - std::exp (-kTwoPi * 1000.0f / fs);
    octEnvA  = 1.0f - std::exp (-1.0f / (0.02f * fs));
    octSoftA = 1.0f - std::exp (-kTwoPi * 800.0f / fs);

    for (auto& o : oct) o = OctState();
    distLp[0] = distLp[1] = 0.0f;
    noteStack.clear();
    filterCounter = 0;
}

void KompaLeadProcessor::readParams()
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    p.level = get ("level");   p.oscMix = get ("oscMix"); p.pw = get ("pw");
    p.detune = get ("detune"); p.sub = get ("sub");
    p.cutoff = get ("cutoff"); p.reso = get ("reso");     p.envAmt = get ("envAmt");
    p.glide = get ("glide");   p.vibDepth = get ("vibDepth"); p.vibRate = get ("vibRate");

    p.octOn = get ("octOn") > 0.5f;   p.octDown = get ("octDown"); p.octUp = get ("octUp");
    p.driveOn = get ("driveOn") > 0.5f; p.drive = get ("drive");
    p.distOn = get ("distOn") > 0.5f;   p.dist = get ("dist"); p.distTone = get ("distTone");
    p.chorusOn = get ("chorusOn") > 0.5f;
    p.chRate = get ("chRate"); p.chDepth = get ("chDepth"); p.chMix = get ("chMix");

    ampEnv.setParameters ({ get ("attack"), get ("decay"), get ("sustain"), get ("release") });
    fltEnv.setParameters ({ 0.002f, get ("decay"), 0.15f, get ("release") });

    detuneRatio = std::exp2 (p.detune / 1200.0f);
    glideCoef = p.glide <= 0.5f ? 1.0f
                                : 1.0f - std::exp (-1.0f / (0.001f * p.glide * (float) sr * 0.3f));
    filter.setResonance (0.7f + p.reso * 7.0f);
    distA = 1.0f - std::exp (-kTwoPi * p.distTone / (float) sr);
}

//==============================================================================
static float midiToHz (int note) { return 440.0f * std::exp2 (((float) note - 69.0f) / 12.0f); }

void KompaLeadProcessor::noteOn (int note, float vel)
{
    const bool wasHeld = ! noteStack.empty();
    const bool wasActive = ampEnv.isActive();

    noteStack.erase (std::remove (noteStack.begin(), noteStack.end(), note), noteStack.end());
    noteStack.push_back (note);

    tgtFreq = midiToHz (note);
    velocity = vel;

    const bool legato = wasHeld && p.glide > 0.5f;
    if (! legato)
    {
        if (! wasActive || p.glide <= 0.5f)
            curFreq = tgtFreq;
        ampEnv.noteOn();
        fltEnv.noteOn();
        vibFade = 0.0f;
    }
}

void KompaLeadProcessor::noteOff (int note)
{
    noteStack.erase (std::remove (noteStack.begin(), noteStack.end(), note), noteStack.end());

    if (noteStack.empty())
    {
        ampEnv.noteOff();
        fltEnv.noteOff();
    }
    else
    {
        tgtFreq = midiToHz (noteStack.back());
    }
}

void KompaLeadProcessor::renderSynth (float* out, int start, int num)
{
    const float fs = (float) sr;

    auto pulse = [this] (float ph, float dt)
    {
        float v = ph < p.pw ? 1.0f : -1.0f;
        v += polyBlep (ph, dt);
        v -= polyBlep (wrap01 (ph + 1.0f - p.pw), dt);
        return v;
    };

    for (int i = 0; i < num; ++i)
    {
        curFreq += (tgtFreq - curFreq) * glideCoef;

        // delayed vibrato: fades in after the note starts; mod wheel adds more
        vibFade = std::min (1.0f, vibFade + 1.0f / (0.4f * fs));
        lfoPh = wrap01 (lfoPh + p.vibRate / fs);
        const float vibSemi = std::sin (kTwoPi * lfoPh) * std::min (1.0f, p.vibDepth + modWheel) * 0.5f * vibFade;

        const float f = curFreq * std::exp2 ((vibSemi + bendSemis) / 12.0f);
        const float dt1 = std::min (0.45f, f / fs);
        const float dt2 = std::min (0.45f, dt1 * detuneRatio);

        ph1 = wrap01 (ph1 + dt1);
        ph2 = wrap01 (ph2 + dt2);
        phSub = wrap01 (phSub + dt1 * 0.5f);

        const float saw = 0.5f * ((2.0f * ph1 - 1.0f - polyBlep (ph1, dt1))
                                + (2.0f * ph2 - 1.0f - polyBlep (ph2, dt2)));
        const float sq  = 0.5f * (pulse (ph1, dt1) + pulse (ph2, dt2));

        float s = (1.0f - p.oscMix) * saw + p.oscMix * sq
                + p.sub * 0.6f * std::sin (kTwoPi * phSub);

        const float fe = fltEnv.getNextSample();
        if (--filterCounter <= 0)
        {
            filterCounter = 8;
            float fc = p.cutoff * std::exp2 (p.envAmt * fe * 4.0f + std::log2 (std::max (f, 20.0f) / 261.6f) * 0.3f);
            fc = juce::jlimit (30.0f, std::min (18000.0f, 0.45f * fs), fc);
            filter.setCutoffFrequency (fc);
        }
        s = filter.processSample (0, s);

        out[start + i] = s * ampEnv.getNextSample() * (0.35f + 0.65f * velocity) * p.level * 0.5f;
    }
}

//==============================================================================
void KompaLeadProcessor::applyPedals (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = std::min (2, buffer.getNumChannels());
    const float dGain = 1.0f + p.drive * 14.0f;
    const float dComp = 1.0f - 0.35f * p.drive;
    const float xGain = 1.0f + p.dist * 60.0f;

    for (int c = 0; c < chans; ++c)
    {
        float* d = buffer.getWritePointer (c);
        auto& o = oct[c];

        for (int i = 0; i < n; ++i)
        {
            float x = d[i];

            // 1) Octave: analog-style flip-flop (down) and rectifier (up)
            if (p.octOn)
            {
                o.lp += octDetA * (x - o.lp);
                if (o.prevLp <= 0.0f && o.lp > 0.0f) o.ff = -o.ff;
                o.prevLp = o.lp;

                o.env += octEnvA * (std::fabs (x) - o.env);
                o.dn += octSoftA * (o.ff * o.env * 2.0f - o.dn);

                const float r = std::fabs (x);
                const float up = r - o.dcX + 0.995f * o.dcY;
                o.dcX = r;
                o.dcY = up;

                x += o.dn * p.octDown * 1.6f + up * p.octUp * 1.2f;
            }

            // 2) Drive: smooth tanh overdrive
            if (p.driveOn)
                x = std::tanh (x * dGain) * dComp;

            // 3) Distortion: hard clip + tone filter
            if (p.distOn)
            {
                const float y = juce::jlimit (-0.4f, 0.4f, x * xGain * 0.5f) * 2.2f;
                distLp[c] += distA * (y - distLp[c]);
                x = distLp[c];
            }

            d[i] = x;
        }
    }

    // 4) Chorus
    if (p.chorusOn)
    {
        chorus.setRate (p.chRate);
        chorus.setDepth (p.chDepth);
        chorus.setCentreDelay (8.0f);
        chorus.setFeedback (0.1f);
        chorus.setMix (p.chMix);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        chorus.process (ctx);
    }

    // safety limiter
    for (int c = 0; c < chans; ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            d[i] = juce::jlimit (-1.0f, 1.0f, d[i]);
    }
}

//==============================================================================
void KompaLeadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    readParams();

    float* left = buffer.getWritePointer (0);
    int pos = 0;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int t = juce::jlimit (pos, n, meta.samplePosition);

        renderSynth (left, pos, t - pos);
        pos = t;

        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            noteStack.clear();
            ampEnv.reset();
            fltEnv.reset();
        }
        else if (m.isPitchWheel())
            bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        else if (m.isControllerOfType (1))
            modWheel = (float) m.getControllerValue() / 127.0f;
    }

    renderSynth (left, pos, n - pos);

    if (buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    applyPedals (buffer);
}

//==============================================================================
juce::AudioProcessorEditor* KompaLeadProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void KompaLeadProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KompaLeadProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KompaLeadProcessor();
}
