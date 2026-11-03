// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
#pragma once

#include "PluginProcessor.h"

//==============================================================================
/** One labelled control bound to one parameter. */
class ParamCell : public juce::Component
{
public:
    enum class Kind { Rotary, Linear, Combo, Toggle };

    ParamCell (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text, Kind k);

    int preferredWidth() const;
    int preferredHeight() const;
    void resized() override;

private:
    Kind kind;
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtt;
};

/** A page of ParamCells flowed left-to-right, with an optional reserved strip at the top. */
class ParamPanel : public juce::Component
{
public:
    ParamPanel (juce::AudioProcessorValueTreeState& s) : state (s) {}

    ParamCell* add (const juce::String& id, const juce::String& text, ParamCell::Kind kind)
    {
        auto* c = new ParamCell (state, id, text, kind);
        cells.add (c);
        addAndMakeVisible (c);
        return c;
    }

    /** Lays the cells out inside 'area'. */
    void flow (juce::Rectangle<int> area);

protected:
    juce::AudioProcessorValueTreeState& state;
    juce::OwnedArray<ParamCell> cells;
};

class LevelMeter : public juce::Component
{
public:
    void set (float v01, float marker01 = -1.f) { value = v01; marker = marker01; repaint(); }
    void paint (juce::Graphics&) override;

private:
    float value = 0.f, marker = -1.f;
};

//==============================================================================
class PitchPanel : public ParamPanel
{
public:
    explicit PitchPanel (VoxMidiProcessor&);
    void resized() override;

private:
    juce::Label scaleLabel;
    juce::OwnedArray<juce::ToggleButton> noteButtons;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> noteAtts;
};

class ChordPanel : public ParamPanel
{
public:
    explicit ChordPanel (VoxMidiProcessor&);
    void resized() override { flow (getLocalBounds().reduced (8)); }
};

class TriggerPanel : public ParamPanel
{
public:
    explicit TriggerPanel (VoxMidiProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick();

private:
    struct Tile
    {
        juce::TextEditor name;
        juce::Slider note;
        juce::TextButton train { "Train" }, clear { "Clear" };
        juce::Label status;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteAtt;
        juce::Rectangle<int> bounds;
    };

    void trainClicked (int pad);

    VoxMidiProcessor& proc;
    juce::OwnedArray<Tile> tiles;
    std::array<vox::PadModel, vox::kNumPads> padCache {};
    int trainingPad = -1, flashPad = -1, flashTicks = 0, lastCounter = 0, refreshTicks = 0;
    juce::Label hint;
};

class VowelPanel : public ParamPanel
{
public:
    explicit VowelPanel (VoxMidiProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick();

private:
    VoxMidiProcessor& proc;
    juce::TextButton learn[3];
    juce::Label learnHint;
    float meters[4] {};
    juce::Rectangle<int> meterArea;
};

//==============================================================================
class VoxMidiEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit VoxMidiEditor (VoxMidiProcessor&);
    ~VoxMidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showAudioSettings();
    void applySuggestedKey();

    VoxMidiProcessor& proc;
    juce::LookAndFeel_V4 laf;

    juce::Label title, noteReadout, keyLabel, sourceStatus;
    LevelMeter meter;
    ParamPanel headerCells;
    ParamCell* gainCell = nullptr;
    juce::TextButton audioButton { "Audio input..." }, keyButton { "Use key" }, keyReset { "Reset" };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    PitchPanel* pitchPanel = nullptr;
    ChordPanel* chordPanel = nullptr;
    TriggerPanel* trigPanel = nullptr;
    VowelPanel* vowelPanel = nullptr;

    int suggestedRoot = -1, suggestedMinor = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxMidiEditor)
};
