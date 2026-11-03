// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// Vowel tracking: LPC spectral envelope -> first two formants -> "amount of A / E / O" controllers.
// The reference formant positions are sensible adult defaults and can be re-learned per singer.
#pragma once

#include "Common.h"

namespace vox
{
struct VowelRefs
{
    std::array<float, 3> f1 { 730.f, 300.f, 400.f };   // A ("ah"), E ("ee"), O ("oo")
    std::array<float, 3> f2 { 1100.f, 2300.f, 800.f };
};

class VowelTracker
{
public:
    void prepare (float decimatedRate)
    {
        srDec = decimatedRate;
        order = clampv ((int) std::lround (2.0f + srDec / 1000.0f), 10, 18);
        L = std::max (64, (int) (srDec * 0.024f));
        buf.assign ((size_t) L, 0.f);
        ac.assign ((size_t) order + 1, 0.f);
        a.assign ((size_t) order + 1, 0.f);
        tmpA.assign ((size_t) order + 1, 0.f);

        const float fMax = std::min (4000.f, 0.46f * srDec);
        nGrid = (int) ((fMax - kFMin) / kStep) + 1;
        gridCos.assign ((size_t) nGrid * (size_t) (order + 1), 0.f);
        gridSin.assign ((size_t) nGrid * (size_t) (order + 1), 0.f);
        env.assign ((size_t) nGrid, 0.f);
        for (int g = 0; g < nGrid; ++g)
        {
            const float w = 2.0f * kPi * (kFMin + kStep * (float) g) / srDec;
            for (int k = 0; k <= order; ++k)
            {
                gridCos[(size_t) g * (size_t) (order + 1) + (size_t) k] = std::cos (w * (float) k);
                gridSin[(size_t) g * (size_t) (order + 1) + (size_t) k] = std::sin (w * (float) k);
            }
        }
        lagWin.assign ((size_t) order + 1, 1.f);
        for (int k = 0; k <= order; ++k)
        {
            const float x = 2.0f * kPi * 40.0f * (float) k / srDec; // 40 Hz bandwidth expansion
            lagWin[(size_t) k] = std::exp (-0.5f * x * x);
        }
        reset();
    }

    void reset()
    {
        val.fill (0.f);
        learning = -1; learnCount = 0;
    }

    /** Estimate F1/F2 from the most recent audio. Returns false if no sensible pair was found. */
    bool estimate (const Decimator& dec, float& f1, float& f2)
    {
        dec.copyLast (buf.data(), L);
        // pre-emphasis + Hamming window
        float prev = 0.f;
        for (int n = 0; n < L; ++n)
        {
            const float x = buf[(size_t) n];
            const float y = x - 0.97f * prev;
            prev = x;
            buf[(size_t) n] = y * (0.54f - 0.46f * std::cos (2.0f * kPi * (float) n / (float) (L - 1)));
        }
        for (int k = 0; k <= order; ++k)
        {
            float s = 0.f;
            for (int n = k; n < L; ++n) s += buf[(size_t) n] * buf[(size_t) (n - k)];
            ac[(size_t) k] = s;
        }
        if (ac[0] < 1.0e-8f) return false;
        ac[0] *= 1.0001f;
        for (int k = 1; k <= order; ++k) ac[(size_t) k] *= lagWin[(size_t) k];

        // Levinson-Durbin
        std::fill (a.begin(), a.end(), 0.f);
        a[0] = 1.f;
        float err = ac[0];
        for (int i = 1; i <= order; ++i)
        {
            float acc = ac[(size_t) i];
            for (int j = 1; j < i; ++j) acc += a[(size_t) j] * ac[(size_t) (i - j)];
            const float k = -acc / err;
            tmpA = a;
            for (int j = 1; j < i; ++j) a[(size_t) j] = tmpA[(size_t) j] + k * tmpA[(size_t) (i - j)];
            a[(size_t) i] = k;
            err *= (1.f - k * k);
            if (err <= 1.0e-12f) return false;
        }

        // spectral envelope on a fixed grid
        float maxEnv = -1.0e9f;
        for (int g = 0; g < nGrid; ++g)
        {
            float r = 0.f, im = 0.f;
            const float* c = &gridCos[(size_t) g * (size_t) (order + 1)];
            const float* s = &gridSin[(size_t) g * (size_t) (order + 1)];
            for (int k = 0; k <= order; ++k) { r += a[(size_t) k] * c[k]; im -= a[(size_t) k] * s[k]; }
            env[(size_t) g] = -10.0f * std::log10 (r * r + im * im + 1.0e-12f);
            maxEnv = std::max (maxEnv, env[(size_t) g]);
        }

        float peaks[16];
        int np = 0;
        for (int g = 1; g < nGrid - 1 && np < 16; ++g)
        {
            const float l = env[(size_t) g - 1], c = env[(size_t) g], r = env[(size_t) g + 1];
            if (c > l && c >= r && c > maxEnv - 30.f)
            {
                const float den = l - 2.f * c + r;
                const float sh = std::fabs (den) > 1.0e-6f ? clampv (0.5f * (l - r) / den, -1.f, 1.f) : 0.f;
                peaks[np++] = kFMin + kStep * ((float) g + sh);
            }
        }
        int i1 = -1;
        for (int i = 0; i < np; ++i)
            if (peaks[i] >= 200.f && peaks[i] <= 1000.f) { i1 = i; break; }
        if (i1 < 0) return false;
        int i2 = -1;
        for (int i = i1 + 1; i < np; ++i)
            if (peaks[i] >= peaks[i1] + 180.f && peaks[i] <= 3400.f) { i2 = i; break; }
        if (i2 < 0) return false;
        f1 = peaks[i1];
        f2 = peaks[i2];
        return true;
    }

