// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// Offline tests for the VoxMidi engine using synthetic voices / beatbox sounds.
// Build:  g++ -O2 -std=c++17 -I../engine engine_tests.cpp -o engine_tests

#include "../engine/VoxEngine.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>

using namespace vox;

static int g_fail = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (! (cond)) {                                               \
            ++g_fail;                                                 \
            std::printf ("    FAIL (%s:%d): ", __FILE__, __LINE__);   \
            std::printf (__VA_ARGS__);                                \
            std::printf ("\n");                                       \
        }                                                             \
    } while (0)

#ifndef TEST_SR
#define TEST_SR 48000.0
#endif
static constexpr double SR = TEST_SR;

struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed) {}
    float uni() { s = s * 1664525u + 1013904223u; return ((float) (s >> 8) / 16777216.f) * 2.f - 1.f; }
    float u01() { return 0.5f * (uni() + 1.f); }
};

struct Ev { double ms; MidiEv e; };

static std::vector<Ev> run (VoxEngine& eng, const std::vector<float>& sig, const EngineParams& p, int block = 256)
{
    std::vector<Ev> evs;
    static EventBuffer buf;
    for (size_t pos = 0; pos < sig.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, sig.size() - pos);
        eng.process (sig.data() + pos, n, p, buf);
        for (int i = 0; i < buf.size(); ++i)
            evs.push_back ({ 1000.0 * (double) (pos + (size_t) buf[i].offset) / SR, buf[i] });
    }
    return evs;
}

static std::vector<Ev> filter (const std::vector<Ev>& v, MidiEv::Type t, int ch = -1)
{
    std::vector<Ev> r;
    for (auto& e : v) if (e.e.type == t && (ch < 0 || e.e.channel == ch)) r.push_back (e);
    return r;
}

//------------------------------------------------------------------ signal generators
using Fn = std::function<float (double)>;

static std::vector<float> voice (double dur, Fn hz, Fn amp = [] (double) { return 0.3f; }, int harmonics = 12)
{
    const size_t n = (size_t) (dur * SR);
    std::vector<float> s (n);
    double ph = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / SR;
        const double f = hz (t);
        const double a = amp (t);
        if (f <= 0 || a <= 0) { s[i] = 0; continue; }
        ph += 2.0 * 3.14159265358979 * f / SR;
        if (ph > 2 * 3.14159265358979) ph -= 2 * 3.14159265358979;
        double v = 0;
        for (int h = 1; h <= harmonics && h * f < 0.45 * SR; ++h) v += std::sin (h * ph) / h;
        s[i] = (float) (a * v * 0.6);
    }
    return s;
}

static float midiHz (double m) { return (float) (440.0 * std::pow (2.0, (m - 69.0) / 12.0)); }

static EngineParams baseParams()
{
    EngineParams p;
    p.mode = 1;                 // pitch only for the pitch tests
    p.note.bendMode = 0;
    p.note.stick = 0.4f;
    p.ccOn = { false, false, false, false };
    return p;
}

static void silence (std::vector<float>& s, double dur) { s.insert (s.end(), (size_t) (dur * SR), 0.f); }
static void append (std::vector<float>& a, const std::vector<float>& b) { a.insert (a.end(), b.begin(), b.end()); }

//------------------------------------------------------------------ tests
static void testSteadyNote()
{
    std::printf ("[steady note, latency, velocity]\n");
    VoxEngine eng; eng.prepare (SR);
    auto p = baseParams();
    auto sig = voice (0.7, [] (double) { return 220.f; });
    silence (sig, 0.3);
    auto ev = run (eng, sig, p);
    auto on = filter (ev, MidiEv::NoteOn), off = filter (ev, MidiEv::NoteOff);
    CHECK (on.size() == 1 && off.size() == 1, "expected 1 on / 1 off, got %zu / %zu", on.size(), off.size());
    if (! on.empty())
    {
        std::printf ("    note=%d vel=%d latency=%.1f ms\n", on[0].e.data1, on[0].e.data2, on[0].ms);
        CHECK (on[0].e.data1 == 57, "expected note 57 (A3)");
        CHECK (on[0].ms < 70.0, "note-on latency too high: %.1f ms", on[0].ms);
    }
    if (! off.empty()) std::printf ("    note off at %.0f ms (tone ends at 700 ms)\n", off[0].ms);
}

