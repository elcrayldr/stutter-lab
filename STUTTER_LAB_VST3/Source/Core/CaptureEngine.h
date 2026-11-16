// CAPTURE store. The audio thread is the only writer while recording. Everything else
// (snapshots, clear, load, peaks) is non-realtime and coordinates through atomics only:
// no mutex, no spin-lock, no allocation in process().
#pragma once
#include <chrono>
#include <thread>
#include <mutex>
#include "Common.h"

namespace cray
{
class CaptureEngine
{
public:
    // Non-realtime. Allocates. Handles sample-rate changes by resampling any existing take.
    void prepare (double sr, double maxSeconds)
    {
        std::lock_guard<std::mutex> guard (controlMutex);
        beginExclusive();
        requestStopAndWait();
        StereoBuffer keep;
        if (const size_t n = (size_t) num.load (std::memory_order_acquire); n > 0 && rate.load (std::memory_order_acquire) > 0.0)
        {
            keep.sampleRate = rate.load (std::memory_order_acquire);
            keep.l.assign (l.begin(), l.begin() + (long) n);
            keep.r.assign (r.begin(), r.begin() + (long) n);
        }
        if (! pending.empty() && keep.empty()) { keep = std::move (pending); pending = {}; }
        rate.store (sr, std::memory_order_release);
        const size_t cap = (size_t) std::max (1.0, std::ceil (sr * maxSeconds));
        l.assign (cap, 0.0f); r.assign (cap, 0.0f);
        capacity.store (cap, std::memory_order_release);
        num.store (0);
        if (! keep.empty()) copyIn (resampleBuffer (keep, sr));
        endExclusive();
    }

    // Any thread
    void requestStart() noexcept { startReq.store (true, std::memory_order_release); }
    void requestStop() noexcept  { stopReq.store (true, std::memory_order_release); startReq.store (false); }
    bool isRecording() const noexcept { return recording.load (std::memory_order_acquire); }
    int  numSamples() const noexcept  { return num.load (std::memory_order_acquire); }
    double sampleRate() const noexcept { return rate.load (std::memory_order_acquire); }
    size_t getCapacity() const noexcept { return capacity.load (std::memory_order_acquire); }
    bool hitLimit() const noexcept { return limitHit.load(); }

    // Realtime. r may be nullptr (mono bus) -> dual mono.
    // The active-callback counter lets non-realtime readers pause capture safely
    // without ever making the audio thread wait on a mutex.
    void process (const float* inL, const float* inR, int n) noexcept
    {
        activeCallbacks.fetch_add (1, std::memory_order_seq_cst);
        if (! exclusive.load (std::memory_order_seq_cst))
        {
            if (stopReq.exchange (false, std::memory_order_acq_rel))
                recording.store (false, std::memory_order_release);
            if (startReq.exchange (false, std::memory_order_acq_rel) && capacity.load (std::memory_order_acquire) > 0)
            {
                num.store (0, std::memory_order_release);
                limitHit.store (false, std::memory_order_release);
                recording.store (true, std::memory_order_release);
            }
            if (recording.load (std::memory_order_acquire) && inL != nullptr)
            {
                const size_t at = (size_t) num.load (std::memory_order_relaxed);
                const size_t cap = capacity.load (std::memory_order_acquire);
                const size_t room = cap > at ? cap - at : 0;
                const size_t k = std::min ((size_t) n, room);
                for (size_t i = 0; i < k; ++i)
                {
                    l[at + i] = inL[i];
                    r[at + i] = inR ? inR[i] : inL[i];
                }
                num.store ((int) (at + k), std::memory_order_release);
                if (k < (size_t) n)
                {
                    recording.store (false, std::memory_order_release);
                    limitHit.store (true, std::memory_order_release);
                }
            }
        }
        activeCallbacks.fetch_sub (1, std::memory_order_seq_cst);
    }

    // Non-realtime
    void clear()
    {
        std::lock_guard<std::mutex> guard (controlMutex);
        beginExclusive(); requestStopAndWait();
        num.store (0, std::memory_order_release); pending = {};
        endExclusive();
    }

