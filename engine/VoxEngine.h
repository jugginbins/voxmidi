// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// VoxEngine: the whole voice -> MIDI brain, independent of any plugin framework.
//
//   audio in (mono) -> [triggers]            -> drum notes
//                   -> [decimate] -> [MPM] -> [note tracker] -> melody + chords + pitch bend
//                                 -> [LPC vowels] + [level]  -> CC messages
#pragma once

#include "Common.h"
#include "NoteTracker.h"
#include "PitchTracker.h"
#include "Scales.h"
#include "TriggerEngine.h"
#include "VowelTracker.h"

namespace vox
{
struct EngineParams
{
    int mode = 0;               // 0 = pitch + triggers, 1 = pitch only, 2 = triggers only
    float rangeHz = 80.f;       // lowest expected voice pitch (sets analysis window / latency)
    NoteParams note;
    TriggerParams trig;

    int trigCh = 10;
    std::array<int, kNumPads> padNote { 36, 38, 42, 46, 39, 37, 41, 49 };
    float trigNoteLenMs = 60.f;
    bool trigIgnoreSinging = true;
    float trigInhibitPitchMs = 100.f;

    int ccCh = 1;
    std::array<bool, 4> ccOn { true, true, true, false }; // A, E, O, level
    std::array<int, 4> ccNum { 20, 21, 22, 23 };
    float vowelSmooth = 0.4f;
    float envLoDb = -50.f;
    float envHiDb = -12.f;
};

class VoxEngine
{
public:
    static constexpr int kHopDec = 64; // analysis hop in decimated samples (~4 ms)

    //-- telemetry for the UI (written on the audio thread, read anywhere) ------------
    std::atomic<float> tHz { 0.f }, tClarity { 0.f }, tRmsDb { -120.f }, tPitchMidi { 0.f };
    std::atomic<int> tNote { -1 };
    std::atomic<int> tTrigCounter { 0 }, tTrigPad { -1 };
    std::atomic<float> tVowel[4] {};
    std::atomic<int> tTrainCount { 0 }, tTrainPad { -1 };
    std::atomic<float> tLearnProgress { 0.f };
    std::atomic<int> tKeyRoot { -1 }, tKeyMinor { 0 };
    std::atomic<float> tLatencyMs { 0.f };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        dec.prepare (sampleRate);
        pitch.prepare (dec.rate(), params.rangeHz);
        vowel.prepare (dec.rate());
        framesPerSec = dec.rate() / (float) kHopDec;
        notes.prepare (framesPerSec);
        trig.prepare (sampleRate);
        key.reset();
        hopPhase = 0; vowelPhase = 0; clock = 0;
        for (auto& s : sched) s.used = false;
        lastCC.fill (-1);
        envSmooth = 0.f;
        tLatencyMs = pitch.windowMs() * 0.5f + 1000.f * (float) kHopDec / dec.rate();
        prepared = true;
    }

    /** Process a block of mono audio. out receives MIDI events (offset = sample within this block). */
    void process (const float* in, int n, const EngineParams& ep, EventBuffer& out)
    {
        out.clear();
        if (! prepared) return;
        const int64_t blockStart = clock;

        applyParams (ep, out);
        handleCommands();

        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            ++clock;

            if (params.mode != 1)
            {
                TriggerHit hit;
                if (trig.pushSample (x, i, hit)) onTrigger (hit, blockStart, out);
            }

            if (dec.push (x) && ++hopPhase >= kHopDec)
            {
                hopPhase = 0;
                onHop (i, out);
            }
        }
        flushScheduled (blockStart, n, out);
        if (trig.trainingPad() >= 0) tTrainCount = trig.trainingCount();
    }

    //-- UI-thread commands --------------------------------------------------------
    void requestTrain (int pad)   { cmdTrain = pad; }          // >=0 start, -1 stop + commit
    void requestClearPad (int pad) { cmdClear = pad; }
    void requestLearnVowel (int v) { cmdLearn = v; }
    void requestResetKey()         { cmdKeyReset = true; }

    void getPads (std::array<PadModel, kNumPads>& dst)
    {
        shadowLock.lock();
        dst = pendingPadsFlag ? pendingPads : shadowPads;
        shadowLock.unlock();
    }
    void setPads (const std::array<PadModel, kNumPads>& src)
    {
        shadowLock.lock();
        pendingPads = src; pendingPadsFlag = true;
        shadowLock.unlock();
    }
    VowelRefs getVowelRefs()
    {
        shadowLock.lock();
        const VowelRefs r = pendingRefsFlag ? pendingRefs : shadowRefs;
        shadowLock.unlock();
        return r;
    }
    void setVowelRefs (const VowelRefs& r)
    {
        shadowLock.lock();
        pendingRefs = r; pendingRefsFlag = true;
        shadowLock.unlock();
    }

    // direct access for tests
    const NoteTracker& noteTracker() const { return notes; }
    const TriggerEngine& triggerEngine() const { return trig; }
    const PitchTracker& pitchTracker() const { return pitch; }

