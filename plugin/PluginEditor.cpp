// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
#include "PluginEditor.h"

namespace
{
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

juce::String noteName (int midi)
{
    return juce::String (kNoteNames[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

const juce::Colour kAccent (0xff4fc3f7);
const juce::Colour kPanel (0xff1e2530);
const juce::Colour kPanelLight (0xff2a3342);
} // namespace

//==============================================================================
ParamCell::ParamCell (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text, Kind k)
    : kind (k)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (kind == Kind::Linear ? juce::Justification::centredLeft : juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (12.5f)));
    label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));

    switch (kind)
    {
        case Kind::Rotary:
            addAndMakeVisible (label);
            addAndMakeVisible (slider);
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
            sliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
            break;
        case Kind::Linear:
            addAndMakeVisible (label);
            addAndMakeVisible (slider);
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
            sliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
            break;
        case Kind::Combo:
            addAndMakeVisible (label);
            addAndMakeVisible (combo);
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
                combo.addItemList (p->choices, 1);
            comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, combo);
            break;
        case Kind::Toggle:
            addAndMakeVisible (toggle);
            toggle.setButtonText (text);
            buttonAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, toggle);
            break;
    }
}

int ParamCell::preferredWidth() const
{
    switch (kind)
    {
        case Kind::Rotary: return 96;
        case Kind::Linear: return 236;
        case Kind::Combo:  return 156;
        case Kind::Toggle: return 236;
    }
    return 100;
}

int ParamCell::preferredHeight() const
{
    switch (kind)
    {
        case Kind::Rotary: return 100;
        case Kind::Linear: return 44;
        case Kind::Combo:  return 54;
        case Kind::Toggle: return 30;
    }
    return 40;
}

void ParamCell::resized()
{
    auto r = getLocalBounds();
    switch (kind)
    {
        case Kind::Rotary:
            label.setBounds (r.removeFromTop (16));
            slider.setBounds (r);
            break;
        case Kind::Linear:
            label.setBounds (r.removeFromTop (16));
            slider.setBounds (r);
            break;
        case Kind::Combo:
            label.setBounds (r.removeFromTop (16));
            combo.setBounds (r.removeFromTop (28).reduced (2, 0));
            break;
        case Kind::Toggle:
            toggle.setBounds (r);
            break;
    }
}

void ParamPanel::flow (juce::Rectangle<int> area)
{
    int x = area.getX(), y = area.getY(), rowH = 0;
    for (auto* c : cells)
    {
        const int w = c->preferredWidth(), h = c->preferredHeight();
        if (x + w > area.getRight() && x > area.getX())
        {
            x = area.getX();
            y += rowH + 4;
            rowH = 0;
        }
        c->setBounds (x, y, w, h);
        x += w + 6;
        rowH = std::max (rowH, h);
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r, 3.f);
    auto fill = r.reduced (1.f);
    fill.setWidth (fill.getWidth() * juce::jlimit (0.f, 1.f, value));
    g.setColour (value > 0.92f ? juce::Colours::orangered : kAccent);
    g.fillRoundedRectangle (fill, 2.f);
    if (marker >= 0.f)
    {
        g.setColour (juce::Colours::yellow.withAlpha (0.9f));
        const float x = r.getX() + r.getWidth() * juce::jlimit (0.f, 1.f, marker);
        g.drawLine (x, r.getY(), x, r.getBottom(), 1.5f);
    }
}

