// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 VoxMidi contributors
// Beatbox / percussive-sound trigger detection and classification.
//
// Onsets come from a fast level-rise detector. Once an onset is seen we wait a few milliseconds
// (delayFrames hops), measure the spectral shape of the burst and compare it with the sounds the
// user trained: 8 band-energy ratios + spectral centroid + flatness, matched with a diagonal
// Mahalanobis-style distance. Anything too far from every trained pad is rejected, so sung notes
// and room noise do not fire drums.
#pragma once

#include "Common.h"

namespace vox
{
constexpr int kNumPads = 8;
constexpr int kNumFeat = 10;

struct PadModel
{
    bool trained = false;
    int examples = 0;
    std::array<float, kNumFeat> mean {};
    std::array<float, kNumFeat> sigma {};
    float reject = 2.5f;
};

struct TriggerParams
{
    float onsetDb = 7.f;       // level rise needed to count as an onset
    float floorDb = -46.f;     // ignore everything quieter than this
    int delayFrames = 2;       // hops to wait before classifying (latency vs accuracy)
    float strict = 1.f;        // multiplies each pad's reject distance
    float lockMs = 30.f;       // minimum time between triggers
    bool dynVel = true;
    int fixedVel = 110;
    float velFloorDb = -40.f;
    float velCeilDb = -12.f;
};

struct TriggerHit
{
    int pad = -1;
    int velocity = 0;
    int offset = 0;
};

class TriggerEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        N = clampv (nextPow2 ((int) (sr * 0.021f)), 512, 4096);
        hop = N / 8;
        fft.init (N);
        re.assign ((size_t) N, 0.f);
        im.assign ((size_t) N, 0.f);
        win.assign ((size_t) N, 0.f);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) N);
        ring.assign ((size_t) (N * 2), 0.f);
        ringMask = N * 2 - 1;
        ringPos = 0;
        relCoef = std::exp (-1.0f / (0.025f * sr));
        accPow.assign ((size_t) N / 2 + 1, 0.f);

        const float edges[kNumFeat - 1] = { 50.f, 120.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f };
        for (int b = 0; b <= 8; ++b)
        {
            const float f = std::min (edges[b], 0.49f * sr);
            bandEdge[(size_t) b] = std::max (1, (int) std::lround (f * (float) N / sr));
        }
        reset();
    }

    void reset()
    {
        env = 0.f; blockSamples = 0;
        prevDb[0] = prevDb[1] = -120.f;
        collecting = false; clock = 0; lockUntil = 0;
        std::fill (ring.begin(), ring.end(), 0.f);
    }

    void setParams (const TriggerParams& tp) { p = tp; }

    //-- training (audio thread) --------------------------------------------------
    void beginTraining (int pad)
    {
        trainPad = pad;
        examples.clear();
        examples.reserve (48);
        trainCount = 0;
    }

    void endTraining()
    {
        if (trainPad >= 0 && examples.size() >= 2) finishTraining();
        trainPad = -1;
        examples.clear();
    }

    int trainingPad() const { return trainPad; }
    int trainingCount() const { return trainCount; }

    void clearPad (int pad) { if (pad >= 0 && pad < kNumPads) pads[(size_t) pad] = PadModel {}; }

    const std::array<PadModel, kNumPads>& getPads() const { return pads; }
    void setPads (const std::array<PadModel, kNumPads>& m) { pads = m; }

    /** Feed one sample. Returns true and fills 'hit' when a trigger fired on this sample. */
    bool pushSample (float x, int offsetInBlock, TriggerHit& hit)
    {
        ring[(size_t) ringPos] = x;
        ringPos = (ringPos + 1) & ringMask;
        ++clock;
        // Peak follower (instant attack, ~25 ms release). A plain short-window RMS would ripple at the
        // frequency of a low kick drum and re-trigger on every cycle.
        env = std::max (std::fabs (x), env * relCoef);
        if (++blockSamples < hop) return false;

        // ---- once per hop
        const float rmsDb = ampToDb (env);
        blockSamples = 0;

        bool fired = false;
        if (collecting)
        {
            accumulate();
            maxDb = std::max (maxDb, rmsDb);
            if (--framesLeft <= 0)
            {
                collecting = false;
                fired = finalise (offsetInBlock, hit);
            }
        }
        else if (rmsDb > p.floorDb && rmsDb - std::min (prevDb[0], prevDb[1]) > p.onsetDb && clock >= lockUntil)
        {
            collecting = true;
            std::fill (accPow.begin(), accPow.end(), 0.f);
            maxDb = rmsDb;
            accumulate();
            framesLeft = std::max (0, p.delayFrames);
            if (framesLeft == 0)
            {
                collecting = false;
                fired = finalise (offsetInBlock, hit);
            }
        }
        prevDb[1] = prevDb[0];
        prevDb[0] = rmsDb;
        return fired;
    }

    float lastDistance() const { return lastDist; }

