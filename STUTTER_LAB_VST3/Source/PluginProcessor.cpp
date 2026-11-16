#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Background render worker: one job at a time, newest job wins, cooperative cancel.
class StutterLabAudioProcessor::RenderWorker final : public juce::Thread
{
public:
    explicit RenderWorker (StutterLabAudioProcessor& o) : juce::Thread ("STUTTER LAB render"), owner (o) { startThread(); }
    ~RenderWorker() override { owner.cancelFlag.store (true); stopThread (4000); }

    void post (Job&& j)
    {
        j.cancelEpoch = owner.cancelEpoch.fetch_add (1, std::memory_order_acq_rel) + 1;
        owner.cancelFlag.store (true, std::memory_order_release);
        { const juce::ScopedLock sl (lock); pending = std::make_unique<Job> (std::move (j)); }
        wake.signal();
    }
    void cancelAll()
    {
        owner.cancelEpoch.fetch_add (1, std::memory_order_acq_rel);
        owner.cancelFlag.store (true, std::memory_order_release);
        const juce::ScopedLock sl (lock); pending.reset();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wake.wait (200);
            std::unique_ptr<Job> job;
            { const juce::ScopedLock sl (lock); job = std::move (pending); }
            if (job == nullptr) continue;

            if (job->cancelEpoch != owner.cancelEpoch.load (std::memory_order_acquire)) continue;
            owner.cancelFlag.store (false, std::memory_order_release);
            if (job->cancelEpoch != owner.cancelEpoch.load (std::memory_order_acquire))
                owner.cancelFlag.store (true, std::memory_order_release);
            cray::RenderStats stats;
            auto audio = cray::renderStutter (job->selection, job->settings, &stats, &owner.cancelFlag, &owner.renderProgress);
            if (audio.empty() || threadShouldExit())
            {
                // cancelled (or invalid): only report if no newer job is waiting
                const juce::ScopedLock sl (lock);
                if (pending == nullptr && owner.renderState.load() == RenderState::Rendering)
                {
                    owner.renderProgress.store (0.0f, std::memory_order_release);
                    owner.renderState.store (RenderState::Idle);
                }
                continue;
            }
            owner.publishRender (std::move (audio), stats, job->id);
        }
    }