    // Copies [start,end) into dst. Fails while a take is recording.
    bool snapshot (StereoBuffer& dst, size_t start, size_t end)
    {
        std::lock_guard<std::mutex> guard (controlMutex);
        beginExclusive();
        requestStopAndWait();
        const size_t total = (size_t) num.load (std::memory_order_acquire);
        end = std::min (end, total);
        bool ok = false;
        if (start < end)
        {
            dst.sampleRate = rate;
            dst.l.assign (l.begin() + (long) start, l.begin() + (long) end);
            dst.r.assign (r.begin() + (long) start, r.begin() + (long) end);
            ok = true;
        }
        endExclusive();
        return ok;
    }
    bool snapshotAll (StereoBuffer& dst) { return snapshot (dst, 0, (size_t) numSamples()); }

    // Replace contents (state recall). If not yet prepared the take is held until prepare().
    void load (const StereoBuffer& src)
    {
        std::lock_guard<std::mutex> guard (controlMutex);
        if (capacity.load (std::memory_order_acquire) == 0) { pending = src; return; }
        beginExclusive(); requestStopAndWait();
        num.store (0, std::memory_order_release);
        copyIn (resampleBuffer (src, rate.load (std::memory_order_acquire)));
        endExclusive();
    }

    // Min/max per bin over [start,end). Safe on the UI thread: only reads already-published samples.
    void peaks (size_t start, size_t end, int bins, float* mn, float* mx)
    {
        std::lock_guard<std::mutex> guard (controlMutex);
        beginExclusive();
        const size_t total = (size_t) num.load (std::memory_order_acquire);
        end = std::min (end, total);
        for (int b = 0; b < bins; ++b) { mn[b] = 0.0f; mx[b] = 0.0f; }
        if (start >= end || bins <= 0) { endExclusive(); return; }
        const double span = (double) (end - start);
        for (int b = 0; b < bins; ++b)
        {
            const size_t a = start + (size_t) (span * b / bins);
            const size_t e = std::max (a + 1, start + (size_t) (span * (b + 1) / bins));
            float lo = 0.0f, hi = 0.0f;
            for (size_t i = a; i < std::min (e, end); ++i)
            {
                const float m = 0.5f * (l[i] + r[i]);
                lo = std::min (lo, m); hi = std::max (hi, m);
            }
            mn[b] = lo; mx[b] = hi;
        }
        endExclusive();
    }

private:
    void beginExclusive()
    {
        exclusive.store (true, std::memory_order_seq_cst);
        while (activeCallbacks.load (std::memory_order_seq_cst) != 0) std::this_thread::yield();
    }
    void endExclusive() noexcept { exclusive.store (false, std::memory_order_seq_cst); }
    void requestStopAndWait()
    {
        stopReq.store (true, std::memory_order_release);
        startReq.store (false, std::memory_order_release);
        while (activeCallbacks.load (std::memory_order_seq_cst) != 0) std::this_thread::yield();
        recording.store (false, std::memory_order_release);
        stopReq.store (false, std::memory_order_release);
    }
    void copyIn (const StereoBuffer& b)
    {
        const size_t k = std::min (b.size(), capacity.load (std::memory_order_acquire));
        std::copy (b.l.begin(), b.l.begin() + (long) k, l.begin());
        std::copy (b.r.begin(), b.r.begin() + (long) k, r.begin());
        num.store ((int) k, std::memory_order_release);
    }

    std::vector<float> l, r;
    StereoBuffer pending;
    std::atomic<size_t> capacity { 0 };
    std::atomic<double> rate { 44100.0 };
    std::atomic<uint32_t> activeCallbacks { 0 };
    std::mutex controlMutex;
    std::atomic<int> num { 0 };
    std::atomic<bool> recording { false }, startReq { false }, stopReq { false };
    std::atomic<bool> exclusive { false }, limitHit { false };
};
} // namespace cray
