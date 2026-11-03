// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// Turns a stream of noisy pitch frames into clean MIDI notes, pitch bend and chords.
//
// This is where most of the "does it feel good to sing into" quality lives:
//  * a note only starts once the pitch has been stable for a few frames (no firing mid-scoop),
//  * single-frame octave errors are rejected,
//  * a stationary change of pitch re-triggers, a moving pitch becomes pitch bend,
//  * a dip in loudness (new syllable) re-articulates the note,
//  * IntelliBend anchors to the note until you deliberately leave it, TruBend follows exactly.
#pragma once

#include "Common.h"
#include "PitchTracker.h"
#include "Scales.h"

namespace vox
{
struct NoteParams
{
    uint16_t mask = 0x0FFF;   // absolute pitch classes allowed (0x0FFF = chromatic)
    int melodyCh = 1;
    int chordCh = 2;
    float gateOpenDb = -48.f; // level needed to start a note (closes 5 dB lower)
    float clarity = 0.82f;    // pitch confidence needed to start a note
    float stick = 0.4f;       // "stickiness": 0 = fastest, 1 = most forgiving
    bool retrig = true;       // re-articulate on loudness dips
    float dipDb = 8.f;
    int transpose = 0;        // semitones applied to output only
    bool dynVel = true;
    int fixedVel = 100;
    float velFloorDb = -42.f;
    float velCeilDb = -14.f;
    int bendMode = 1;         // 0 off, 1 IntelliBend, 2 TruBend
    int bendRange = 2;        // semitones - must match the receiving synth
    int chordMode = 0;        // see chordModeNames
    float holdMs = 45.f;      // how long a note survives pitch dropouts
};

inline const char* const* chordModeNames()
{
    static const char* n[] = { "Off", "Diatonic Triad", "Diatonic 7th", "Power (5th + Oct)", "Octave", "Sus2", "Sus4", "Major", "Minor" };
    return n;
}
constexpr int kNumChordModes = 9;

class NoteTracker
{
public:
    void prepare (float framesPerSecond)
    {
        framePeriodMs = 1000.f / framesPerSecond;
        scaleNotes.reserve (128);
        reset();
    }

    void reset()
    {
        state = State::Idle;
        histN = histPos = pendFrames = gap = unv = candCount = octRun = dipFrames = age = 0;
        dipArmed = false;
        voice = Voice {};
        curNote = -1;
    }

    void setParams (const NoteParams& np)
    {
        if (np.mask != p.mask) rebuildScale (np.mask);
        p = np;
    }

    bool active() const { return state == State::Active; }
    int currentNote() const { return state == State::Active ? curNote : -1; }
    float currentPitch() const { return state == State::Active ? smoothP : 0.f; }
    int pendingFrames() const { return state == State::Pending ? pendFrames : 0; }

    void allNotesOff (int off, EventBuffer& out)
    {
        if (state == State::Active) stopNote (off, out);
        state = State::Idle;
        pendFrames = gap = 0;
    }

