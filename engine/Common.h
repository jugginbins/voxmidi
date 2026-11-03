// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// VoxMidi engine - common utilities (header-only, no dependencies, real-time safe).
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vox
{
constexpr float kPi = 3.14159265358979323846f;

inline float hzToMidi (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float m) { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }
inline float ampToDb (float a) { return 20.0f * std::log10 (std::max (a, 1.0e-9f)); }
inline float powToDb (float p) { return 10.0f * std::log10 (std::max (p, 1.0e-18f)); }

template <typename T>
inline T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline int nextPow2 (int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

//==============================================================================
/** A MIDI event produced by the engine. offset is in samples from the start of the block. */
struct MidiEv
{
    enum Type : uint8_t { NoteOn, NoteOff, CC, PitchBend };

    int offset = 0;
    Type type = NoteOn;
    uint8_t channel = 1; // 1..16
    int data1 = 0;       // note / cc number / 14-bit bend value
    int data2 = 0;       // velocity / cc value
};

/** Fixed capacity event list - never allocates. */
class EventBuffer
{
public:
    static constexpr int kCap = 1024;

    void clear() { n = 0; }
    void push (const MidiEv& e) { if (n < kCap) ev[(size_t) n++] = e; }
    int size() const { return n; }
    const MidiEv& operator[] (int i) const { return ev[(size_t) i]; }
    MidiEv& operator[] (int i) { return ev[(size_t) i]; }

private:
    std::array<MidiEv, kCap> ev {};
    int n = 0;
};

//==============================================================================
/** Radix-2 complex FFT, in place, split real/imag arrays. */
class FFT
{
public:
    void init (int size)
    {
        n = size;
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        cosT.assign ((size_t) n / 2, 0.f);
        sinT.assign ((size_t) n / 2, 0.f);
        for (int k = 0; k < n / 2; ++k)
        {
            cosT[(size_t) k] = std::cos (2.0f * kPi * (float) k / (float) n);
            sinT[(size_t) k] = std::sin (2.0f * kPi * (float) k / (float) n);
        }
    }

    int size() const { return n; }

    void transform (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const float wr = cosT[(size_t) (j * step)];
                    const float wi = inverse ? sinT[(size_t) (j * step)] : -sinT[(size_t) (j * step)];
                    const int a = i + j, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
            }
        }
        if (inverse)
        {
            const float s = 1.0f / (float) n;
            for (int i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
        }
    }

private:
    int n = 0;
    std::vector<int> rev;
    std::vector<float> cosT, sinT;
};

//==============================================================================
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowpass (float fc, float sr, float q)
    {
        const float w0 = 2.0f * kPi * fc / sr;
        const float cw = std::cos (w0), alpha = std::sin (w0) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - cw) * 0.5f / a0;
        b1 = (1.0f - cw) / a0;
        b2 = b0;
        a1 = -2.0f * cw / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void reset() { z1 = z2 = 0; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
/** Anti-aliased decimator to roughly 16 kHz. Voice pitch and formants live well below 8 kHz,
    so every analysis stage except the percussive triggers runs on this stream - it keeps CPU tiny. */
class Decimator
{
public:
    static constexpr int kRing = 8192;

    void prepare (double sampleRate)
    {
        factor = std::max (1, (int) std::lround (sampleRate / 16000.0));
        srDec = (float) (sampleRate / factor);
        const float fc = std::min (0.42f * srDec, 0.45f * (float) sampleRate);
        lp[0].setLowpass (fc, (float) sampleRate, 0.5412f);
        lp[1].setLowpass (fc, (float) sampleRate, 1.3066f);
        lp[0].reset(); lp[1].reset();
        ring.assign (kRing, 0.f);
        w = 0; phase = 0;
    }

    /** Returns true when a new decimated sample was produced. */
    bool push (float x)
    {
        const float y = lp[1].process (lp[0].process (x));
        if (++phase >= factor)
        {
            phase = 0;
            ring[(size_t) w] = y;
            w = (w + 1) & (kRing - 1);
            return true;
        }
        return false;
    }

    void copyLast (float* dst, int count) const
    {
        int idx = (w - count) & (kRing - 1);
        for (int i = 0; i < count; ++i)
        {
            dst[i] = ring[(size_t) idx];
            idx = (idx + 1) & (kRing - 1);
        }
    }

    float rate() const { return srDec; }
    int getFactor() const { return factor; }

private:
    int factor = 3, phase = 0, w = 0;
    float srDec = 16000.f;
    Biquad lp[2];
    std::vector<float> ring;
};

//==============================================================================
/** Tiny spin lock for UI -> audio hand-over of small state blobs. */
class SpinLock
{
public:
    void lock() { while (flag.test_and_set (std::memory_order_acquire)) {} }
    bool tryLock() { return ! flag.test_and_set (std::memory_order_acquire); }
    void unlock() { flag.clear (std::memory_order_release); }

private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};

} // namespace vox