private:
    StutterLabAudioProcessor& owner;
    juce::CriticalSection lock;
    std::unique_ptr<Job> pending;
    juce::WaitableEvent wake;
};

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout StutterLabAudioProcessor::createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using API = juce::AudioParameterInt;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<API> ("repeats", "Repeats", 2, 32, 4));
    layout.add (std::make_unique<APC> ("division", "Rate", juce::StringArray { "1/2", "1/4", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T", "1/32", "1/32 T", "1/64" }, 2));
    layout.add (std::make_unique<APC> ("direction", "Direction", juce::StringArray { "Forward", "Reverse", "Ping-Pong", "Random" }, 0));
    layout.add (std::make_unique<APB> ("scan", "Scan Source", false));
    layout.add (std::make_unique<APF> ("swing", "Swing", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<APF> ("gate", "Gate", juce::NormalisableRange<float> (0.05f, 1.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<APF> ("humanize", "Humanize", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<APF> ("drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<APB> ("phone", "Phone", false));
    layout.add (std::make_unique<APF> ("pitch", "Pitch", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<APF> ("formant", "Formant", juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<APC> ("delayDivision", "Delay Rate", juce::StringArray { "1/2", "1/4", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T", "1/32", "1/32 T", "1/64" }, 2));
    layout.add (std::make_unique<APF> ("delayMix", "Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<APF> ("delayFeedback", "Delay Feedback", juce::NormalisableRange<float> (0.0f, 0.85f, 0.01f), 0.28f));
    layout.add (std::make_unique<APF> ("reverbMix", "Reverb Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<APF> ("reverbSize", "Reverb Size", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    layout.add (std::make_unique<APF> ("bpm", "Manual BPM", juce::NormalisableRange<float> (40.0f, 300.0f, 0.1f), 120.0f));
    layout.add (std::make_unique<APB> ("loop", "Loop Preview", false));
    return layout;
}

StutterLabAudioProcessor::StutterLabAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, &undoManager, "STUTTER_LAB_PARAMETERS", createParameterLayout())
{
    cleanOldRenders();
    worker = std::make_unique<RenderWorker> (*this);
}

StutterLabAudioProcessor::~StutterLabAudioProcessor()
{
    worker.reset();                                  // joins the thread before members die
}

void StutterLabAudioProcessor::prepareToPlay (double sampleRate, int)
{
    capture.prepare (sampleRate, 60.0);              // resamples an existing take if the host rate changed
    preview.prepare (sampleRate);
    auto* empty = new cray::PreviewBuffer();         // a render made at another rate must not play back
    preview.submit (empty);
    preview.stop();
    preview.collectGarbage();
    renderState.store (RenderState::Idle);
    const double duration = getCaptureSeconds();
    if (duration > 0.0)
    {
        const double a = juce::jlimit (0.0, duration, selectionStart.load());
        double b = juce::jlimit (a, duration, selectionEnd.load());
        if (b <= a) b = duration;
        selectionStart.store (a);
        selectionEnd.store (b);
    }
    else
    {
        selectionStart.store (0.0);
        selectionEnd.store (0.5);
    }
}

bool StutterLabAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == layouts.getMainOutputChannelSet();
}

void StutterLabAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) { hostBpm.store ((float) *bpm, std::memory_order_relaxed); hostBpmValid.store (true, std::memory_order_relaxed); }
            else hostBpmValid.store (false, std::memory_order_relaxed);
        }

    syncParametersToAtomics();
    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // input meter (pre-capture, pre-preview)
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (l[i]), r ? std::abs (r[i]) : 0.0f);
    if (pk > inputPeak.load (std::memory_order_relaxed)) inputPeak.store (pk, std::memory_order_relaxed);
    if (pk >= 0.999f) clipFlag.store (true, std::memory_order_relaxed);

    capture.process (l, r, n);                       // dry signal -> capture store (if recording)
    preview.setLoop (loopPreview.load (std::memory_order_relaxed));
    preview.process (l, r, n);                       // crossfades to the rendered buffer only while previewing
}

juce::AudioProcessorEditor* StutterLabAudioProcessor::createEditor() { return new StutterLabAudioProcessorEditor (*this); }

//==============================================================================
void StutterLabAudioProcessor::startCapture()
{
    preview.stop();
    capture.requestStart();
}
void StutterLabAudioProcessor::stopCapture() { capture.requestStop(); }
void StutterLabAudioProcessor::clearCapture()
{
    capture.clear();
    selectionStart.store (0.0); selectionEnd.store (0.5);
}

void StutterLabAudioProcessor::setSelection (double a, double b)
{
    const double dur = getCaptureSeconds();
    const double lo = juce::jlimit (0.0, dur, juce::jmin (a, b));
    const double hi = juce::jlimit (0.0, dur, juce::jmax (a, b));
    selectionStart.store (lo);
    selectionEnd.store (juce::jmax (lo + 0.005, hi));
}

void StutterLabAudioProcessor::syncParametersToAtomics()
{
    repeats.store ((int) parameters.getRawParameterValue ("repeats")->load());
    division.store ((int) parameters.getRawParameterValue ("division")->load());
    direction.store ((int) parameters.getRawParameterValue ("direction")->load());
    scanSelection.store (parameters.getRawParameterValue ("scan")->load() >= 0.5f);
    swing.store (parameters.getRawParameterValue ("swing")->load());
    gate.store (parameters.getRawParameterValue ("gate")->load());
    humanize.store (parameters.getRawParameterValue ("humanize")->load());
    saturation.store (parameters.getRawParameterValue ("drive")->load());
    phoneMode.store (parameters.getRawParameterValue ("phone")->load() >= 0.5f);
    pitchSemitones.store (parameters.getRawParameterValue ("pitch")->load());
    formantSemitones.store (parameters.getRawParameterValue ("formant")->load());
    delayDivision.store ((int) parameters.getRawParameterValue ("delayDivision")->load());
    delayMix.store (parameters.getRawParameterValue ("delayMix")->load());
    delayFeedback.store (parameters.getRawParameterValue ("delayFeedback")->load());
    reverbMix.store (parameters.getRawParameterValue ("reverbMix")->load());
    reverbSize.store (parameters.getRawParameterValue ("reverbSize")->load());
    manualBpm.store (parameters.getRawParameterValue ("bpm")->load());
    loopPreview.store (parameters.getRawParameterValue ("loop")->load() >= 0.5f);
}

void StutterLabAudioProcessor::syncAtomicsToParameters()
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = parameters.getParameter (id)) p->setValue (p->convertTo0to1 (value));
    };
    set ("repeats", (float) repeats.load()); set ("division", (float) division.load()); set ("direction", (float) direction.load());
    set ("scan", scanSelection.load() ? 1.0f : 0.0f); set ("swing", swing.load()); set ("gate", gate.load());
    set ("delayDivision", (float) delayDivision.load());
    set ("humanize", humanize.load()); set ("drive", saturation.load()); set ("phone", phoneMode.load() ? 1.0f : 0.0f);
    set ("pitch", pitchSemitones.load()); set ("formant", formantSemitones.load()); set ("delayMix", delayMix.load());
    set ("delayFeedback", delayFeedback.load()); set ("reverbMix", reverbMix.load()); set ("reverbSize", reverbSize.load());
    set ("bpm", manualBpm.load()); set ("loop", loopPreview.load() ? 1.0f : 0.0f);
}

void StutterLabAudioProcessor::loadFactoryPreset (int index)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = parameters.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    // Presets are intentionally gain-safe and leave the live input path untouched.
    switch (juce::jlimit (0, 4, index))
    {
        case 0: // Init
            set ("repeats", 4); set ("division", 2); set ("direction", 0); set ("swing", 0); set ("gate", 1);
            set ("humanize", 0); set ("drive", 0); set ("phone", 0); set ("pitch", 0); set ("formant", 0);
            set ("delayDivision", 2); set ("delayMix", 0); set ("delayFeedback", 0.28f); set ("reverbMix", 0); set ("reverbSize", 0.5f); break;
        case 1: // Tight 1/16
            set ("repeats", 8); set ("division", 5); set ("direction", 0); set ("swing", 0.08f); set ("gate", 0.72f);
            set ("humanize", 0.04f); set ("drive", 0.12f); set ("phone", 0); set ("pitch", 0); set ("formant", 0);
            set ("delayDivision", 5); set ("delayMix", 0.04f); set ("delayFeedback", 0.22f); set ("reverbMix", 0.05f); set ("reverbSize", 0.35f); break;
        case 2: // Triplet glitch
            set ("repeats", 12); set ("division", 4); set ("direction", 3); set ("swing", 0.18f); set ("gate", 0.48f);
            set ("humanize", 0.24f); set ("drive", 0.2f); set ("phone", 0); set ("pitch", 0); set ("formant", 0);
            set ("delayDivision", 4); set ("delayMix", 0.12f); set ("delayFeedback", 0.3f); set ("reverbMix", 0.08f); set ("reverbSize", 0.4f); break;
        case 3: // Reverse vocal
            set ("repeats", 6); set ("division", 2); set ("direction", 1); set ("swing", 0); set ("gate", 0.9f);
            set ("humanize", 0); set ("drive", 0.1f); set ("phone", 0); set ("pitch", -2); set ("formant", 0);
            set ("delayDivision", 2); set ("delayMix", 0.08f); set ("delayFeedback", 0.25f); set ("reverbMix", 0.1f); set ("reverbSize", 0.45f); break;
        case 4: // Wide atmosphere
            set ("repeats", 4); set ("division", 1); set ("direction", 0); set ("swing", 0); set ("gate", 1);
            set ("humanize", 0.02f); set ("drive", 0.05f); set ("phone", 0); set ("pitch", 0); set ("formant", 0);
            set ("delayDivision", 1); set ("delayMix", 0.28f); set ("delayFeedback", 0.48f); set ("reverbMix", 0.32f); set ("reverbSize", 0.82f); break;
    }
    syncParametersToAtomics();
}

void StutterLabAudioProcessor::storeAB (bool storeB)
{
    if (storeB) { stateB = parameters.copyState(); hasStateB = true; }
    else { stateA = parameters.copyState(); hasStateA = true; }
}

void StutterLabAudioProcessor::toggleAB()
{
    if (! hasStateA || ! hasStateB) return;
    parameters.replaceState (showingStateB ? stateA.createCopy() : stateB.createCopy());
    showingStateB = ! showingStateB;
    syncParametersToAtomics();
}

bool StutterLabAudioProcessor::undoParameterChange()
{
    if (! undoManager.canUndo()) return false;
    undoManager.undo();
    syncParametersToAtomics();
    return true;
}

bool StutterLabAudioProcessor::saveUserPreset (const juce::File& file, juce::String& error)
{
    auto xml = parameters.copyState().createXml();
    if (xml == nullptr || ! xml->writeTo (file, {})) { error = "Could not save preset file."; return false; }
    return true;
}

bool StutterLabAudioProcessor::loadUserPreset (const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr) { error = "Invalid preset XML."; return false; }
    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid() || tree.getType() != parameters.state.getType()) { error = "Preset is not a STUTTER LAB preset."; return false; }
    parameters.replaceState (tree);
    syncParametersToAtomics();
    return true;
}

double StutterLabAudioProcessor::getEffectiveBpm() const
{
    const double v = hostBpmValid.load() ? (double) hostBpm.load() : (double) manualBpm.load();
    return juce::jlimit (20.0, 400.0, v);
}

//==============================================================================
bool StutterLabAudioProcessor::generate (juce::String& error)
{
    syncParametersToAtomics();
    if (capture.isRecording()) { error = "Stop the capture before generating."; return false; }
    const double sr = capture.sampleRate();
    const size_t total = (size_t) capture.numSamples();
    if (total < 64) { error = "Capture a phrase first."; return false; }

    const size_t a = (size_t) juce::jlimit<double> (0.0, (double) total - 1.0, std::floor (selectionStart.load() * sr));
    const size_t b = (size_t) juce::jlimit<double> ((double) a + 1.0, (double) total, std::ceil (selectionEnd.load() * sr));

    Job job;
    if (! capture.snapshot (job.selection, a, b)) { error = "Could not read the capture."; return false; }
    auto& s = job.settings;
    s.bpm = getEffectiveBpm();
    s.saturation = saturation.load();
    s.phone = phoneMode.load();
    s.pitchSemitones = pitchSemitones.load(); s.formantSemitones = formantSemitones.load();
    s.delayDivision = delayDivision.load(); s.delayMix = delayMix.load(); s.delayFeedback = delayFeedback.load();
    s.reverbMix = reverbMix.load(); s.reverbSize = reverbSize.load();
    s.stutter.repeats = repeats.load();
    s.stutter.division = juce::jlimit (0, cray::kNumDivisions - 1, division.load());
    s.stutter.direction = (cray::Direction) juce::jlimit (0, 3, direction.load());
    s.stutter.scan = scanSelection.load();
    s.stutter.swing = swing.load(); s.stutter.gate = gate.load(); s.stutter.humanize = humanize.load();
    s.stutter.seed = (uint32_t) seed.load();
    job.id = nextJobId.fetch_add (1);

    const double estimate = (double) s.stutter.repeats * cray::divisionSamples (s.stutter.division, s.bpm, sr) / sr;
    if (estimate > 120.0) { error = "Render would exceed 120 s. Reduce repeats or use a shorter division."; return false; }

    preview.stop();
    renderProgress.store (0.0f, std::memory_order_release);
    renderState.store (RenderState::Rendering);
    worker->post (std::move (job));
    return true;
}

void StutterLabAudioProcessor::cancelRender()
{
    worker->cancelAll();
    if (renderState.load() == RenderState::Rendering)
    {
        renderProgress.store (0.0f, std::memory_order_release);
        renderState.store (RenderState::Idle);
    }
}

void StutterLabAudioProcessor::publishRender (cray::StereoBuffer&& audio, const cray::RenderStats& stats, uint64_t id)
{
    if (id + 1 != nextJobId.load (std::memory_order_acquire)) return;
    // 1) waveform overview
    constexpr int bins = 1024;
    std::vector<float> peaks ((size_t) bins * 2, 0.0f);
    for (int x = 0; x < bins; ++x)
    {
        const size_t a = audio.size() * (size_t) x / bins, e = juce::jmax (a + 1, audio.size() * (size_t) (x + 1) / bins);
        float lo = 0, hi = 0;
        for (size_t i = a; i < juce::jmin (e, audio.size()); ++i) { const float m = 0.5f * (audio.l[i] + audio.r[i]); lo = juce::jmin (lo, m); hi = juce::jmax (hi, m); }
        peaks[(size_t) x * 2] = lo; peaks[(size_t) x * 2 + 1] = hi;
    }
    // 2) unique WAV on disk (never overwrites a file a DAW may be importing)
    const auto file = makeUniqueRenderFile();
    const auto bytes = cray::encodeWav (audio, cray::WavFormat::Pcm24);
    bool wrote = false;
    {
        juce::FileOutputStream out (file);
        if (out.openedOk()) { wrote = out.write (bytes.data(), bytes.size()); out.flush(); }
    }
    if (! wrote)
    {
        const juce::ScopedLock sl (infoLock);
        info = {}; info.message = "Could not write the render WAV to " + file.getParentDirectory().getFullPathName();
        renderState.store (RenderState::Failed);
        return;
    }
    // 3) publish the exact rendered buffer for preview, then the metadata
    auto* pb = new cray::PreviewBuffer();
    pb->audio = audio;
    preview.submit (pb);
    {
        const juce::ScopedLock sl (infoLock);
        info.file = file; info.seconds = stats.seconds; info.peak = stats.peak; info.sampleRate = audio.sampleRate;
        info.slices = (int) stats.slices; info.bitDepth = 24; info.id = id; info.peaks = std::move (peaks);
        info.message = {};
    }
    renderProgress.store (1.0f, std::memory_order_release);
    renderId.store (id);
    renderState.store (RenderState::Ready);
}

StutterLabAudioProcessor::RenderInfo StutterLabAudioProcessor::getRenderInfo() const
{
    const juce::ScopedLock sl (infoLock);
    return info;
}

void StutterLabAudioProcessor::startPreview()
{
    if (! hasRender()) return;
    preview.play (loopPreview.load());
}

bool StutterLabAudioProcessor::exportRenderTo (const juce::File& destination, juce::String& error) const
{
    const auto src = getRenderInfo().file;
    if (! src.existsAsFile()) { error = "Generate a render first."; return false; }
    destination.getParentDirectory().createDirectory();
    if (! src.copyFileTo (destination)) { error = "Could not write " + destination.getFullPathName(); return false; }
    return true;
}

juce::File StutterLabAudioProcessor::makeUniqueRenderFile() const
{
    static std::atomic<int> counter { 0 };
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CRAY LDR").getChildFile ("STUTTER LAB");
    dir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    return dir.getNonexistentChildFile ("STUTTER_LAB_" + stamp + "_" + juce::String (++counter), ".wav", false);
}

void StutterLabAudioProcessor::cleanOldRenders() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CRAY LDR").getChildFile ("STUTTER LAB");
    juce::Array<juce::File> files;
    dir.findChildFiles (files, juce::File::findFiles, false, "STUTTER_LAB_*.wav");
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (2);
    for (auto& f : files) if (f.getLastModificationTime() < cutoff) f.deleteFile();   // only files older than 2 days
}

//==============================================================================
// State: v1-v4 are migrated; v5 adds compressed capture persistence.
void StutterLabAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    syncParametersToAtomics();
    juce::MemoryOutputStream out (dest, true);
    out.writeInt (6);
    out.writeInt (repeats.load()); out.writeInt (division.load()); out.writeInt (direction.load());
    out.writeBool (scanSelection.load());
    out.writeFloat (swing.load()); out.writeFloat (gate.load()); out.writeFloat (humanize.load());
    out.writeFloat (saturation.load()); out.writeBool (phoneMode.load());
    out.writeFloat (pitchSemitones.load()); out.writeFloat (formantSemitones.load());
    out.writeInt (delayDivision.load());
    out.writeFloat (delayMix.load()); out.writeFloat (delayFeedback.load());
    out.writeFloat (reverbMix.load()); out.writeFloat (reverbSize.load());
    out.writeInt (seed.load()); out.writeFloat (manualBpm.load()); out.writeBool (loopPreview.load());
    out.writeDouble (selectionStart.load()); out.writeDouble (selectionEnd.load());

    // Persist the captured take with the project/session state. Compress a compact
    // float payload so a typical vocal take remains manageable in a DAW state chunk.
    cray::StereoBuffer take;
    if (capture.snapshotAll (take) && ! take.empty())
    {
        juce::MemoryOutputStream raw;
        raw.writeDouble (take.sampleRate);
        raw.writeInt ((int) take.size());
        for (size_t i = 0; i < take.size(); ++i) { raw.writeFloat (take.l[i]); raw.writeFloat (take.r[i]); }
        juce::MemoryOutputStream compressed;
        {
            juce::GZIPCompressorOutputStream gzip (compressed, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
            gzip.write (raw.getData(), raw.getDataSize());
            gzip.flush();
        }
        out.writeInt ((int) compressed.getDataSize());
        out.write (compressed.getData(), compressed.getDataSize());
    }
    else out.writeInt (0);
}

void StutterLabAudioProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream in (data, (size_t) size, false);
    const int version = in.readInt();
    if (version < 1 || version > 6) return;
    if (version <= 2)
    {
        repeats.store (juce::jlimit (2, 32, in.readInt()));
        division.store (cray::divisionFromLegacy (in.readInt()));
        direction.store (in.readBool() ? (int) cray::Direction::Reverse : (int) cray::Direction::Forward);
        if (version == 2) { saturation.store (juce::jlimit (0.0f, 1.0f, in.readFloat())); phoneMode.store (in.readBool()); }
    }
    else
    {
        repeats.store (juce::jlimit (2, 32, in.readInt()));
        division.store (juce::jlimit (0, cray::kNumDivisions - 1, in.readInt()));
        direction.store (juce::jlimit (0, 3, in.readInt()));
        scanSelection.store (in.readBool());
        swing.store (juce::jlimit (0.0f, 1.0f, in.readFloat()));
        gate.store (juce::jlimit (0.05f, 1.0f, in.readFloat()));
        humanize.store (juce::jlimit (0.0f, 1.0f, in.readFloat()));
        saturation.store (juce::jlimit (0.0f, 1.0f, in.readFloat()));
        phoneMode.store (in.readBool());
        if (version >= 4)
        {
            pitchSemitones.store (juce::jlimit (-12.0f, 12.0f, in.readFloat()));
            formantSemitones.store (juce::jlimit (-6.0f, 6.0f, in.readFloat()));
            if (version >= 6) delayDivision.store (juce::jlimit (0, cray::kNumDivisions - 1, in.readInt()));
            delayMix.store (juce::jlimit (0.0f, 1.0f, in.readFloat()));
            delayFeedback.store (juce::jlimit (0.0f, 0.85f, in.readFloat()));
            reverbMix.store (juce::jlimit (0.0f, 1.0f, in.readFloat()));
            reverbSize.store (juce::jlimit (0.0f, 1.0f, in.readFloat()));
        }
        seed.store (in.readInt());
        manualBpm.store (juce::jlimit (20.0f, 400.0f, in.readFloat()));
        loopPreview.store (in.readBool());
    }
    const double a = juce::jmax (0.0, in.readDouble());
    selectionStart.store (a);
    selectionEnd.store (juce::jmax (a + 0.005, in.readDouble()));
    if (version >= 5 && in.getNumBytesRemaining() >= 4)
    {
        const int compressedSize = juce::jlimit (0, 128 * 1024 * 1024, in.readInt());
        if (compressedSize > 0 && compressedSize <= in.getNumBytesRemaining())
        {
            juce::MemoryBlock compressed;
            compressed.setSize ((size_t) compressedSize);
            in.read (compressed.getData(), (size_t) compressedSize);
            juce::MemoryInputStream compressedInput (compressed, false);
            juce::GZIPDecompressorInputStream gzip (&compressedInput, false, juce::GZIPDecompressorInputStream::gzipFormat);
            juce::MemoryBlock raw;
            if (gzip.readIntoMemoryBlock (raw, (ssize_t) (12 + (size_t) 60 * 192000 * 8)) && raw.getSize() >= 12)
            {
                juce::MemoryInputStream payload (raw, false);
                cray::StereoBuffer take;
                take.sampleRate = payload.readDouble();
                const int samples = juce::jlimit (0, 60 * 192000, payload.readInt());
                if (take.sampleRate >= 8000.0 && take.sampleRate <= 384000.0 && samples > 0
                    && (size_t) samples <= (raw.getSize() - 12) / 8)
                {
                    take.resize ((size_t) samples);
                    for (int i = 0; i < samples; ++i) { take.l[(size_t) i] = payload.readFloat(); take.r[(size_t) i] = payload.readFloat(); }
                    capture.load (take);
                    selectionStart.store (juce::jlimit (0.0, take.seconds(), selectionStart.load()));
                    selectionEnd.store (std::min (take.seconds(), juce::jmax (selectionStart.load() + 0.005, selectionEnd.load())));
                }
            }
        }
    }
    syncAtomicsToParameters();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StutterLabAudioProcessor(); }
