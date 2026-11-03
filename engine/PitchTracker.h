// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// McLeod Pitch Method (MPM) tracker for a monophonic voice, running on the decimated stream.
// Reference: McLeod & Wyvill, "A smarter way to find pitch" (2005).
#pragma once

#include "Common.h"

namespace vox
{
struct PitchFrame
{
    bool valid = false;
    float hz = 0.f;
    float midi = 0.f;
    float clarity = 0.f; // 0..1, height of the chosen NSDF peak
};

class PitchTracker
{
public:
    /** minHz decides the analysis window: lower voices need a longer window (= more latency). */
    void prepare (float decimatedRate, float minHz)
    {
        srDec = decimatedRate;
        lowHz = minHz;
        W = clampv ((int) std::lround (2.4f * srDec / minHz), 128, 1024);
        nfft = nextPow2 (2 * W);
        fft.init (nfft);
        re.assign ((size_t) nfft, 0.f);
        im.assign ((size_t) nfft, 0.f);
        x.assign ((size_t) W, 0.f);
        nsdf.assign ((size_t) W, 0.f);
        tauMin = std::max (2, (int) (srDec / 1200.0f));
        tauMax = W / 2;
    }

    int windowSamples() const { return W; }
    float windowMs() const { return 1000.f * (float) W / srDec; }

    PitchFrame analyze (const Decimator& dec)
    {
        PitchFrame out;
        dec.copyLast (x.data(), W);

        float mean = 0.f;
        for (int i = 0; i < W; ++i) mean += x[(size_t) i];
        mean /= (float) W;

        float sumsq = 0.f;
        for (int i = 0; i < W; ++i)
        {
            x[(size_t) i] -= mean;
            sumsq += x[(size_t) i] * x[(size_t) i];
        }
        if (sumsq < 1.0e-9f) return out;

        // Autocorrelation via FFT
        std::fill (re.begin(), re.end(), 0.f);
        std::fill (im.begin(), im.end(), 0.f);
        std::copy (x.begin(), x.end(), re.begin());
        fft.transform (re.data(), im.data(), false);
        for (int i = 0; i < nfft; ++i)
        {
            re[(size_t) i] = re[(size_t) i] * re[(size_t) i] + im[(size_t) i] * im[(size_t) i];
            im[(size_t) i] = 0.f;
        }
        fft.transform (re.data(), im.data(), true);

        // Normalised square difference function
        float m = 2.0f * sumsq;
        nsdf[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            m -= x[(size_t) (tau - 1)] * x[(size_t) (tau - 1)] + x[(size_t) (W - tau)] * x[(size_t) (W - tau)];
            nsdf[(size_t) tau] = m > 1.0e-12f ? 2.0f * re[(size_t) tau] / m : 0.f;
        }

        // Key maxima: highest point between each positive-going and negative-going zero crossing
        int peaks[24];
        int np = 0;
        float best = -1.f;
        int tau = 1;
        while (tau < tauMax && nsdf[(size_t) tau] > 0.f) ++tau; // skip the lobe around lag 0
        while (tau < tauMax)
        {
            while (tau < tauMax && nsdf[(size_t) tau] <= 0.f) ++tau;
            if (tau >= tauMax) break;
            int bi = tau;
            while (tau < tauMax && nsdf[(size_t) tau] > 0.f)
            {
                if (nsdf[(size_t) tau] > nsdf[(size_t) bi]) bi = tau;
                ++tau;
            }
            if (bi >= tauMin && bi < tauMax && np < 24)
            {
                peaks[np++] = bi;
                best = std::max (best, nsdf[(size_t) bi]);
            }
        }
        if (np == 0) return out;

        const float thresh = 0.92f * best;
        int chosen = peaks[0];
        for (int i = 0; i < np; ++i)
            if (nsdf[(size_t) peaks[i]] >= thresh) { chosen = peaks[i]; break; }

        // Parabolic interpolation
        const float a = nsdf[(size_t) chosen - 1], b = nsdf[(size_t) chosen], c = nsdf[(size_t) chosen + 1];
        const float den = a - 2.0f * b + c;
        float shift = 0.f, height = b;
        if (std::fabs (den) > 1.0e-9f)
        {
            shift = clampv (0.5f * (a - c) / den, -1.f, 1.f);
            height = b - 0.25f * (a - c) * shift;
        }
        const float tauF = (float) chosen + shift;
        out.hz = srDec / tauF;
        out.clarity = clampv (height, 0.f, 1.f);
        out.midi = hzToMidi (out.hz);
        out.valid = out.hz >= lowHz * 0.8f && out.hz <= 1200.f;
        return out;
    }

private:
    float srDec = 16000.f, lowHz = 80.f;
    int W = 512, nfft = 1024, tauMin = 13, tauMax = 256;
    FFT fft;
    std::vector<float> re, im, x, nsdf;
};

} // namespace vox