//==============================================================================
PitchPanel::PitchPanel (VoxMidiProcessor& p) : ParamPanel (p.apvts)
{
    using K = ParamCell::Kind;
    add (ids::root, "Root", K::Combo);
    add (ids::scale, "Scale", K::Combo);
    add (ids::bendmode, "Pitch bend", K::Combo);
    add (ids::bendrange, "Bend range (semitones)", K::Rotary);
    add (ids::stick, "Stickiness", K::Rotary);
    add (ids::clarity, "Pitch confidence", K::Rotary);
    add (ids::gate, "Gate", K::Rotary);
    add (ids::dip, "Dip depth", K::Rotary);
    add (ids::octave, "Octave", K::Rotary);
    add (ids::transpose, "Transpose", K::Rotary);
    add (ids::fixedvel, "Fixed velocity", K::Rotary);
    add (ids::velfloor, "Velocity floor", K::Rotary);
    add (ids::velceil, "Velocity ceiling", K::Rotary);
    add (ids::retrig, "Retrigger on loudness dip (new syllable)", K::Toggle);
    add (ids::dynvel, "Velocity follows loudness", K::Toggle);

    scaleLabel.setText ("Custom scale - tick the notes you want (used when Scale = Custom)", juce::dontSendNotification);
    scaleLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    addAndMakeVisible (scaleLabel);
    for (int i = 0; i < 12; ++i)
    {
        auto* b = noteButtons.add (new juce::ToggleButton (kNoteNames[i]));
        addAndMakeVisible (b);
        noteAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (p.apvts, indexedId ("cust", i), *b));
    }
}

void PitchPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    scaleLabel.setBounds (r.removeFromTop (20));
    auto row = r.removeFromTop (28);
    const int w = std::min (64, row.getWidth() / 12);
    for (auto* b : noteButtons) b->setBounds (row.removeFromLeft (w));
    r.removeFromTop (6);
    flow (r);
}

//==============================================================================
ChordPanel::ChordPanel (VoxMidiProcessor& p) : ParamPanel (p.apvts)
{
    using K = ParamCell::Kind;
    add (ids::chord, "Chord type (follows the melody)", K::Combo);
    add (ids::melch, "Melody MIDI channel", K::Rotary);
    add (ids::chordch, "Chord MIDI channel", K::Rotary);
}

//==============================================================================
TriggerPanel::TriggerPanel (VoxMidiProcessor& p) : ParamPanel (p.apvts), proc (p)
{
    using K = ParamCell::Kind;
    add (ids::trigsens, "Sensitivity", K::Rotary);
    add (ids::trigfloor, "Threshold", K::Rotary);
    add (ids::trigdelay, "Delay (hops)", K::Rotary);
    add (ids::trigstrict, "Match tolerance", K::Rotary);
    add (ids::triglock, "Lockout", K::Rotary);
    add (ids::trignotelen, "Note length", K::Rotary);
    add (ids::trigch, "MIDI channel", K::Rotary);
    add (ids::triginhibit, "Trigger beats pitch", K::Rotary);
    add (ids::trigvel, "Fixed velocity", K::Rotary);
    add (ids::trigdyn, "Velocity follows loudness", K::Toggle);
    add (ids::trigignore, "Mute triggers while a note is held", K::Toggle);

    hint.setText ("Press Train, make that sound 6-10 times (kick, snare, click, clap...), then press Done.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    addAndMakeVisible (hint);

    for (int i = 0; i < vox::kNumPads; ++i)
    {
        auto* t = tiles.add (new Tile());
        addAndMakeVisible (t->name);
        t->name.setText (proc.padNames[i], juce::dontSendNotification);
        t->name.onTextChange = [this, i, t] { proc.padNames[i] = t->name.getText(); };

        addAndMakeVisible (t->note);
        t->note.setSliderStyle (juce::Slider::IncDecButtons);
        t->note.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 34, 22);
        t->noteAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, indexedId ("pad", i), t->note);

        addAndMakeVisible (t->train);
        t->train.onClick = [this, i] { trainClicked (i); };
        addAndMakeVisible (t->clear);
        t->clear.onClick = [this, i] { proc.getEngine().requestClearPad (i); refreshTicks = 100; };
        addAndMakeVisible (t->status);
        t->status.setFont (juce::Font (juce::FontOptions (12.f)));
        t->status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    }
    proc.getEngine().getPads (padCache);
}

void TriggerPanel::trainClicked (int pad)
{
    auto& eng = proc.getEngine();
    if (trainingPad == pad) eng.requestTrain (-1);
    else if (trainingPad < 0) eng.requestTrain (pad);
}

void TriggerPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    hint.setBounds (r.removeFromTop (20));
    r.removeFromTop (4);
    const int tileH = 92, gap = 8;
    const int tileW = (r.getWidth() - gap * 3) / 4;
    for (int i = 0; i < vox::kNumPads; ++i)
    {
        const int col = i % 4, row = i / 4;
        auto b = juce::Rectangle<int> (r.getX() + col * (tileW + gap), r.getY() + row * (tileH + gap), tileW, tileH);
        auto* t = tiles[i];
        t->bounds = b;
        auto in = b.reduced (6);
        auto r1 = in.removeFromTop (26);
        t->note.setBounds (r1.removeFromRight (84));
        t->name.setBounds (r1.reduced (0, 1).withTrimmedRight (4));
        in.removeFromTop (4);
        auto r2 = in.removeFromTop (26);
        t->train.setBounds (r2.removeFromLeft (r2.getWidth() / 2).reduced (1));
        t->clear.setBounds (r2.reduced (1));
        t->status.setBounds (in);
    }
    r.removeFromTop (2 * (tileH + gap) + 4);
    flow (r);
}

void TriggerPanel::paint (juce::Graphics& g)
{
    for (int i = 0; i < tiles.size(); ++i)
    {
        auto b = tiles[i]->bounds.toFloat();
        g.setColour (i == trainingPad ? juce::Colour (0xff6b2a2a) : (i == flashPad ? kAccent.withAlpha (0.55f) : kPanelLight));
        g.fillRoundedRectangle (b, 6.f);
    }
}

void TriggerPanel::tick()
{
    auto& eng = proc.getEngine();
    bool repaintNeeded = false;

    const int c = eng.tTrigCounter.load();
    if (c != lastCounter)
    {
        lastCounter = c;
        flashPad = eng.tTrigPad.load();
        flashTicks = 5;
        repaintNeeded = true;
    }
    if (flashTicks > 0 && --flashTicks == 0) { flashPad = -1; repaintNeeded = true; }

    const int tp = eng.tTrainPad.load();
    if (tp != trainingPad)
    {
        trainingPad = tp;
        refreshTicks = 100;
        repaintNeeded = true;
    }
    if (++refreshTicks >= 15 || trainingPad >= 0)
    {
        if (refreshTicks >= 15) { eng.getPads (padCache); refreshTicks = 0; }
        for (int i = 0; i < tiles.size(); ++i)
        {
            auto* t = tiles[i];
            if (i == trainingPad)
                t->status.setText ("Listening... " + juce::String (eng.tTrainCount.load()) + " hits", juce::dontSendNotification);
            else if (padCache[(size_t) i].trained)
                t->status.setText ("Trained (" + juce::String (padCache[(size_t) i].examples) + " hits)", juce::dontSendNotification);
            else
                t->status.setText ("Not trained", juce::dontSendNotification);
            t->train.setButtonText (i == trainingPad ? "Done" : "Train");
            t->train.setEnabled (trainingPad < 0 || i == trainingPad);
        }
    }
    if (repaintNeeded) repaint();
}

//==============================================================================
VowelPanel::VowelPanel (VoxMidiProcessor& p) : ParamPanel (p.apvts), proc (p)
{
    using K = ParamCell::Kind;
    const char* names[4] = { "Vowel A (ah)", "Vowel E (ee)", "Vowel O (oo)", "Loudness" };
    for (int i = 0; i < 4; ++i)
    {
        add (indexedId ("cc", i, "on"), juce::String (names[i]) + " -> CC", K::Toggle);
        add (indexedId ("cc", i, "num"), "CC number", K::Rotary);
    }
    add (ids::ccch, "CC channel", K::Rotary);
    add (ids::vowelsmooth, "Smoothing", K::Rotary);

    const char* learnNames[3] = { "Learn A (ah)", "Learn E (ee)", "Learn O (oo)" };
    for (int i = 0; i < 3; ++i)
    {
        learn[i].setButtonText (learnNames[i]);
        addAndMakeVisible (learn[i]);
        learn[i].onClick = [this, i] { proc.getEngine().requestLearnVowel (i); };
    }
    learnHint.setText ("Optional: press Learn, then hold that vowel steadily for half a second to tune it to your voice.",
                       juce::dontSendNotification);
    learnHint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    addAndMakeVisible (learnHint);
}

void VowelPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto row = r.removeFromTop (28);
    for (auto& b : learn) b.setBounds (row.removeFromLeft (140).reduced (2));
    learnHint.setBounds (r.removeFromTop (22));
    meterArea = r.removeFromTop (4 * 22 + 8);
    flow (r);
}

void VowelPanel::paint (juce::Graphics& g)
{
    const char* names[4] = { "A", "E", "O", "Level" };
    for (int i = 0; i < 4; ++i)
    {
        auto row = meterArea.withTrimmedTop (i * 22).removeFromTop (18);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (13.f);
        g.drawText (names[i], row.removeFromLeft (46), juce::Justification::centredLeft);
        auto bar = row.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar, 3.f);
        g.setColour (kAccent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.f, 1.f, meters[i])), 3.f);
    }
}

void VowelPanel::tick()
{
    auto& eng = proc.getEngine();
    for (int i = 0; i < 4; ++i) meters[i] = eng.tVowel[i].load();
    const float prog = eng.tLearnProgress.load();
    for (int i = 0; i < 3; ++i) learn[i].setEnabled (prog <= 0.f);
    learnHint.setText (prog > 0.f ? "Learning... keep holding the vowel (" + juce::String ((int) (prog * 100.f)) + "%)"
                                  : "Optional: press Learn, then hold that vowel steadily for half a second to tune it to your voice.",
                       juce::dontSendNotification);
    repaint (meterArea);
}

//==============================================================================
VoxMidiEditor::VoxMidiEditor (VoxMidiProcessor& p)
    : AudioProcessorEditor (&p), proc (p), headerCells (p.apvts)
{
    laf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    laf.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    laf.setColour (juce::Slider::thumbColourId, kAccent);
    setLookAndFeel (&laf);

    title.setText ("VoxMidi", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (26.f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (title);

    noteReadout.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 24.f, juce::Font::bold)));
    noteReadout.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (noteReadout);
    addAndMakeVisible (meter);

    using K = ParamCell::Kind;
    headerCells.add (ids::mode, "Mode", K::Combo);
    headerCells.add (ids::inputsrc, "Audio source", K::Combo);
    headerCells.add (ids::range, "Voice range", K::Combo);
    gainCell = headerCells.add (ids::inputgain, "Input gain", K::Linear);
    headerCells.add (ids::monitor, "Monitor input (effect build)", K::Toggle);
    addAndMakeVisible (headerCells);

    keyLabel.setText ("Key: listening...", juce::dontSendNotification);
    addAndMakeVisible (keyLabel);
    addAndMakeVisible (keyButton);
    addAndMakeVisible (keyReset);
    keyButton.setEnabled (false);
    keyButton.onClick = [this] { applySuggestedKey(); };
    keyReset.onClick = [this] { proc.getEngine().requestResetKey(); };

    sourceStatus.setFont (juce::Font (juce::FontOptions (12.f)));
    sourceStatus.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    sourceStatus.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (sourceStatus);

    addAndMakeVisible (audioButton);
    audioButton.setVisible (proc.directInputAvailable());
    audioButton.onClick = [this] { showAudioSettings(); };

    pitchPanel = new PitchPanel (proc);
    chordPanel = new ChordPanel (proc);
    trigPanel = new TriggerPanel (proc);
    vowelPanel = new VowelPanel (proc);
    const auto tabColour = juce::Colour (0xff1a2029);
    tabs.addTab ("Pitch", tabColour, pitchPanel, true);
    tabs.addTab ("Chords / Channels", tabColour, chordPanel, true);
    tabs.addTab ("Triggers", tabColour, trigPanel, true);
    tabs.addTab ("Vowels / CC", tabColour, vowelPanel, true);
    addAndMakeVisible (tabs);

    setSize (920, 660);
    startTimerHz (30);
}

VoxMidiEditor::~VoxMidiEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VoxMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141a22));
    g.setColour (kPanel);
    g.fillRect (0, 0, getWidth(), 150);
}