static void testBend()
{
    std::printf ("[pitch bend: +30 cents sharp]\n");
    auto sig = voice (0.8, [] (double) { return midiHz (57.3); });
    silence (sig, 0.2);
    for (int mode = 1; mode <= 2; ++mode)
    {
        VoxEngine eng; eng.prepare (SR);
        auto p = baseParams();
        p.note.bendMode = mode; p.note.bendRange = 2;
        auto ev = run (eng, sig, p);
        auto bends = filter (ev, MidiEv::PitchBend);
        int last = 8192;
        for (auto& b : bends) last = b.e.data1;
        std::printf ("    %s: final bend value %d (centre 8192, +30c over +-2st = 8192+1229)\n", mode == 1 ? "IntelliBend" : "TruBend", last);
        if (mode == 1) CHECK (std::abs (last - 8192) < 300, "IntelliBend should anchor near centre");
        else           CHECK (std::abs (last - (8192 + 1229)) < 450, "TruBend should follow the pitch");
    }
}

static void testScoop()
{
    std::printf ("[scoop up into a note - should not double-trigger]\n");
    // starts 1.5 semitones flat and slides up over 90 ms
    auto hz = [] (double t) { const double m = 57.0 - 1.5 * std::max (0.0, 1.0 - t / 0.09); return midiHz (m); };
    auto sig = voice (0.7, hz);
    silence (sig, 0.3);
    for (int mode = 0; mode <= 2; mode += 2)
    {
        VoxEngine eng; eng.prepare (SR);
        auto p = baseParams();
        p.note.bendMode = mode;
        p.note.stick = 0.6f;
        auto ev = run (eng, sig, p);
        auto on = filter (ev, MidiEv::NoteOn);
        std::printf ("    bend %s: notes:", mode == 0 ? "off" : "TruBend");
        for (auto& e : on) std::printf (" %d@%.0fms", e.e.data1, e.ms);
        std::printf ("\n");
        CHECK (on.size() == 1, "double note on scoop (%zu notes)", on.size());
    }
}

static void testVibrato()
{
    std::printf ("[vibrato +-0.6 semitone]\n");
    VoxEngine eng; eng.prepare (SR);
    auto p = baseParams();
    auto sig = voice (1.2, [] (double t) { return midiHz (62.0 + 0.6 * std::sin (2 * 3.14159265 * 5.5 * t)); });
    silence (sig, 0.2);
    auto ev = run (eng, sig, p);
    auto on = filter (ev, MidiEv::NoteOn);
    CHECK (on.size() == 1, "vibrato produced %zu notes", on.size());
}

static void testTwoNotes()
{
    std::printf ("[legato step A3 -> C4, then a gap and E4]\n");
    VoxEngine eng; eng.prepare (SR);
    auto p = baseParams();
    auto sig = voice (0.6, [] (double t) { return t < 0.3 ? midiHz (57) : midiHz (60); });
    silence (sig, 0.1);
    append (sig, voice (0.4, [] (double) { return midiHz (64); }));
    silence (sig, 0.2);
    auto ev = run (eng, sig, p);
    auto on = filter (ev, MidiEv::NoteOn);
    std::printf ("    notes:");
    for (auto& e : on) std::printf (" %d@%.0fms", e.e.data1, e.ms);
    std::printf ("\n");
    CHECK (on.size() == 3, "expected 3 notes, got %zu", on.size());
    if (on.size() == 3) CHECK (on[0].e.data1 == 57 && on[1].e.data1 == 60 && on[2].e.data1 == 64, "wrong note sequence");
}

