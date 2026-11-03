// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
#include "PluginProcessor.h"
#include "PluginEditor.h"

#ifndef VOXMIDI_FX
 #define VOXMIDI_FX 0
#endif
#ifndef VOXMIDI_DEFAULT_DIRECT
 #define VOXMIDI_DEFAULT_DIRECT 0
#endif

namespace
{
const char* const kDefaultPadNames[vox::kNumPads] = { "Kick", "Snare", "Hat", "Open Hat", "Clap", "Rim", "Tom", "Crash" };
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const int kMajorScalePcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VoxMidiProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto pid = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };

    auto addFloat = [&] (const juce::String& id, const juce::String& name, float lo, float hi, float step, float def, const juce::String& label = {})
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (pid (id), name, juce::NormalisableRange<float> (lo, hi, step), def,
                                                                 juce::AudioParameterFloatAttributes().withLabel (label)));
    };
    auto addInt = [&] (const juce::String& id, const juce::String& name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterInt> (pid (id), name, lo, hi, def));
    };
    auto addBool = [&] (const juce::String& id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (pid (id), name, def));
    };
    auto addChoice = [&] (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (pid (id), name, items, def));
    };

    // global
    addChoice (ids::mode, "Mode", { "Pitch + Triggers", "Pitch only", "Triggers only" }, 0);
#if VOXMIDI_DEFAULT_DIRECT
    addChoice (ids::inputsrc, "Audio Source", { "Host input", "Direct (own device)" }, 1);
#else
    addChoice (ids::inputsrc, "Audio Source", { "Host input", "Direct (own device)" }, 0);
#endif
    addFloat (ids::inputgain, "Input Gain", -24.f, 36.f, 0.1f, 0.f, "dB");
    addBool (ids::monitor, "Monitor Input", false);
    addChoice (ids::range, "Voice Range", { "Low (60 Hz)", "Normal (80 Hz)", "High (130 Hz)" }, 1);

    // pitch
    juce::StringArray roots;
    for (auto* n : kNoteNames) roots.add (n);
    addChoice (ids::root, "Root", roots, 0);
    juce::StringArray scales;
    for (int i = 0; i < vox::kNumScales; ++i) scales.add (vox::getScaleTable()[i].name);
    scales.add ("Custom");
    addChoice (ids::scale, "Scale", scales, 0);
    for (int i = 0; i < 12; ++i)
    {
        bool inMajor = false;
        for (int pc : kMajorScalePcs) inMajor = inMajor || pc == i;
        addBool (indexedId ("cust", i), juce::String ("Custom ") + kNoteNames[i], inMajor);
    }
    addFloat (ids::gate, "Gate", -70.f, -20.f, 0.5f, -48.f, "dB");
    addFloat (ids::clarity, "Pitch Confidence", 0.5f, 0.95f, 0.01f, 0.82f);
    addFloat (ids::stick, "Stickiness", 0.f, 1.f, 0.01f, 0.4f);
    addBool (ids::retrig, "Retrigger on Dip", true);
    addFloat (ids::dip, "Dip Depth", 3.f, 20.f, 0.5f, 8.f, "dB");
    addInt (ids::octave, "Octave", -3, 3, 0);
    addInt (ids::transpose, "Transpose", -12, 12, 0);
    addBool (ids::dynvel, "Dynamic Velocity", true);
    addInt (ids::fixedvel, "Fixed Velocity", 1, 127, 100);
    addFloat (ids::velfloor, "Velocity Floor", -60.f, -20.f, 0.5f, -42.f, "dB");
    addFloat (ids::velceil, "Velocity Ceiling", -30.f, 0.f, 0.5f, -14.f, "dB");
    addChoice (ids::bendmode, "Pitch Bend", { "Off", "IntelliBend", "TruBend" }, 1);
    addInt (ids::bendrange, "Bend Range", 1, 24, 2);
    addInt (ids::melch, "Melody Channel", 1, 16, 1);
    {
        juce::StringArray chords;
        for (int i = 0; i < vox::kNumChordModes; ++i) chords.add (vox::chordModeNames()[i]);
        addChoice (ids::chord, "Chords", chords, 0);
    }
    addInt (ids::chordch, "Chord Channel", 1, 16, 2);

    // triggers
    addFloat (ids::trigsens, "Trigger Sensitivity", 3.f, 14.f, 0.5f, 7.f, "dB");
    addFloat (ids::trigfloor, "Trigger Threshold", -60.f, -20.f, 0.5f, -46.f, "dB");
    addInt (ids::trigdelay, "Trigger Delay", 0, 6, 2);
    addFloat (ids::trigstrict, "Match Tolerance", 0.5f, 2.f, 0.01f, 1.f);
    addFloat (ids::triglock, "Retrigger Lockout", 10.f, 100.f, 1.f, 30.f, "ms");
    addFloat (ids::trignotelen, "Note Length", 20.f, 300.f, 1.f, 60.f, "ms");
    addInt (ids::trigch, "Trigger Channel", 1, 16, 10);
    addBool (ids::trigignore, "Mute Triggers While Singing", true);
    addFloat (ids::triginhibit, "Trigger Beats Pitch", 0.f, 300.f, 1.f, 100.f, "ms");
    addBool (ids::trigdyn, "Dynamic Trigger Velocity", true);
    addInt (ids::trigvel, "Fixed Trigger Velocity", 1, 127, 110);
    const int padDefaults[vox::kNumPads] = { 36, 38, 42, 46, 39, 37, 41, 49 };
    for (int i = 0; i < vox::kNumPads; ++i)
        addInt (indexedId ("pad", i), "Pad " + juce::String (i + 1) + " Note", 0, 127, padDefaults[i]);

    // controllers
    const char* ccNames[4] = { "Vowel A", "Vowel E", "Vowel O", "Level" };
    const bool ccDefOn[4] = { true, true, true, false };
    for (int i = 0; i < 4; ++i)
    {
        addBool (indexedId ("cc", i, "on"), juce::String (ccNames[i]) + " CC On", ccDefOn[i]);
        addInt (indexedId ("cc", i, "num"), juce::String (ccNames[i]) + " CC Number", 0, 127, 20 + i);
    }
    addInt (ids::ccch, "CC Channel", 1, 16, 1);
    addFloat (ids::vowelsmooth, "Controller Smoothing", 0.f, 1.f, 0.01f, 0.4f);

    return layout;
}