private:
    void accumulate()
    {
        // Hann-windowed spectrum of the most recent N samples
        for (int i = 0; i < N; ++i)
            re[(size_t) i] = ring[(size_t) ((ringPos - N + i) & ringMask)] * win[(size_t) i];
        std::fill (im.begin(), im.end(), 0.f);
        fft.transform (re.data(), im.data(), false);
        for (int k = 0; k <= N / 2; ++k)
            accPow[(size_t) k] += re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k];
    }

    void features (std::array<float, kNumFeat>& f) const
    {
        float band[8];
        float total = 0.f;
        for (int b = 0; b < 8; ++b)
        {
            float s = 0.f;
            const int lo = bandEdge[(size_t) b], hi = std::max (lo + 1, bandEdge[(size_t) b + 1]);
            for (int k = lo; k < hi && k <= N / 2; ++k) s += accPow[(size_t) k];
            band[b] = s;
            total += s;
        }
        for (int b = 0; b < 8; ++b)
            f[(size_t) b] = clampv (powToDb (band[b] / (total + 1.0e-18f)), -40.f, 0.f);

        // centroid + flatness over 100 Hz .. 12 kHz
        const int k0 = std::max (1, (int) (100.f * (float) N / sr));
        const int k1 = std::min (N / 2, (int) (12000.f * (float) N / sr));
        double num = 0, den = 0, logSum = 0;
        int cnt = 0;
        for (int k = k0; k <= k1; ++k)
        {
            const double pw = (double) accPow[(size_t) k] + 1.0e-18;
            num += pw * (double) k * (double) sr / (double) N;
            den += pw;
            logSum += std::log (pw);
            ++cnt;
        }
        const double centroid = den > 0 ? num / den : 1000.0;
        const double arith = den / std::max (1, cnt);
        const double geo = std::exp (logSum / std::max (1, cnt));
        f[8] = (float) std::log2 (std::max (50.0, centroid) / 1000.0);
        f[9] = (float) clampv (geo / std::max (arith, 1.0e-30), 0.0, 1.0);
    }

    static float sigmaFloor (int i) { return i < 8 ? 3.0f : (i == 8 ? 0.3f : 0.1f); }

    float distance (const std::array<float, kNumFeat>& f, const PadModel& m) const
    {
        float s = 0.f;
        for (int i = 0; i < kNumFeat; ++i)
        {
            const float d = (f[(size_t) i] - m.mean[(size_t) i]) / m.sigma[(size_t) i];
            s += d * d;
        }
        return std::sqrt (s / (float) kNumFeat);
    }

    bool finalise (int offset, TriggerHit& hit)
    {
        std::array<float, kNumFeat> f {};
        features (f);
        lockUntil = clock + (int64_t) (p.lockMs * 0.001f * sr);

        if (trainPad >= 0)
        {
            if (examples.size() < 48) examples.push_back (f);
            trainCount = (int) examples.size();
            return false;
        }

        int best = -1;
        float bestD = 1.0e9f;
        for (int i = 0; i < kNumPads; ++i)
        {
            if (! pads[(size_t) i].trained) continue;
            const float d = distance (f, pads[(size_t) i]);
            if (d < bestD) { bestD = d; best = i; }
        }
        lastDist = bestD;
        if (best < 0 || bestD > pads[(size_t) best].reject * p.strict) return false;

        hit.pad = best;
        hit.offset = offset;
        if (p.dynVel)
        {
            const float span = std::max (6.f, p.velCeilDb - p.velFloorDb);
            hit.velocity = clampv (1 + (int) std::lround (126.f * clampv ((maxDb - p.velFloorDb) / span, 0.f, 1.f)), 1, 127);
        }
        else hit.velocity = clampv (p.fixedVel, 1, 127);
        return true;
    }

    void finishTraining()
    {
        PadModel m;
        const int n = (int) examples.size();
        for (int i = 0; i < kNumFeat; ++i)
        {
            float mean = 0.f;
            for (auto& e : examples) mean += e[(size_t) i];
            mean /= (float) n;
            float var = 0.f;
            for (auto& e : examples) var += (e[(size_t) i] - mean) * (e[(size_t) i] - mean);
            var /= (float) std::max (1, n - 1);
            const float fl = sigmaFloor (i);
            m.mean[(size_t) i] = mean;
            m.sigma[(size_t) i] = std::sqrt (var + fl * fl);
        }
        float maxD = 0.f;
        for (auto& e : examples) maxD = std::max (maxD, distance (e, m));
        m.reject = clampv (maxD * 1.6f, 2.0f, 4.0f);
        m.trained = true;
        m.examples = n;
        pads[(size_t) trainPad] = m;
    }

    TriggerParams p;
    float sr = 48000.f;
    int N = 1024, hop = 128;
    FFT fft;
    std::vector<float> re, im, win, ring, accPow;
    int ringMask = 2047, ringPos = 0;
    std::array<int, 9> bandEdge {};

    int blockSamples = 0;
    float env = 0.f, relCoef = 0.999f;
    float prevDb[2] { -120.f, -120.f };
    bool collecting = false;
    int framesLeft = 0;
    float maxDb = -120.f;
    int64_t clock = 0, lockUntil = 0;
    float lastDist = 0.f;

    std::array<PadModel, kNumPads> pads {};
    int trainPad = -1, trainCount = 0;
    std::vector<std::array<float, kNumFeat>> examples;
};

} // namespace vox