static void testOctaveGlitch()
{
    std::printf ("[brief octave jump is ignored]\n");
    VoxEngine eng; eng.prepare (SR);
    auto p = baseParams();
    auto sig = voice (0.8, [] (double t) { return (t > 0.4 && t < 0.416) ? 440.f : 220.f; });
    silence (sig, 0.2);
    auto ev = run (eng, sig, p);
    auto on = filter (ev, MidiEv::NoteOn);
    CHECK (on.size() == 1, "octave glitch caused %zu notes", on.size());
}

static void testRetrigger()
{
    std::printf ("[loudness dip re-articulates the note]\n");
    auto amp = [] (double t) { return (t > 0.4 && t < 0.44) ? 0.012f : 0.3f; };
    auto sig = voice (0.9, [] (double) { return 220.f; }, amp);
    silence (sig, 0.2);
    for (int rt = 0; rt < 2; ++rt)
    {
        VoxEngine eng; eng.prepare (SR);
        auto p = baseParams();
        p.note.retrig = rt == 1;
        auto ev = run (eng, sig, p);
        auto on = filter (ev, MidiEv::NoteOn);
        std::printf ("    retrig %s: %zu notes\n", rt ? "on" : "off", on.size());
        CHECK ((rt == 1 && on.size() == 2) || (rt == 0 && on.size() == 1), "unexpected note count %zu", on.size());
    }
}

static void testChords()
{
    std::printf ("[diatonic chords in C major, melody on ch1, chords on ch2]\n");
    VoxEngine eng; eng.prepare (SR);
    auto p = baseParams();
    p.note.mask = absoluteMask (getScaleTable()[1].intervals, 0);
    p.note.chordMode = 1;
    auto sig = voice (0.4, [] (double) { return midiHz (60.0); });
    silence (sig, 0.15);
    append (sig, voice (0.4, [] (double) { return midiHz (62.0); }));
    silence (sig, 0.2);
    auto ev = run (eng, sig, p);
    auto mel = filter (ev, MidiEv::NoteOn, 1), ch = filter (ev, MidiEv::NoteOn, 2);
    std::printf ("    melody:");
    for (auto& e : mel) std::printf (" %d", e.e.data1);
    std::printf ("  chord:");
    for (auto& e : ch) std::printf (" %d", e.e.data1);
    std::printf ("\n");
    const std::vector<int> want { 64, 67, 65, 69 };
    CHECK (ch.size() == 4, "expected 4 chord notes, got %zu", ch.size());
    if (ch.size() == 4) for (size_t i = 0; i < 4; ++i) CHECK (ch[i].e.data1 == want[i], "chord note %zu: got %d want %d", i, ch[i].e.data1, want[i]);
    CHECK (filter (ev, MidiEv::NoteOff, 2).size() == 4, "chord notes must all be released");
}

static void testScaleSnap()
{
    std::printf ("[scale snapping: out-of-tune singing in C major pentatonic]\n");
    VoxEngine eng; eng.prepare (SR);
    auto p = baseParams();
    p.note.mask = absoluteMask (getScaleTable()[5].intervals, 0); // C D E G A
    // 60.4 -> 60 (C), 62.6 (between D and E, nearer E at 64? 62.6 is nearer D=62) -> 62, 66.8 -> 67
    auto sig = voice (0.35, [] (double) { return midiHz (60.4); });
    silence (sig, 0.15);
    append (sig, voice (0.35, [] (double) { return midiHz (62.6); }));
    silence (sig, 0.15);
    append (sig, voice (0.35, [] (double) { return midiHz (66.8); }));
    silence (sig, 0.2);
    auto on = filter (run (eng, sig, p), MidiEv::NoteOn);
    std::printf ("    notes:");
    for (auto& e : on) std::printf (" %d", e.e.data1);
    std::printf ("\n");
    CHECK (on.size() == 3, "expected 3 notes");
    if (on.size() == 3) CHECK (on[0].e.data1 == 60 && on[1].e.data1 == 62 && on[2].e.data1 == 67, "snap result wrong");
}