//==============================================================================
VoxMidiProcessor::VoxMidiProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), VOXMIDI_FX != 0)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VOXMIDI", createLayout())
{
    for (int i = 0; i < vox::kNumPads; ++i) padNames[i] = kDefaultPadNames[i];
    apvts.addParameterListener (ids::inputsrc, this);
}

VoxMidiProcessor::~VoxMidiProcessor()
{
    *aliveFlag = false;
    apvts.removeParameterListener (ids::inputsrc, this);
    if (deviceOpen)
    {
        deviceManager.removeAudioCallback (&directInput);
        deviceManager.closeAudioDevice();
    }
}

bool VoxMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void VoxMidiProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == ids::inputsrc)
    {
        wantDirect = directInputAvailable() && (int) get (ids::inputsrc) == 1;
        auto flag = aliveFlag;
        juce::MessageManager::callAsync ([this, flag] { if (*flag) updateDirectInput(); });
    }
}

void VoxMidiProcessor::updateDirectInput()
{
    const bool want = wantDirect.load();
    if (want && ! deviceOpen)
    {
        const auto err = deviceManager.initialise (2, 0, savedDeviceXml.get(), true);
        if (err.isEmpty())
        {
            deviceManager.addAudioCallback (&directInput);
            deviceOpen = true;
        }
        directActive = deviceOpen;
    }
    else if (! want && deviceOpen)
    {
        deviceManager.removeAudioCallback (&directInput);
        deviceManager.closeAudioDevice();
        deviceOpen = false;
        directActive = false;
    }
}

void VoxMidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    hostBlock = samplesPerBlock;
    mono.assign ((size_t) std::max (32768, samplesPerBlock * 4), 0.f);
    engine.prepare (sampleRate);
    engineRate = sampleRate;

    // Only open the microphone when the plugin is really going to run (not while a host scans plugins).
    wantDirect = directInputAvailable() && (int) get (ids::inputsrc) == 1;
    auto flag = aliveFlag;
    juce::MessageManager::callAsync ([this, flag] { if (*flag) updateDirectInput(); });
}

