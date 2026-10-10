#include "PluginProcessor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using Range = juce::NormalisableRange<float>;

static std::unique_ptr<juce::AudioParameterFloat>
mk (const char* id, const char* name, float lo, float hi, float def, const char* suffix = "")
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, Range (lo, hi, 0.01f), def,
        juce::AudioParameterFloatAttributes().withLabel (suffix));
}

APVTS::ParameterLayout DemonVoiceAudioProcessor::createLayout()
{
    APVTS::ParameterLayout l;
    l.add (mk ("inGain",   "Input Gain",        -24.f, 24.f,  0.f, "dB"));
    l.add (mk ("lowPitch", "Deep Pitch",        -24.f,  0.f, -12.f, "st"));
    l.add (mk ("lowLevel", "Deep Level",          0.f,  1.f,  0.8f));
    l.add (mk ("midPitch", "Mid Pitch",         -12.f,  0.f,  -5.f, "st"));
    l.add (mk ("midLevel", "Mid Level",           0.f,  1.f,  0.6f));
    l.add (mk ("whispPitch","Whisper Pitch",      0.f, 24.f,  12.f, "st"));
    l.add (mk ("whispLevel","Whisper Level",      0.f,  1.f,  0.25f));
    l.add (mk ("drive",    "Drive",               1.f, 20.f,  4.f));
    l.add (mk ("lowBoost", "Low Boost 150Hz",   -6.f, 12.f,  5.f, "dB"));
    l.add (mk ("presence", "Presence 3kHz",     -6.f, 12.f,  3.f, "dB"));
    l.add (mk ("revSize",  "Reverb Size",         0.f,  1.f,  0.9f));
    l.add (mk ("revMix",   "Reverb Mix",          0.f,  1.f,  0.25f));
    l.add (mk ("mix",      "Dry/Wet",             0.f,  1.f,  1.0f));
    l.add (mk ("outGain",  "Output Gain",       -24.f, 12.f, -3.f, "dB"));
    return l;
}

DemonVoiceAudioProcessor::DemonVoiceAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool DemonVoiceAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void DemonVoiceAudioProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize,
                                  (juce::uint32) getTotalNumOutputChannels() };

    for (auto& ch : shifters) for (auto& s : ch) s.prepare (sampleRate);

    highPass.prepare (spec); lowShelf.prepare (spec);
    presence.prepare (spec); whisperHP.prepare (spec);
    limiter.prepare (spec);
    limiter.setThreshold (-1.0f);
    limiter.setRelease (50.0f);

    *highPass.state  = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 60.0f);
    *whisperHP.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 2500.0f);

    reverb.reset();
    const int n = getTotalNumOutputChannels();
    dryBuf.setSize (n, blockSize); wetBuf.setSize (n, blockSize); whisperBuf.setSize (n, blockSize);
}

void DemonVoiceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();

    dryBuf.setSize (numCh, n, false, false, true);
    wetBuf.setSize (numCh, n, false, false, true);
    whisperBuf.setSize (numCh, n, false, false, true);

    buffer.applyGain (juce::Decibels::decibelsToGain (get ("inGain")));

    const float rLow  = std::pow (2.0f, get ("lowPitch")   / 12.0f);
    const float rMid  = std::pow (2.0f, get ("midPitch")   / 12.0f);
    const float rHigh = std::pow (2.0f, get ("whispPitch") / 12.0f);
    const float lvLow = get ("lowLevel"), lvMid = get ("midLevel"), lvWh = get ("whispLevel");
    const float drive = get ("drive");
    const float driveComp = 1.0f / (1.0f + 0.35f * (drive - 1.0f));

    // 1) Layering
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* in = buffer.getReadPointer (ch);
        float* dry = dryBuf.getWritePointer (ch);
        float* wet = wetBuf.getWritePointer (ch);
        float* wh  = whisperBuf.getWritePointer (ch);

        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            dry[i] = x;
            wet[i] = x + lvLow * shifters[ch][0].process (x, rLow)
                       + lvMid * shifters[ch][1].process (x, rMid);
            wh[i]  = shifters[ch][2].process (x, rHigh);
        }
    }

    // Flüster-Layer: nur Höhen, dann dazumischen
    {
        juce::dsp::AudioBlock<float> wb (whisperBuf);
        whisperHP.process (juce::dsp::ProcessContextReplacing<float> (wb));
    }
    for (int ch = 0; ch < numCh; ++ch)
        wetBuf.addFrom (ch, 0, whisperBuf, ch, 0, n, lvWh);

    // 2) Verzerrung (Sättigung)
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* w = wetBuf.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            w[i] = std::tanh (w[i] * drive) * driveComp;
    }

    // 3) EQ
    *lowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sr, 150.0f, 0.7f, juce::Decibels::decibelsToGain (get ("lowBoost")));
    *presence.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sr, 3000.0f, 0.9f, juce::Decibels::decibelsToGain (get ("presence")));
    {
        juce::dsp::AudioBlock<float> wb (wetBuf);
        juce::dsp::ProcessContextReplacing<float> ctx (wb);
        highPass.process (ctx); lowShelf.process (ctx); presence.process (ctx);
    }

    // 4) Hall
    juce::Reverb::Parameters rp;
    rp.roomSize = get ("revSize"); rp.damping = 0.6f;
    rp.wetLevel = get ("revMix");  rp.dryLevel = 1.0f - 0.5f * get ("revMix");
    rp.width = 1.0f; rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
    if (numCh == 2) reverb.processStereo (wetBuf.getWritePointer (0), wetBuf.getWritePointer (1), n);
    else            reverb.processMono   (wetBuf.getWritePointer (0), n);

    // 5) Dry/Wet + Output + Limiter
    const float mix = get ("mix");
    const float outG = juce::Decibels::decibelsToGain (get ("outGain"));
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* o = buffer.getWritePointer (ch);
        const float* d = dryBuf.getReadPointer (ch);
        const float* w = wetBuf.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            o[i] = (d[i] * (1.0f - mix) + w[i] * mix) * outG;
    }
    juce::dsp::AudioBlock<float> ob (buffer);
    limiter.process (juce::dsp::ProcessContextReplacing<float> (ob));
}

void DemonVoiceAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void DemonVoiceAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DemonVoiceAudioProcessor();
}