static void testKeyDetection()
{
    std::printf ("[key detection: melody in A minor]\n");
    VoxEngine eng; eng.prepare (SR);
    auto p = baseParams();
    const int melody[] = { 57, 59, 60, 62, 64, 62, 60, 59, 57, 64, 60, 57, 55, 57, 59, 60, 64, 57 };
    std::vector<float> sig;
    for (int n : melody)
    {
        append (sig, voice (0.28, [n] (double) { return midiHz (n); }));
        silence (sig, 0.05);
    }
    run (eng, sig, p);
    const int root = eng.tKeyRoot.load(), minor = eng.tKeyMinor.load();
    std::printf ("    suggested root pitch class %d (%s)\n", root, minor ? "minor" : "major");
    CHECK ((root == 9 && minor == 1) || (root == 0 && minor == 0), "expected A minor / C major, got %d %d", root, minor);
}

static void testCpu()
{
    std::printf ("[cpu]\n");
    VoxEngine eng; eng.prepare (SR);
    EngineParams p;
    auto sig = voice (10.0, [] (double t) { return midiHz (55.0 + 8.0 * std::sin (t * 2.0)); });
    const auto t0 = std::chrono::steady_clock::now();
    run (eng, sig, p, 128);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("    10 s of audio processed in %.3f s (%.1fx realtime, ~%.1f%% of one core)\n", sec, 10.0 / sec, 100.0 * sec / 10.0);
    CHECK (sec < 3.0, "too slow");
}

//------------------------------------------------------------------ percussive sounds
static std::vector<float> hitKick (Rng& r)
{
    std::vector<float> s ((size_t) (0.15 * SR));
    const float f0 = 50.f + 12.f * r.u01(), amp = 0.5f + 0.4f * r.u01();
    double ph = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / SR;
        const double f = f0 * (1.0 + 2.5 * std::exp (-t / 0.02));
        ph += 2 * 3.14159265 * f / SR;
        s[i] = (float) (amp * std::sin (ph) * std::exp (-t / 0.06) + 0.03 * r.uni() * std::exp (-t / 0.004));
    }
    return s;
}

static std::vector<float> hitSnare (Rng& r)
{
    std::vector<float> s ((size_t) (0.14 * SR));
    const float amp = 0.4f + 0.4f * r.u01();
    float lp = 0, hp = 0, prev = 0;
    double ph = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / SR;
        const float n = r.uni();
        hp = 0.85f * (hp + n - prev); prev = n;       // roughly high-passed
        lp += 0.55f * (hp - lp);                      // and tamed above ~5-6k
        ph += 2 * 3.14159265 * 190.0 / SR;
        s[i] = (float) (amp * (lp * 1.4 * std::exp (-t / 0.05) + 0.35 * std::sin (ph) * std::exp (-t / 0.03)));
    }
    return s;
}

static std::vector<float> hitHat (Rng& r)
{
    std::vector<float> s ((size_t) (0.07 * SR));
    const float amp = 0.25f + 0.3f * r.u01();
    float p1 = 0, p2 = 0, pp = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / SR;
        const float n = r.uni();
        const float h1 = n - p1; p1 = n;
        const float h2 = h1 - p2; p2 = h1;
        (void) pp;
        s[i] = (float) (amp * 0.5f * h2 * std::exp (-t / 0.012));
    }
    return s;
}

static std::vector<float> hitOf (int kind, Rng& r) { return kind == 0 ? hitKick (r) : (kind == 1 ? hitSnare (r) : hitHat (r)); }