vox::EngineParams VoxMidiProcessor::buildParams() const
{
    vox::EngineParams ep;
    ep.mode = (int) get (ids::mode);
    const float ranges[3] = { 60.f, 80.f, 130.f };
    ep.rangeHz = ranges[juce::jlimit (0, 2, (int) get (ids::range))];

    auto& n = ep.note;
    const int scaleIdx = (int) get (ids::scale);
    if (scaleIdx >= vox::kNumScales)
    {
        uint16_t m = 0;
        for (int i = 0; i < 12; ++i)
            if (get (indexedId ("cust", i)) > 0.5f) m = (uint16_t) (m | (1u << i));
        n.mask = m != 0 ? m : (uint16_t) 0x0FFF;
    }
    else
    {
        n.mask = vox::absoluteMask (vox::getScaleTable()[scaleIdx].intervals, (int) get (ids::root));
    }
    n.melodyCh = (int) get (ids::melch);
    n.chordCh = (int) get (ids::chordch);
    n.gateOpenDb = get (ids::gate);
    n.clarity = get (ids::clarity);
    n.stick = get (ids::stick);
    n.retrig = get (ids::retrig) > 0.5f;
    n.dipDb = get (ids::dip);
    n.transpose = (int) get (ids::octave) * 12 + (int) get (ids::transpose);
    n.dynVel = get (ids::dynvel) > 0.5f;
    n.fixedVel = (int) get (ids::fixedvel);
    n.velFloorDb = get (ids::velfloor);
    n.velCeilDb = get (ids::velceil);
    n.bendMode = (int) get (ids::bendmode);
    n.bendRange = (int) get (ids::bendrange);
    n.chordMode = (int) get (ids::chord);

    auto& t = ep.trig;
    t.onsetDb = get (ids::trigsens);
    t.floorDb = get (ids::trigfloor);
    t.delayFrames = (int) get (ids::trigdelay);
    t.strict = get (ids::trigstrict);
    t.lockMs = get (ids::triglock);
    t.dynVel = get (ids::trigdyn) > 0.5f;
    t.fixedVel = (int) get (ids::trigvel);
    ep.trigCh = (int) get (ids::trigch);
    ep.trigNoteLenMs = get (ids::trignotelen);
    ep.trigIgnoreSinging = get (ids::trigignore) > 0.5f;
    ep.trigInhibitPitchMs = get (ids::triginhibit);
    for (int i = 0; i < vox::kNumPads; ++i) ep.padNote[(size_t) i] = (int) get (indexedId ("pad", i));

    ep.ccCh = (int) get (ids::ccch);
    for (int i = 0; i < 4; ++i)
    {
        ep.ccOn[(size_t) i] = get (indexedId ("cc", i, "on")) > 0.5f;
        ep.ccNum[(size_t) i] = (int) get (indexedId ("cc", i, "num"));
    }
    ep.vowelSmooth = get (ids::vowelsmooth);
    return ep;
}

void VoxMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    const bool direct = wantDirect.load() && directActive.load();

    int count = 0;
    double rate = hostRate;
    if (direct)
    {
        rate = directInput.getRate();
        const int wanted = (int) std::ceil ((double) n * rate / hostRate);
        count = directInput.read (mono.data(), (int) mono.size(), wanted);
    }
    else
    {
        count = std::min (n, (int) mono.size());
        if (inCh <= 0)
        {
            std::fill (mono.begin(), mono.begin() + count, 0.f);
        }
        else
        {
            const float* l = buffer.getReadPointer (0);
            const float* r = inCh > 1 ? buffer.getReadPointer (1) : nullptr;
            for (int i = 0; i < count; ++i) mono[(size_t) i] = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
        }
    }

    if (std::abs (rate - engineRate) > 0.5)
    {
        engine.prepare (rate);   // sample rate of the microphone differs from the host's
        engineRate = rate;
    }

    events.clear();
    if (count > 0)
    {
        const float gain = juce::Decibels::decibelsToGain (get (ids::inputgain));
        for (int i = 0; i < count; ++i) mono[(size_t) i] *= gain;
        engine.process (mono.data(), count, buildParams(), events);
    }

    for (int i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        int pos = e.offset;
        if (direct && count > 0) pos = (int) std::lround ((double) e.offset * (double) n / (double) count);
        pos = juce::jlimit (0, std::max (0, n - 1), pos);
        const int ch = juce::jlimit (1, 16, (int) e.channel);

        switch (e.type)
        {
            case vox::MidiEv::NoteOn:
                midi.addEvent (juce::MidiMessage::noteOn (ch, e.data1, (juce::uint8) e.data2), pos); break;
            case vox::MidiEv::NoteOff:
                midi.addEvent (juce::MidiMessage::noteOff (ch, e.data1, (juce::uint8) 0), pos); break;
            case vox::MidiEv::CC:
                midi.addEvent (juce::MidiMessage::controllerEvent (ch, e.data1, e.data2), pos); break;
            case vox::MidiEv::PitchBend:
                midi.addEvent (juce::MidiMessage::pitchWheel (ch, e.data1), pos); break;
        }
    }

    // audio output: silent unless the effect build is asked to monitor the microphone
    if (direct || get (ids::monitor) < 0.5f)
    {
        buffer.clear();
    }
    else
    {
        for (int ch = inCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    }
}

