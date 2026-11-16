// PREVIEW player. A finished render is published to the audio thread through a single
// atomic pointer hand-off (no locks). Retired buffers go back to the worker through a
// small atomic ring so the audio thread never frees memory.
#pragma once
#include "Common.h"

namespace cray
{
struct PreviewBuffer
{
    StereoBuffer audio;
    bool keepPosition = false;   // live re-render while auditioning: keep playhead
};

class PreviewEngine
{
public:
    ~PreviewEngine() { delete incoming.exchange (nullptr); delete current; collectGarbage(); }

    void prepare (double sr)
    {
        sampleRate = sr;
        slewPerSample = (float) (1.0 / std::max (1.0, 0.004 * sr));   // 4 ms de-click ramp
    }

    // Non-realtime. Takes ownership.
    void submit (PreviewBuffer* b) { delete incoming.exchange (b, std::memory_order_acq_rel); }
    void collectGarbage()
    {
        for (auto& s : retired) delete s.exchange (nullptr, std::memory_order_acq_rel);
    }
    void play (bool loopIt) { loop.store (loopIt); restart.store (true); wantPlay.store (true, std::memory_order_release); }
    void stop() { wantPlay.store (false, std::memory_order_release); }
    void setLoop (bool v) { loop.store (v); }
    bool isPlaying() const noexcept { return wantPlay.load (std::memory_order_acquire) || audible.load (std::memory_order_acquire); }
    bool hasAudio() const noexcept { return length.load() > 0; }
    int  lengthSamples() const noexcept { return length.load(); }
    float progress() const noexcept
    {
        const int n = length.load();
        return n > 0 ? (float) pos.load() / (float) n : 0.0f;
    }

    // Realtime. Replaces the buffer content with the preview while it is audible,
    // crossfading with the live signal. r may be nullptr (mono bus).
    void process (float* l, float* r, int n) noexcept
    {
        if (PreviewBuffer* nb = incoming.load (std::memory_order_acquire))
        {
            int slot = -1;
            for (int i = 0; i < kRing; ++i) if (retired[i].load (std::memory_order_relaxed) == nullptr) { slot = i; break; }
            if (slot >= 0 && incoming.compare_exchange_strong (nb, nullptr, std::memory_order_acq_rel))
            {
                PreviewBuffer* old = current;
                current = nb;
                length.store ((int) nb->audio.size(), std::memory_order_release);
                if (! nb->keepPosition) cursor = 0;
                if (cursor > (long) nb->audio.size()) cursor = 0;
                if (old) retired[slot].store (old, std::memory_order_release);
            }
        }
        if (restart.exchange (false)) cursor = 0;

        const bool hasCurrentAudio = current != nullptr && ! current->audio.empty();
        if (! hasCurrentAudio) wantPlay.store (false, std::memory_order_release);
        const bool want = wantPlay.load (std::memory_order_acquire) && hasCurrentAudio;
        if (! want && gain <= 0.0f) { audible.store (false, std::memory_order_release); pos.store ((int) cursor, std::memory_order_relaxed); return; }

        const auto& a = current->audio;
        const long total = (long) a.size();
        const bool lp = loop.load (std::memory_order_relaxed);
        bool playing = want;
        for (int i = 0; i < n; ++i)
        {
            if (playing && cursor >= total)
            {
                if (lp) cursor = 0;
                else { playing = false; wantPlay.store (false, std::memory_order_release); }
            }
            gain += playing ? slewPerSample : -slewPerSample;
            gain = std::min (1.0f, std::max (0.0f, gain));
            const float pl = (playing && cursor < total) ? a.l[(size_t) cursor] : 0.0f;
            const float pr = (playing && cursor < total) ? a.r[(size_t) cursor] : 0.0f;
            if (r != nullptr)
            {
                l[i] = l[i] * (1.0f - gain) + pl * gain;
                r[i] = r[i] * (1.0f - gain) + pr * gain;
            }
            else
                l[i] = l[i] * (1.0f - gain) + 0.5f * (pl + pr) * gain;
            if (playing) ++cursor;
        }
        audible.store (gain > 0.0f, std::memory_order_release);
        pos.store ((int) std::min<long> (cursor, total), std::memory_order_relaxed);
    }

private:
    static constexpr int kRing = 4;
    std::atomic<PreviewBuffer*> incoming { nullptr };
    std::atomic<PreviewBuffer*> retired[kRing] = {};
    PreviewBuffer* current = nullptr;            // audio thread only
    long cursor = 0;                             // audio thread only
    float gain = 0.0f;                           // audio thread only
    float slewPerSample = 0.01f;
    double sampleRate = 44100.0;
    std::atomic<bool> wantPlay { false }, loop { false }, restart { false }, audible { false };
    std::atomic<int> length { 0 }, pos { 0 };
};
} // namespace cray