private:
    struct Sched { bool used = false; int64_t due = 0; MidiEv ev; };

    void applyParams (const EngineParams& ep, EventBuffer& out)
    {
        if (ep.rangeHz != params.rangeHz) pitch.prepare (dec.rate(), ep.rangeHz);
        if (ep.mode != params.mode)
        {
            notes.allNotesOff (0, out);
            trig.reset();
        }
        // changing the melody/chord channel must not strand a sounding note: NoteTracker remembers
        // the channel each sounding note was started on, so nothing special is needed here.
        notes.setParams (ep.note);
        trig.setParams (ep.trig);
        params = ep;
        tLatencyMs = pitch.windowMs() * 0.5f + 1000.f * (float) kHopDec / dec.rate();
    }

    void handleCommands()
    {
        const int ct = cmdTrain.exchange (kNone);
        if (ct == -1)
        {
            trig.endTraining();
            publishPads();
            tTrainPad = -1; tTrainCount = 0;
        }
        else if (ct >= 0 && ct < kNumPads)
        {
            trig.beginTraining (ct);
            tTrainPad = ct; tTrainCount = 0;
        }
        const int cc = cmdClear.exchange (kNone);
        if (cc >= 0 && cc < kNumPads) { trig.clearPad (cc); publishPads(); }

        const int cl = cmdLearn.exchange (kNone);
        if (cl >= 0 && cl < 3) vowel.beginLearn (cl);
        if (cmdKeyReset.exchange (false)) key.reset();

        if (pendingPadsFlag && shadowLock.tryLock())
        {
            if (pendingPadsFlag) { trig.setPads (pendingPads); shadowPads = pendingPads; pendingPadsFlag = false; }
            shadowLock.unlock();
        }
        if (pendingRefsFlag && shadowLock.tryLock())
        {
            if (pendingRefsFlag) { vowel.refs = pendingRefs; shadowRefs = pendingRefs; pendingRefsFlag = false; }
            shadowLock.unlock();
        }
    }

    void publishPads()
    {
        if (shadowLock.tryLock())
        {
            shadowPads = trig.getPads();
            shadowLock.unlock();
        }
    }

    void onTrigger (const TriggerHit& hit, int64_t blockStart, EventBuffer& out)
    {
        if (params.trigIgnoreSinging && notes.active()) return;

        MidiEv on;
        on.offset = hit.offset; on.type = MidiEv::NoteOn; on.channel = (uint8_t) params.trigCh;
        on.data1 = params.padNote[(size_t) hit.pad]; on.data2 = hit.velocity;
        out.push (on);

        MidiEv off = on;
        off.type = MidiEv::NoteOff; off.data2 = 0;
        const int64_t due = blockStart + hit.offset + (int64_t) (params.trigNoteLenMs * 0.001 * sr);
        schedule (due, off);

        inhibitUntil = clock + (int64_t) (params.trigInhibitPitchMs * 0.001 * sr);
        tTrigPad = hit.pad;
        tTrigCounter.fetch_add (1);
    }

    void schedule (int64_t due, const MidiEv& ev)
    {
        for (auto& s : sched)
            if (! s.used) { s.used = true; s.due = due; s.ev = ev; return; }
        // table full: emit the off at the end of this block rather than leaving a stuck note
        if (pendingImmediateN < 8) pendingImmediate[(size_t) pendingImmediateN++] = ev;
    }

    void flushScheduled (int64_t blockStart, int n, EventBuffer& out)
    {
        for (int i = 0; i < pendingImmediateN; ++i)
        {
            MidiEv e = pendingImmediate[(size_t) i];
            e.offset = n > 0 ? n - 1 : 0;
            out.push (e);
        }
        pendingImmediateN = 0;
        for (auto& s : sched)
        {
            if (! s.used || s.due >= blockStart + n) continue;
            MidiEv e = s.ev;
            e.offset = (int) std::max<int64_t> (0, s.due - blockStart);
            out.push (e);
            s.used = false;
        }
    }

    void onHop (int off, EventBuffer& out)
    {
        // short-term level from the decimated stream (last ~12 ms)
        float tmp[192];
        dec.copyLast (tmp, 192);
        float ss = 0.f;
        for (float v : tmp) ss += v * v;
        const float rmsDb = powToDb (ss / 192.f);
        tRmsDb = rmsDb;

        if (params.mode == 2) return;

        const PitchFrame f = pitch.analyze (dec);
        const bool inhibit = clock < inhibitUntil;
        notes.onFrame (f, rmsDb, off, out, inhibit);

        tHz = f.valid ? f.hz : 0.f;
        tClarity = f.clarity;
        tNote = notes.currentNote();
        tPitchMidi = notes.active() ? notes.currentPitch() : (f.valid ? f.midi : 0.f);

        // key detection from what is actually sung
        key.decay (1.0e-4f);
        if (f.valid && f.clarity > 0.85f && rmsDb > params.note.gateOpenDb)
            key.add ((int) std::lround (f.midi), 1.f / framesPerSec);
        if (++keyPhase >= 125)
        {
            keyPhase = 0;
            const auto s = key.suggest();
            tKeyRoot = s.root; tKeyMinor = s.minor ? 1 : 0;
        }

        // vowels + level at ~125 Hz
        if (++vowelPhase >= 2)
        {
            vowelPhase = 0;
            const bool voiced = f.valid && f.clarity > 0.7f && rmsDb > params.note.gateOpenDb;
            float f1 = 0, f2 = 0;
            const bool ok = voiced && vowel.estimate (dec, f1, f2);
            vowel.update (voiced, ok, f1, f2, params.vowelSmooth);
            tLearnProgress = vowel.learnProgress();

            const float lvl = clampv ((rmsDb - params.envLoDb) / std::max (6.f, params.envHiDb - params.envLoDb), 0.f, 1.f);
            envSmooth += 0.3f * (lvl - envSmooth);
            const float vals[4] = { vowel.value (0), vowel.value (1), vowel.value (2), envSmooth };
            for (int i = 0; i < 4; ++i)
            {
                tVowel[i] = vals[i];
                if (! params.ccOn[(size_t) i]) continue;
                const int v = clampv ((int) std::lround (vals[i] * 127.f), 0, 127);
                if (v == lastCC[(size_t) i]) continue;
                lastCC[(size_t) i] = v;
                MidiEv e;
                e.offset = off; e.type = MidiEv::CC; e.channel = (uint8_t) params.ccCh;
                e.data1 = params.ccNum[(size_t) i]; e.data2 = v;
                out.push (e);
            }
            // learning just finished: publish the new reference formants for state saving
            if (prevLearning >= 0 && vowel.learningVowel() < 0)
                if (shadowLock.tryLock()) { shadowRefs = vowel.refs; shadowLock.unlock(); }
            prevLearning = vowel.learningVowel();
        }
    }

    static constexpr int kNone = -2;

    double sr = 48000.0;
    bool prepared = false;
    EngineParams params;

    Decimator dec;
    PitchTracker pitch;
    NoteTracker notes;
    TriggerEngine trig;
    VowelTracker vowel;
    KeyDetector key;

    float framesPerSec = 250.f;
    int hopPhase = 0, vowelPhase = 0, keyPhase = 0;
    int64_t clock = 0, inhibitUntil = 0;
    float envSmooth = 0.f;
    std::array<int, 4> lastCC {};
    std::array<Sched, 32> sched {};
    std::array<MidiEv, 8> pendingImmediate {};
    int pendingImmediateN = 0;
    int prevLearning = -1;

    std::atomic<int> cmdTrain { kNone }, cmdClear { kNone }, cmdLearn { kNone };
    std::atomic<bool> cmdKeyReset { false };

    SpinLock shadowLock;
    std::array<PadModel, kNumPads> shadowPads {}, pendingPads {};
    VowelRefs shadowRefs, pendingRefs;
    std::atomic<bool> pendingPadsFlag { false }, pendingRefsFlag { false };
};

} // namespace vox