    void onFrame (const PitchFrame& f, float rmsDb, int off, EventBuffer& out, bool inhibit)
    {
        const float gateOn = p.gateOpenDb;
        const float gateOff = p.gateOpenDb - 5.f;

        if (state != State::Active)
        {
            const bool voiced = f.valid && f.clarity >= p.clarity && rmsDb >= gateOn;
            if (inhibit) { state = State::Idle; pendFrames = gap = 0; return; }
            if (! voiced)
            {
                if (state == State::Pending && ++gap > 1) { state = State::Idle; pendFrames = gap = 0; }
                return;
            }
            gap = 0;
            if (state == State::Idle)
            {
                state = State::Pending;
                pendFrames = 0; histN = 0; histPos = 0; pendPeakDb = -120.f;
            }
            pushHist (f.midi);
            pendPeakDb = std::max (pendPeakDb, rmsDb);
            ++pendFrames;

            const int n = nOn();
            if (pendFrames >= n)
            {
                float med = 0.f, drift = 0.f;
                const float spread = analyseLast (n, med, drift);
                const float tol = 0.95f - 0.45f * p.stick;
                if (spread <= tol && drift <= 0.3f * tol)
                {
                    smoothP = med;
                    const int note = nearestAllowed (med, p.mask);
                    startNote (note, velFor (pendPeakDb), off, out);
                    state = State::Active;
                    peakDb = pendPeakDb;
                    dipArmed = false; candCount = 0; unv = 0; octRun = 0; age = 0;
                    return;
                }
            }
            if (pendFrames > 220) { state = State::Idle; pendFrames = 0; }
            return;
        }

        // ---------------------------------------------------------------- Active
        const bool voiced = f.valid && f.clarity >= p.clarity - 0.12f && rmsDb >= gateOff;
        if (! voiced)
        {
            ++unv;
            if ((float) unv * framePeriodMs >= p.holdMs || (rmsDb < gateOff && unv >= 2))
                stopNote (off, out);
            return;
        }
        unv = 0;
        ++age;

        float m = f.midi;
        const float dprev = m - smoothP;
        const float ad = std::fabs (dprev);
        if (std::fabs (ad - 12.f) < 1.3f || std::fabs (ad - 24.f) < 1.6f)
        {
            if (++octRun < 6) m = smoothP;  // probably an octave error: ignore for a few frames
            else { smoothP = m; octRun = 0; }
        }
        else octRun = 0;

        pushHist (m);
        float last3[3];
        const int k3 = lastK (3, last3);
        const float med3 = medianOf (last3, k3);
        const float alpha = p.bendMode == 2 ? 0.55f : 0.4f;
        smoothP += alpha * (med3 - smoothP);

        // loudness bookkeeping for re-articulation
        peakDb = std::max (peakDb - 0.12f, rmsDb);
        bool retrigNow = false;
        if (p.retrig)
        {
            if (! dipArmed)
            {
                if (rmsDb < peakDb - p.dipDb) { dipArmed = true; dipMin = rmsDb; dipFrames = 0; }
            }
            else
            {
                dipMin = std::min (dipMin, rmsDb);
                ++dipFrames;
                if (rmsDb > dipMin + p.dipDb * 0.75f) retrigNow = true;
                else if ((float) dipFrames * framePeriodMs > 300.f) dipArmed = false; // slow fade, not a syllable
            }
        }

        const int cur = curNote;
        const float d = smoothP - (float) cur;
        const int cand = nearestAllowed (smoothP, p.mask);
        const int n = nCh();
        bool change = false;

        if (p.bendMode == 0)
        {
            const float thr = 0.62f + 0.2f * p.stick;
            if (cand != cur && std::fabs (d) > thr) ++candCount; else candCount = 0;
            if (candCount >= n)
            {
                float med, drift;
                if (analyseLast (n, med, drift) <= 1.0f) change = true;
            }
        }
        else
        {
            // moving pitch -> bend, stationary pitch -> new note. During the first moments of a note
            // a settling pitch is treated as a scoop and simply bent into, never re-triggered.
            const float scoopMs = 40.f + 160.f * p.stick;
            const bool inScoop = (float) age * framePeriodMs < scoopMs;
            const bool outOfRange = std::fabs (d) > (float) p.bendRange - 0.15f;
            if (cand != cur && std::fabs (d) >= 0.9f) ++candCount; else candCount = 0;
            if (candCount >= n && ! inScoop)
            {
                float med, drift;
                if (analyseLast (n, med, drift) <= 0.6f) change = true;
            }
            if (outOfRange && cand != cur && candCount >= 2) change = true;
        }

        if (change)
        {
            changeNote (cand, velFor (rmsDb), true, off, out);
            age = 0; candCount = 0; dipArmed = false; peakDb = rmsDb;
        }
        else if (retrigNow)
        {
            changeNote (cand, velFor (rmsDb), false, off, out);
            age = 0; candCount = 0; dipArmed = false; peakDb = rmsDb;
        }
        else if (p.bendMode != 0)
        {
            emitBend (bendValueFor (smoothP - (float) curNote), off, out);
        }
    }

private:
    enum class State { Idle, Pending, Active };