static void testTriggers()
{
    std::printf ("[beatbox triggers: train kick/snare/hat then classify]\n");
    VoxEngine eng; eng.prepare (SR);
    EngineParams p;
    p.mode = 2;
    p.ccOn = { false, false, false, false };
    p.trig.delayFrames = 2;
    Rng rng (777);
    const float zero[256] = {};
    EventBuffer buf;

    for (int pad = 0; pad < 3; ++pad)
    {
        eng.requestTrain (pad);
        eng.process (zero, 256, p, buf);
        std::vector<float> sig;
        for (int i = 0; i < 8; ++i) { silence (sig, 0.25); append (sig, hitOf (pad, rng)); }
        silence (sig, 0.3);
        run (eng, sig, p);
        eng.requestTrain (-1);
        eng.process (zero, 256, p, buf);
    }

    std::array<PadModel, kNumPads> pads;
    eng.getPads (pads);
    for (int i = 0; i < 3; ++i) std::printf ("    pad %d trained=%d examples=%d reject=%.2f\n", i, pads[(size_t) i].trained, pads[(size_t) i].examples, pads[(size_t) i].reject);

    // test sequence
    std::vector<float> sig;
    std::vector<int> truth;
    std::vector<double> startMs;
    silence (sig, 0.3);
    const int total = 36;
    for (int i = 0; i < total; ++i)
    {
        const int kind = (int) (rng.u01() * 2.999f);
        truth.push_back (kind);
        startMs.push_back (1000.0 * (double) sig.size() / SR);
        append (sig, hitOf (kind, rng));
        silence (sig, 0.2 + 0.05 * rng.u01());
    }
    auto ev = run (eng, sig, p);
    auto on = filter (ev, MidiEv::NoteOn);
    int correct = 0, extra = 0;
    double latSum = 0, latMax = 0;
    std::vector<bool> got (truth.size(), false);
    for (auto& e : on)
    {
        // match to the hit that started most recently before this event
        int idx = -1;
        for (size_t i = 0; i < startMs.size(); ++i) if (startMs[i] <= e.ms) idx = (int) i;
        const int expectedNote = p.padNote[(size_t) truth[(size_t) std::max (idx, 0)]];
        if (idx >= 0 && ! got[(size_t) idx] && e.e.data1 == expectedNote)
        {
            got[(size_t) idx] = true; ++correct;
            const double lat = e.ms - startMs[(size_t) idx];
            latSum += lat; latMax = std::max (latMax, lat);
        }
        else ++extra;
    }
    std::printf ("    correct %d/%d, wrong/extra %d, mean latency %.1f ms (max %.1f)\n", correct, total, extra, correct ? latSum / correct : 0.0, latMax);
    CHECK (correct >= total * 9 / 10, "trigger accuracy too low: %d/%d", correct, total);
    CHECK (extra <= 2, "too many wrong/extra triggers: %d", extra);
    CHECK (latMax < 20.0, "trigger latency too high: %.1f ms", latMax);

    // sung notes must not fire drums
    VoxEngine eng2; eng2.prepare (SR);
    std::array<PadModel, kNumPads> ps; eng.getPads (ps); eng2.setPads (ps);
    EngineParams p2 = p;
    auto sung = voice (1.0, [] (double) { return 196.f; });
    silence (sung, 0.2);
    auto ev2 = run (eng2, sung, p2);
    CHECK (filter (ev2, MidiEv::NoteOn).empty(), "a sung note triggered a drum");
}

