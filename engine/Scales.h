// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// Scales, quantisation helpers and key detection.
#pragma once

#include "Common.h"

namespace vox
{
constexpr uint16_t bitsOf (std::initializer_list<int> l)
{
    uint16_t m = 0;
    for (int i : l) m = (uint16_t) (m | (1u << i));
    return m;
}

struct ScaleInfo
{
    const char* name;
    uint16_t intervals; // bit i set = i semitones above the root
};

constexpr int kNumScales = 15;

inline const ScaleInfo* getScaleTable()
{
    static const ScaleInfo table[kNumScales] = {
        { "Chromatic",        0x0FFF },
        { "Major",            bitsOf ({ 0, 2, 4, 5, 7, 9, 11 }) },
        { "Natural Minor",    bitsOf ({ 0, 2, 3, 5, 7, 8, 10 }) },
        { "Harmonic Minor",   bitsOf ({ 0, 2, 3, 5, 7, 8, 11 }) },
        { "Melodic Minor",    bitsOf ({ 0, 2, 3, 5, 7, 9, 11 }) },
        { "Major Pentatonic", bitsOf ({ 0, 2, 4, 7, 9 }) },
        { "Minor Pentatonic", bitsOf ({ 0, 3, 5, 7, 10 }) },
        { "Blues",            bitsOf ({ 0, 3, 5, 6, 7, 10 }) },
        { "Dorian",           bitsOf ({ 0, 2, 3, 5, 7, 9, 10 }) },
        { "Phrygian",         bitsOf ({ 0, 1, 3, 5, 7, 8, 10 }) },
        { "Lydian",           bitsOf ({ 0, 2, 4, 6, 7, 9, 11 }) },
        { "Mixolydian",       bitsOf ({ 0, 2, 4, 5, 7, 9, 10 }) },
        { "Locrian",          bitsOf ({ 0, 1, 3, 5, 6, 8, 10 }) },
        { "Whole Tone",       bitsOf ({ 0, 2, 4, 6, 8, 10 }) },
        { "Diminished",       bitsOf ({ 0, 2, 3, 5, 6, 8, 9, 11 }) },
    };
    return table;
}

/** Rotate a root-relative interval mask into an absolute pitch-class mask. */
inline uint16_t absoluteMask (uint16_t intervals, int root)
{
    uint16_t m = 0;
    for (int i = 0; i < 12; ++i)
        if (intervals & (1u << i)) m = (uint16_t) (m | (1u << ((i + root) % 12)));
    return m;
}

inline int popcount12 (uint16_t m)
{
    int c = 0;
    for (int i = 0; i < 12; ++i) c += (m >> i) & 1;
    return c;
}

/** Nearest allowed note to a fractional MIDI pitch (mask = absolute pitch classes; 0 = chromatic). */
inline int nearestAllowed (float midi, uint16_t mask)
{
    const int base = (int) std::floor (midi);
    if (mask == 0 || mask == 0x0FFF)
        return clampv ((int) std::lround (midi), 0, 127);

    int bestNote = clampv ((int) std::lround (midi), 0, 127);
    float bestDist = 1.0e9f;
    for (int c = base - 7; c <= base + 8; ++c)
    {
        if (c < 0 || c > 127) continue;
        if (! (mask & (1u << (((c % 12) + 12) % 12)))) continue;
        const float d = std::fabs (midi - (float) c);
        if (d < bestDist) { bestDist = d; bestNote = c; }
    }
    return bestNote;
}

//==============================================================================
/** Krumhansl-Schmuckler key finding on a decaying pitch-class histogram. */
class KeyDetector
{
public:
    struct Suggestion { int root = -1; bool minor = false; float score = 0.f; };

    void reset() { hist.fill (0.f); total = 0.f; }

    void add (int midiNote, float weight)
    {
        hist[(size_t) (((midiNote % 12) + 12) % 12)] += weight;
        total += weight;
    }

    void decay (float f)
    {
        for (auto& h : hist) h *= (1.f - f);
        total *= (1.f - f);
    }

    Suggestion suggest() const
    {
        Suggestion s;
        if (total < 1.5f) return s; // roughly 1.5 s of voiced material needed
        static const float maj[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static const float min[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

        float best = -2.f;
        for (int minor = 0; minor < 2; ++minor)
        {
            const float* prof = minor ? min : maj;
            for (int r = 0; r < 12; ++r)
            {
                float mh = 0, mp = 0;
                for (int i = 0; i < 12; ++i) { mh += hist[(size_t) ((i + r) % 12)]; mp += prof[i]; }
                mh /= 12.f; mp /= 12.f;
                float num = 0, dh = 0, dp = 0;
                for (int i = 0; i < 12; ++i)
                {
                    const float a = hist[(size_t) ((i + r) % 12)] - mh, b = prof[i] - mp;
                    num += a * b; dh += a * a; dp += b * b;
                }
                const float corr = (dh > 0 && dp > 0) ? num / std::sqrt (dh * dp) : 0.f;
                if (corr > best) { best = corr; s.root = r; s.minor = minor == 1; s.score = corr; }
            }
        }
        return s;
    }

private:
    std::array<float, 12> hist {};
    float total = 0.f;
};

} // namespace vox