    /** Call at ~125 Hz. smooth in 0..1 (more = slower, steadier controllers). */
    void update (bool voiced, bool haveFormants, float f1, float f2, float smooth)
    {
        float tgt[3] = { 0.f, 0.f, 0.f };
        if (voiced && haveFormants)
        {
            lastF1 = f1; lastF2 = f2;
            for (int v = 0; v < 3; ++v)
            {
                const float d1 = std::log (f1 / refs.f1[(size_t) v]);
                const float d2 = std::log (f2 / refs.f2[(size_t) v]);
                const float d = std::sqrt (d1 * d1 + d2 * d2) / 0.4f;
                tgt[v] = std::exp (-d * d);
            }
            if (learning >= 0)
            {
                sumL1 += std::log (f1); sumL2 += std::log (f2);
                if (++learnCount >= kLearnFrames)
                {
                    refs.f1[(size_t) learning] = std::exp (sumL1 / (float) learnCount);
                    refs.f2[(size_t) learning] = std::exp (sumL2 / (float) learnCount);
                    learning = -1;
                }
            }
        }
        else if (voiced)
        {
            for (int v = 0; v < 3; ++v) tgt[v] = val[(size_t) v]; // hold through brief formant failures
        }
        const float up = 0.8f - 0.65f * smooth;
        const float down = 0.4f - 0.35f * smooth;
        for (int v = 0; v < 3; ++v)
        {
            const float a2 = tgt[v] > val[(size_t) v] ? up : down;
            val[(size_t) v] += a2 * (tgt[v] - val[(size_t) v]);
        }
    }

    float value (int v) const { return val[(size_t) v]; }
    float f1() const { return lastF1; }
    float f2() const { return lastF2; }

    void beginLearn (int v) { learning = v; learnCount = 0; sumL1 = sumL2 = 0.f; }
    int learningVowel() const { return learning; }
    float learnProgress() const { return learning < 0 ? 0.f : (float) learnCount / (float) kLearnFrames; }

    VowelRefs refs;

private:
    static constexpr float kFMin = 180.f;
    static constexpr float kStep = 30.f;
    static constexpr int kLearnFrames = 50;

    float srDec = 16000.f;
    int order = 16, L = 384, nGrid = 128;
    std::vector<float> buf, ac, a, tmpA, gridCos, gridSin, env, lagWin;
    std::array<float, 3> val {};
    float lastF1 = 0.f, lastF2 = 0.f;
    int learning = -1, learnCount = 0;
    float sumL1 = 0.f, sumL2 = 0.f;
};

} // namespace vox
