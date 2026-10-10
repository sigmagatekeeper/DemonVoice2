#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

// Einfacher Echtzeit-Pitchshifter (zwei überblendete Delay-Taps)
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        windowSize = (float) (0.06 * sampleRate);   // 60 ms
        buffer.assign ((size_t) windowSize * 2 + 8, 0.0f);
        writePos = 0; phase = 0.0f;
    }

    float process (float in, float ratio)
    {
        const int size = (int) buffer.size();
        buffer[(size_t) writePos] = in;

        phase += (1.0f - ratio) / windowSize;
        while (phase >= 1.0f) phase -= 1.0f;
        while (phase < 0.0f)  phase += 1.0f;

        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;

        const float g1 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase);
        const float g2 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p2);

        const float out = g1 * read (phase * windowSize, size)
                        + g2 * read (p2 * windowSize, size);

        writePos = (writePos + 1) % size;
        return out;
    }

private:
    float read (float delay, int size) const
    {
        float pos = (float) writePos - delay;
        while (pos < 0.0f) pos += (float) size;
        const int i0 = (int) pos % size;
        const int i1 = (i0 + 1) % size;
        const float frac = pos - std::floor (pos);
        return buffer[(size_t) i0] * (1.0f - frac) + buffer[(size_t) i1] * frac;
    }

    std::vector<float> buffer;
    float windowSize = 1000.0f, phase = 0.0f;
    int writePos = 0;
};

class DemonVoiceAudioProcessor : public juce::AudioProcessor
{
public:
    DemonVoiceAudioProcessor();
    ~DemonVoiceAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override
    { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DemonVoice"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                  juce::dsp::IIR::Coefficients<float>>;

    PitchShifter shifters[2][3];
    Filter highPass, lowShelf, presence, whisperHP;
    juce::Reverb reverb;
    juce::dsp::Limiter<float> limiter;
    juce::AudioBuffer<float> dryBuf, wetBuf, whisperBuf;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DemonVoiceAudioProcessor)
};