    struct Sounding { int ch = 0; int note = -1; };
    struct Voice
    {
        Sounding melody;
        std::array<Sounding, 6> chord {};
        int chordN = 0;
    };

    //-------------------------------------------------------------------------
    int nOn() const { return clampv (2 + (int) std::lround (p.stick * 8.f), 2, 12); }
    int nCh() const { return clampv (3 + (int) std::lround (p.stick * 8.f), 3, 14); }

    void pushHist (float m)
    {
        hist[(size_t) histPos] = m;
        histPos = (histPos + 1) & 15;
        if (histN < 16) ++histN;
    }

    int lastK (int k, float* dst) const
    {
        k = std::min (k, histN);
        for (int i = 0; i < k; ++i)
            dst[i] = hist[(size_t) ((histPos - k + i) & 15)]; // chronological
        return k;
    }

    static float medianOf (float* v, int n)
    {
        if (n <= 0) return 0.f;
        std::sort (v, v + n);
        return v[n / 2];
    }

    /** Spread (after trimming outliers), median, and start-to-end drift of the last k values. */
    float analyseLast (int k, float& median, float& drift) const
    {
        float t[16];
        const int n = lastK (k, t);
        if (n == 0) { median = 0; drift = 0; return 99.f; }
        const int third = std::max (1, n / 3);
        float a = 0, b = 0;
        for (int i = 0; i < third; ++i) { a += t[i]; b += t[n - 1 - i]; }
        drift = std::fabs (b - a) / (float) third;
        std::sort (t, t + n);
        int lo = 0, hi = n - 1;
        if (n >= 5) { lo = 1; hi = n - 2; }
        median = t[n / 2];
        return t[hi] - t[lo];
    }

    int velFor (float db) const
    {
        if (! p.dynVel) return clampv (p.fixedVel, 1, 127);
        const float span = std::max (6.f, p.velCeilDb - p.velFloorDb);
        const float x = clampv ((db - p.velFloorDb) / span, 0.f, 1.f);
        return clampv (1 + (int) std::lround (126.f * x), 1, 127);
    }

    int bendValueFor (float dev) const
    {
        if (p.bendMode == 0) return 8192;
        float bs = dev;
        if (p.bendMode == 1)
        {
            const float dz = 0.45f + 0.5f * p.stick;
            bs = std::fabs (dev) < dz ? 0.f : (dev > 0 ? dev - dz : dev + dz);
        }
        const float n = clampv (bs / (float) std::max (1, p.bendRange), -1.f, 1.f);
        return clampv (8192 + (int) std::lround (n * 8191.f), 0, 16383);
    }

    void emitBend (int v, int off, EventBuffer& out)
    {
        if (v == lastBend) return;
        lastBend = v;
        MidiEv e; e.offset = off; e.type = MidiEv::PitchBend; e.channel = (uint8_t) p.melodyCh; e.data1 = v;
        out.push (e);
    }

    void rebuildScale (uint16_t mask)
    {
        scaleNotes.clear();
        for (int n = 0; n < 128; ++n)
            if (mask == 0 || (mask & (1u << (n % 12)))) scaleNotes.push_back (n);
    }

