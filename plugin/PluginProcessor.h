// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Params.h"
#include "VoxEngine.h"

//==============================================================================
/** Receives microphone audio straight from an audio device (used by the instrument build, because
    FL Studio only routes MIDI *out* of instruments and gives instruments no audio input). */
class DirectInput : public juce::AudioIODeviceCallback
{
public:
    DirectInput() : fifo (1 << 15), storage ((size_t) (1 << 15), 0.f) {}

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOutputChannels; ++c)
            if (outputChannelData[c] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[c], numSamples);

        int validCh = 0;
        for (int c = 0; c < numInputChannels; ++c)
            if (inputChannelData[c] != nullptr) ++validCh;
        if (validCh == 0) return;

        int s1, n1, s2, n2;
        fifo.prepareToWrite (numSamples, s1, n1, s2, n2);
        const float scale = 1.0f / (float) validCh;
        auto copyRange = [&] (int start, int count, int srcOffset)
        {
            for (int i = 0; i < count; ++i)
            {
                float sum = 0.f;
                for (int c = 0; c < numInputChannels; ++c)
                    if (inputChannelData[c] != nullptr) sum += inputChannelData[c][srcOffset + i];
                storage[(size_t) (start + i)] = sum * scale;
            }
        };
        if (n1 > 0) copyRange (s1, n1, 0);
        if (n2 > 0) copyRange (s2, n2, n1);
        fifo.finishedWrite (n1 + n2);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        rate = d != nullptr ? d->getCurrentSampleRate() : 48000.0;
        fifo.reset();
    }

    void audioDeviceStopped() override {}

    /** Reads up to maxSamples. Drops old audio if the reader has fallen too far behind. */
    int read (float* dst, int maxSamples, int wantedPerBlock)
    {
        int avail = fifo.getNumReady();
        const int limit = std::max (4096, wantedPerBlock * 4);
        if (avail > limit)
        {
            int s1, n1, s2, n2;
            const int drop = avail - wantedPerBlock;
            fifo.prepareToRead (drop, s1, n1, s2, n2);
            fifo.finishedRead (n1 + n2);
            avail = fifo.getNumReady();
        }
        const int n = std::min (avail, maxSamples);
        if (n <= 0) return 0;
        int s1, n1, s2, n2;
        fifo.prepareToRead (n, s1, n1, s2, n2);
        if (n1 > 0) std::copy (storage.begin() + s1, storage.begin() + s1 + n1, dst);
        if (n2 > 0) std::copy (storage.begin() + s2, storage.begin() + s2 + n2, dst + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    double getRate() const { return rate.load(); }

private:
    juce::AbstractFifo fifo;
    std::vector<float> storage;
    std::atomic<double> rate { 48000.0 };
};

//==============================================================================
class VoxMidiProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener
{
public:
    VoxMidiProcessor();
    ~VoxMidiProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VoxMidi"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //-- used by the editor
    juce::AudioProcessorValueTreeState apvts;
    vox::VoxEngine& getEngine() { return engine; }
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    bool directInputAvailable() const { return wrapperType != wrapperType_Standalone; }
    bool directInputActive() const { return directActive.load(); }
    juce::String padNames[vox::kNumPads];

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void updateDirectInput();
    vox::EngineParams buildParams() const;
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    float get (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    vox::VoxEngine engine;
    vox::EventBuffer events;

    juce::AudioDeviceManager deviceManager;
    DirectInput directInput;
    std::atomic<bool> directActive { false };
    std::atomic<bool> wantDirect { false };
    std::unique_ptr<juce::XmlElement> savedDeviceXml;
    bool deviceOpen = false;
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);

    std::vector<float> mono;
    double hostRate = 44100.0;
    double engineRate = 0.0;
    int hostBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxMidiProcessor)
};