//------------------------------------------------------------------ vowels
static std::vector<float> vowelSig (double f0, double F1, double F2, double F3, double dur)
{
    const size_t n = (size_t) (dur * SR);
    std::vector<float> s (n);
    struct Res { double a1, a2, b, y1 = 0, y2 = 0; };
    auto mk = [] (double F, double B) {
        Res r;
        const double T = 1.0 / SR, e = std::exp (-3.14159265 * B * T);
        r.a1 = 2 * e * std::cos (2 * 3.14159265 * F * T);
        r.a2 = -e * e;
        r.b = 1 - r.a1 - r.a2;
        return r;
    };
    Res r1 = mk (F1, 80), r2 = mk (F2, 110), r3 = mk (F3, 170), r4 = mk (4200, 250);
    double next = 0, glot = 0;
    for (size_t i = 0; i < n; ++i)
    {
        double x = 0;
        if ((double) i >= next) { x = 1.0; next += SR / f0; }
        glot = x + 0.92 * glot; // spectral tilt
        auto step = [] (Res& r, double in) { const double y = r.b * in + r.a1 * r.y1 + r.a2 * r.y2; r.y2 = r.y1; r.y1 = y; return y; };
        double y = step (r1, glot);
        y = step (r2, y);
        y = step (r3, y);
        y = step (r4, y);
        s[i] = (float) y;
    }
    float pk = 1e-9f;
    for (float v : s) pk = std::max (pk, std::fabs (v));
    for (auto& v : s) v *= 0.3f / pk;   // every vowel at the same, realistic level
    return s;
}

static void testVowels()
{
    std::printf ("[vowels -> controllers (A, E, O on CC20/21/22)]\n");
    struct V { const char* name; double F1, F2, F3; int idx; };
    const V vs[] = { { "A", 730, 1090, 2440, 0 }, { "E", 270, 2290, 3010, 1 }, { "O", 430, 850, 2400, 2 } };
    for (double f0 : { 130.0, 200.0 })
    {
        for (const auto& v : vs)
        {
            VoxEngine eng; eng.prepare (SR);
            EngineParams p;
            p.mode = 1;
            auto sig = vowelSig (f0, v.F1, v.F2, v.F3, 1.0);
            run (eng, sig, p);
            float val[3];
            for (int i = 0; i < 3; ++i) val[i] = eng.tVowel[i].load();
            std::printf ("    f0=%3.0f vowel %s -> A=%.2f E=%.2f O=%.2f\n", f0, v.name, val[0], val[1], val[2]);
            CHECK (val[v.idx] > 0.4f, "vowel %s not detected (%.2f)", v.name, val[v.idx]);
            for (int i = 0; i < 3; ++i) if (i != v.idx) CHECK (val[i] < 0.3f, "vowel %s leaks into %d (%.2f)", v.name, i, val[i]);
        }
    }

    // direct formant estimate accuracy
    Decimator dec; dec.prepare (SR);
    VowelTracker vt; vt.prepare (dec.rate());
    auto sig = vowelSig (140, 730, 1090, 2440, 0.5);
    for (float x : sig) dec.push (x);
    float f1 = 0, f2 = 0;
    const bool ok = vt.estimate (dec, f1, f2);
    std::printf ("    direct estimate for /a/ (730, 1090): ok=%d F1=%.0f F2=%.0f\n", ok, f1, f2);
    CHECK (ok && std::fabs (f1 - 730) < 90 && std::fabs (f2 - 1090) < 150, "formant estimate off");

    // learning a personal vowel
    VoxEngine eng; eng.prepare (SR);
    EngineParams p; p.mode = 1;
    eng.requestLearnVowel (2);
    auto lsig = vowelSig (150, 500, 1500, 2500, 1.2);   // singer's own "O" is unusual
    run (eng, lsig, p);
    const float afterLearn = eng.tVowel[2].load();
    std::printf ("    after learning a custom O (500/1500): O=%.2f\n", afterLearn);
    CHECK (afterLearn > 0.5f, "learned vowel not recognised");
}

int main()
{
    testSteadyNote();
    testBend();
    testScoop();
    testVibrato();
    testTwoNotes();
    testOctaveGlitch();
    testRetrigger();
    testChords();
    testScaleSnap();
    testKeyDetection();
    testTriggers();
    testVowels();
    testCpu();
    std::printf ("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
