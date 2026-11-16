#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>
#include "Core/CaptureEngine.h"
#include "Core/PreviewEngine.h"
#include "Core/StutterEngine.h"
#include "Core/WavEncoder.h"

class StutterLabAudioProcessor final : public juce::AudioProcessor
{
public:
    StutterLabAudioProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    void loadFactoryPreset (int index);
    void storeAB (bool storeB);
    void toggleAB();
    bool undoParameterChange();
    bool saveUserPreset (const juce::File&, juce::String& error);
    bool loadUserPreset (const juce::File&, juce::String& error);
    ~StutterLabAudioProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- CAPTURE ----
    void startCapture();
    void stopCapture();
    void clearCapture();
    bool isCapturing() const { return capture.isRecording(); }
    int getCapturedSamples() const { return capture.numSamples(); }
    double getCaptureSampleRate() const { return capture.sampleRate(); }
    double getCaptureSeconds() const { return capture.sampleRate() > 0 ? capture.numSamples() / capture.sampleRate() : 0.0; }
    bool captureHitLimit() const { return capture.hitLimit(); }
    const cray::CaptureEngine& getCapture() const { return capture; }
    float getInputPeak() const { return inputPeak.exchange (0.0f); }   // peak since last read (UI meter)
    bool inputClipped() const { return clipFlag.load(); }
    void resetClip() { clipFlag.store (false); }

    // ---- SELECTION ----
    void setSelection (double startSeconds, double endSeconds);
    double getSelectionStart() const { return selectionStart.load(); }
    double getSelectionEnd() const { return selectionEnd.load(); }

    // ---- STUTTER settings (message thread writes, render thread gets a copy) ----
    std::atomic<int> repeats { 4 };
    std::atomic<int> division { 2 };                 // index into cray::kDivisionBeats
    std::atomic<int> direction { 0 };                // cray::Direction
    std::atomic<bool> scanSelection { true };
    std::atomic<float> swing { 0.0f }, gate { 1.0f }, humanize { 0.0f };
    std::atomic<float> saturation { 0.0f };
    std::atomic<float> pitchSemitones { 0.0f }, formantSemitones { 0.0f };
    std::atomic<float> delayMix { 0.0f }, delayFeedback { 0.28f };
    std::atomic<int> delayDivision { 2 };
    std::atomic<float> reverbMix { 0.0f }, reverbSize { 0.5f };
    std::atomic<bool> phoneMode { false };
    std::atomic<int> seed { 0xC0FFEE };
    std::atomic<float> manualBpm { 120.0f };         // used only when the host gives no tempo
    std::atomic<bool> loopPreview { false };

    double getEffectiveBpm() const;
    bool hostTempoAvailable() const { return hostBpmValid.load(); }

    // ---- GENERATE / PREVIEW / DRAG / EXPORT (separate actions) ----
    enum class RenderState { Idle, Rendering, Ready, Failed };
    struct RenderInfo
    {
        juce::File file; juce::String message; double seconds = 0, peak = 0, sampleRate = 0; int slices = 0, bitDepth = 24;
        uint64_t id = 0; std::vector<float> peaks;   // 2 floats (min,max) per bin
    };
    bool generate (juce::String& error);             // snapshot selection and queue a background render
    void cancelRender();
    RenderState getRenderState() const { return renderState.load(); }
    float getRenderProgress() const noexcept { return renderProgress.load (std::memory_order_acquire); }
    uint64_t getRenderId() const { return renderId.load(); }
    RenderInfo getRenderInfo() const;                // copy; safe from the message thread
    void startPreview();
    void stopPreview() { preview.stop(); }
    bool isPreviewPlaying() const { return preview.isPlaying(); }
    float getPreviewProgress() const { return preview.progress(); }
    bool hasRender() const { return renderState.load() == RenderState::Ready; }
    bool exportRenderTo (const juce::File& destination, juce::String& error) const;

private:
    void syncParametersToAtomics();
    void syncAtomicsToParameters();
    class RenderWorker;
    struct Job { cray::StereoBuffer selection; cray::RenderSettings settings; uint64_t id = 0, cancelEpoch = 0; };
    void publishRender (cray::StereoBuffer&& audio, const cray::RenderStats&, uint64_t id);
    juce::File makeUniqueRenderFile() const;
    void cleanOldRenders() const;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState parameters;
    juce::ValueTree stateA, stateB;
    bool hasStateA = false, hasStateB = false, showingStateB = false;
    cray::CaptureEngine capture;
    cray::PreviewEngine preview;
    std::atomic<double> selectionStart { 0.0 }, selectionEnd { 0.5 };
    std::atomic<float> hostBpm { 120.0f };
    std::atomic<bool> hostBpmValid { false };
    mutable std::atomic<float> inputPeak { 0.0f };
    std::atomic<bool> clipFlag { false };
    std::atomic<RenderState> renderState { RenderState::Idle };
    std::atomic<uint64_t> renderId { 0 }, nextJobId { 1 }, cancelEpoch { 0 };
    std::atomic<bool> cancelFlag { false };
    std::atomic<float> renderProgress { 0.0f };

    mutable juce::CriticalSection infoLock;          // message/worker threads only, never the audio thread
    RenderInfo info;
    std::unique_ptr<RenderWorker> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StutterLabAudioProcessor)
};