juce::AudioProcessorEditor* VoxMidiProcessor::createEditor() { return new VoxMidiEditor (*this); }

//==============================================================================
static juce::String floatsToString (const float* v, int n)
{
    juce::StringArray sa;
    for (int i = 0; i < n; ++i) sa.add (juce::String (v[i], 6));
    return sa.joinIntoString (" ");
}

static void stringToFloats (const juce::String& s, float* v, int n)
{
    const auto t = juce::StringArray::fromTokens (s, " ", "");
    for (int i = 0; i < n && i < t.size(); ++i) v[i] = t[i].getFloatValue();
}

void VoxMidiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr) return;

    auto* extra = xml->createNewChildElement ("VOXEXTRA");

    std::array<vox::PadModel, vox::kNumPads> pads;
    engine.getPads (pads);
    for (int i = 0; i < vox::kNumPads; ++i)
    {
        auto* e = extra->createNewChildElement ("Pad");
        const auto& m = pads[(size_t) i];
        e->setAttribute ("i", i);
        e->setAttribute ("name", padNames[i]);
        e->setAttribute ("trained", m.trained ? 1 : 0);
        e->setAttribute ("examples", m.examples);
        e->setAttribute ("reject", (double) m.reject);
        e->setAttribute ("mean", floatsToString (m.mean.data(), vox::kNumFeat));
        e->setAttribute ("sigma", floatsToString (m.sigma.data(), vox::kNumFeat));
    }

    const auto refs = engine.getVowelRefs();
    auto* v = extra->createNewChildElement ("Vowels");
    v->setAttribute ("f1", floatsToString (refs.f1.data(), 3));
    v->setAttribute ("f2", floatsToString (refs.f2.data(), 3));

    if (deviceOpen)
        if (auto dev = deviceManager.createStateXml())
            extra->addChildElement (dev.release());
    if (savedDeviceXml != nullptr && ! deviceOpen)
        extra->addChildElement (new juce::XmlElement (*savedDeviceXml));

    copyXmlToBinary (*xml, dest);
}

void VoxMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    if (auto* extra = xml->getChildByName ("VOXEXTRA"))
    {
        std::array<vox::PadModel, vox::kNumPads> pads;
        engine.getPads (pads);
        for (auto* e : extra->getChildWithTagNameIterator ("Pad"))
        {
            const int i = e->getIntAttribute ("i", -1);
            if (i < 0 || i >= vox::kNumPads) continue;
            padNames[i] = e->getStringAttribute ("name", kDefaultPadNames[i]);
            auto& m = pads[(size_t) i];
            m.trained = e->getIntAttribute ("trained") != 0;
            m.examples = e->getIntAttribute ("examples");
            m.reject = (float) e->getDoubleAttribute ("reject", 2.5);
            stringToFloats (e->getStringAttribute ("mean"), m.mean.data(), vox::kNumFeat);
            stringToFloats (e->getStringAttribute ("sigma"), m.sigma.data(), vox::kNumFeat);
            for (auto& s : m.sigma) if (s <= 0.f) s = 1.f;
        }
        engine.setPads (pads);

        if (auto* v = extra->getChildByName ("Vowels"))
        {
            vox::VowelRefs refs;
            stringToFloats (v->getStringAttribute ("f1"), refs.f1.data(), 3);
            stringToFloats (v->getStringAttribute ("f2"), refs.f2.data(), 3);
            engine.setVowelRefs (refs);
        }
        if (auto* dev = extra->getChildByName ("DEVICESETUP"))
            savedDeviceXml = std::make_unique<juce::XmlElement> (*dev);

        xml->removeChildElement (extra, true);
    }
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxMidiProcessor(); }