void VoxMidiEditor::resized()
{
    title.setBounds (12, 8, 140, 32);
    noteReadout.setBounds (160, 8, 300, 32);
    meter.setBounds (470, 16, 290, 18);
    audioButton.setBounds (getWidth() - 140, 10, 128, 28);

    headerCells.setBounds (0, 44, 730, 104);
    headerCells.flow (headerCells.getLocalBounds().reduced (8, 0));

    auto right = juce::Rectangle<int> (740, 50, getWidth() - 752, 96);
    keyLabel.setBounds (right.removeFromTop (22));
    auto btns = right.removeFromTop (28);
    keyButton.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (2));
    keyReset.setBounds (btns.reduced (2));
    sourceStatus.setBounds (right.reduced (0, 4));

    tabs.setBounds (0, 150, getWidth(), getHeight() - 150);
}

void VoxMidiEditor::timerCallback()
{
    auto& e = proc.getEngine();

    const float db = e.tRmsDb.load();
    const float gate = proc.apvts.getRawParameterValue (ids::gate)->load();
    meter.set (juce::jlimit (0.f, 1.f, (db + 70.f) / 70.f), (gate + 70.f) / 70.f);

    const int note = e.tNote.load();
    const float pitch = e.tPitchMidi.load();
    if (note >= 0)
    {
        const int cents = (int) std::lround ((pitch - (float) note) * 100.f);
        noteReadout.setText (noteName (note) + "  " + (cents >= 0 ? "+" : "") + juce::String (cents) + "c  " + juce::String (e.tHz.load(), 1) + " Hz",
                             juce::dontSendNotification);
        noteReadout.setColour (juce::Label::textColourId, juce::Colours::white);
    }
    else if (e.tHz.load() > 0.f)
    {
        noteReadout.setText (noteName ((int) std::lround (pitch)) + "  " + juce::String (e.tHz.load(), 1) + " Hz", juce::dontSendNotification);
        noteReadout.setColour (juce::Label::textColourId, juce::Colours::grey);
    }
    else
    {
        noteReadout.setText ("-", juce::dontSendNotification);
    }

    const int root = e.tKeyRoot.load(), minor = e.tKeyMinor.load();
    suggestedRoot = root;
    suggestedMinor = minor;
    if (root >= 0)
    {
        keyLabel.setText ("Key: " + juce::String (kNoteNames[root]) + (minor ? " minor" : " major"), juce::dontSendNotification);
        keyButton.setEnabled (true);
    }
    else
    {
        keyLabel.setText ("Key: sing a few notes...", juce::dontSendNotification);
        keyButton.setEnabled (false);
    }

    const bool wantsDirect = (int) proc.apvts.getRawParameterValue (ids::inputsrc)->load() == 1 && proc.directInputAvailable();
    if (wantsDirect)
        sourceStatus.setText (proc.directInputActive() ? "Direct input running (the plugin reads the microphone itself)."
                                                       : "Opening the microphone...",
                              juce::dontSendNotification);
    else
        sourceStatus.setText (proc.directInputAvailable() ? "Using the host's audio input." : "Standalone: audio comes from the app's audio settings.",
                              juce::dontSendNotification);
    audioButton.setEnabled (wantsDirect && proc.directInputActive());

    trigPanel->tick();
    vowelPanel->tick();
}

void VoxMidiEditor::applySuggestedKey()
{
    if (suggestedRoot < 0) return;
    auto setChoice = [this] (const char* id, int index)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
            p->endChangeGesture();
        }
    };
    setChoice (ids::root, suggestedRoot);
    setChoice (ids::scale, suggestedMinor ? 2 : 1); // Natural Minor / Major
}

void VoxMidiEditor::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (proc.getDeviceManager(), 1, 2, 0, 0, false, false, true, false);
    selector->setSize (480, 360);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "VoxMidi - microphone";
    o.componentToCentreAround = this;
    o.dialogBackgroundColour = juce::Colour (0xff1a2029);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = false;
    o.resizable = false;
    o.launchAsync();
}