    int buildChord (int note, int* o) const
    {
        int n = 0;
        auto add = [&] (int v) { if (v >= 0 && v <= 127 && n < 6) o[n++] = v; };
        switch (p.chordMode)
        {
            case 1:
            case 2:
            {
                if (popcount12 (p.mask) >= 12 || scaleNotes.empty())
                {
                    add (note + 4); add (note + 7);
                    if (p.chordMode == 2) add (note + 11);
                }
                else
                {
                    auto it = std::lower_bound (scaleNotes.begin(), scaleNotes.end(), note);
                    const int idx = (int) (it - scaleNotes.begin());
                    const int sz = (int) scaleNotes.size();
                    if (idx + 2 < sz) add (scaleNotes[(size_t) idx + 2]);
                    if (idx + 4 < sz) add (scaleNotes[(size_t) idx + 4]);
                    if (p.chordMode == 2 && idx + 6 < sz) add (scaleNotes[(size_t) idx + 6]);
                }
                break;
            }
            case 3: add (note + 7); add (note + 12); break;
            case 4: add (note + 12); break;
            case 5: add (note + 2); add (note + 7); break;
            case 6: add (note + 5); add (note + 7); break;
            case 7: add (note + 4); add (note + 7); break;
            case 8: add (note + 3); add (note + 7); break;
            default: break;
        }
        return n;
    }

    void startNote (int note, int vel, int off, EventBuffer& out)
    {
        emitBend (p.bendMode == 0 ? 8192 : bendValueFor (smoothP - (float) note), off, out);
        voice = Voice {};
        const int outNote = clampv (note + p.transpose, 0, 127);
        voice.melody = { p.melodyCh, outNote };
        MidiEv e; e.offset = off; e.type = MidiEv::NoteOn; e.channel = (uint8_t) p.melodyCh; e.data1 = outNote; e.data2 = vel;
        out.push (e);

        int cn[6];
        const int n = buildChord (note, cn);
        for (int i = 0; i < n; ++i)
        {
            const int cNote = clampv (cn[i] + p.transpose, 0, 127);
            voice.chord[(size_t) voice.chordN++] = { p.chordCh, cNote };
            MidiEv c; c.offset = off; c.type = MidiEv::NoteOn; c.channel = (uint8_t) p.chordCh;
            c.data1 = cNote; c.data2 = std::max (1, (vel * 85) / 100);
            out.push (c);
        }
        curNote = note;
        curVel = vel;
    }

    void releaseVoice (const Voice& v, int off, EventBuffer& out)
    {
        if (v.melody.note >= 0)
        {
            MidiEv e; e.offset = off; e.type = MidiEv::NoteOff; e.channel = (uint8_t) v.melody.ch; e.data1 = v.melody.note;
            out.push (e);
        }
        for (int i = 0; i < v.chordN; ++i)
        {
            MidiEv e; e.offset = off; e.type = MidiEv::NoteOff; e.channel = (uint8_t) v.chord[(size_t) i].ch;
            e.data1 = v.chord[(size_t) i].note;
            out.push (e);
        }
    }

    void stopNote (int off, EventBuffer& out)
    {
        releaseVoice (voice, off, out);
        voice = Voice {};
        state = State::Idle;
        pendFrames = gap = 0;
        dipArmed = false;
    }

    void changeNote (int note, int vel, bool legato, int off, EventBuffer& out)
    {
        const Voice old = voice;
        if (legato)
        {
            startNote (note, vel, off, out); // new note first so mono synths glide
            releaseVoice (old, off, out);
        }
        else
        {
            releaseVoice (old, off, out);
            startNote (note, vel, off, out);
        }
    }

    NoteParams p;
    State state = State::Idle;
    float framePeriodMs = 4.f;

    std::array<float, 16> hist {};
    int histN = 0, histPos = 0;
    int pendFrames = 0, gap = 0, unv = 0, candCount = 0, octRun = 0, dipFrames = 0, age = 0;
    float pendPeakDb = -120.f, peakDb = -120.f, dipMin = 0.f, smoothP = 0.f;
    bool dipArmed = false;
    int curNote = -1, curVel = 0;
    int lastBend = 8192;
    Voice voice;
    std::vector<int> scaleNotes;
};

} // namespace vox
