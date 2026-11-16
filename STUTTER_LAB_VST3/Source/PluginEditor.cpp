#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff10120f), kPanel (0xff24251f), kWell (0xff0b0d0b), kGold (0xffc7a35a), kIvory (0xffe9e1cf), kDim (0xff9b998c), kRed (0xffa94b3c), kOlive (0xff4a503c), kBrass (0xff8d7445);
}

StutterLabAudioProcessorEditor::StutterLabAudioProcessorEditor (StutterLabAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (900, 600);
    setResizable (true, true);
    setResizeLimits (860, 570, 1600, 1000);

    for (int i = 0; i < cray::kNumDivisions; ++i) divisionBox.addItem (cray::kDivisionNames[i], i + 1);
    divisionBox.setSelectedId (processor.division.load() + 1, juce::dontSendNotification);
    for (auto* name : { "FORWARD", "REVERSE", "PING-PONG", "RANDOM (SEED)" }) directionBox.addItem (name, directionBox.getNumItems() + 1);
    directionBox.setSelectedId (processor.direction.load() + 1, juce::dontSendNotification);
    for (int i = 0; i < cray::kNumDivisions; ++i) delayRateBox.addItem (cray::kDivisionNames[i], i + 1);
    delayRateBox.setSelectedId (processor.delayDivision.load() + 1, juce::dontSendNotification);
    presetBox.addItemList ({ "FACTORY: INIT", "TIGHT 1/16", "TRIPLET GLITCH", "REVERSE VOCAL", "WIDE ATMOSPHERE" }, 1);
    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.onChange = [this] { processor.loadFactoryPreset (presetBox.getSelectedId() - 1); };

    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& text, double lo, double hi, double step, double val, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        s.setRange (lo, hi, step); s.setValue (val, juce::dontSendNotification); s.setTextValueSuffix (suffix);
        addLabelled (s, l, text);
    };
    setup (repeatsSlider, repeatsLabel, "REPEATS", 2, 32, 1, processor.repeats.load(), "");
    setup (swingSlider, swingLabel, "SWING", 0, 100, 1, processor.swing.load() * 100.0, " %");
    setup (gateSlider, gateLabel, "GATE", 5, 100, 1, processor.gate.load() * 100.0, " %");
    setup (humanizeSlider, humanizeLabel, "HUMANIZE", 0, 100, 1, processor.humanize.load() * 100.0, " %");
    setup (driveSlider, driveLabel, "DRIVE", 0, 100, 1, processor.saturation.load() * 100.0, " %");
    setup (bpmSlider, bpmLabel, "MANUAL BPM", 40, 300, 0.1, processor.manualBpm.load(), "");
    setup (pitchSlider, pitchLabel, "PITCH", -12, 12, 0.1, processor.pitchSemitones.load(), " st");
    setup (formantSlider, formantLabel, "FORMANT", -6, 6, 0.1, processor.formantSemitones.load(), " st");
    setup (delayMixSlider, delayMixLabel, "DELAY MIX", 0, 100, 1, processor.delayMix.load() * 100.0, " %");
    setup (delayFeedbackSlider, delayFeedbackLabel, "DELAY FB", 0, 85, 1, processor.delayFeedback.load() * 100.0, " %");
    setup (reverbMixSlider, reverbMixLabel, "REVERB MIX", 0, 100, 1, processor.reverbMix.load() * 100.0, " %");
    setup (reverbSizeSlider, reverbSizeLabel, "REVERB SIZE", 0, 100, 1, processor.reverbSize.load() * 100.0, " %");

    for (auto* l : { &divisionLabel, &directionLabel, &pitchLabel, &formantLabel, &delayMixLabel, &delayFeedbackLabel, &reverbMixLabel, &reverbSizeLabel, &delayRateLabel })
    { l->setColour (juce::Label::textColourId, kDim); l->setFont (juce::Font (juce::FontOptions (12.0f))); addAndMakeVisible (*l); }
    divisionLabel.setText ("RATE", juce::dontSendNotification);
    delayRateLabel.setText ("DELAY RATE", juce::dontSendNotification);
    directionLabel.setText ("DIRECTION", juce::dontSendNotification);

    phoneButton.setToggleState (processor.phoneMode.load(), juce::dontSendNotification);
    scanButton.setToggleState (processor.scanSelection.load(), juce::dontSendNotification);
    loopButton.setToggleState (processor.loopPreview.load(), juce::dontSendNotification);

    captureButton.onClick = [this] { processor.startCapture(); };
    stopButton.onClick = [this] { if (processor.getRenderState() == StutterLabAudioProcessor::RenderState::Rendering) processor.cancelRender(); else { processor.stopCapture(); processor.stopPreview(); } };
    clearButton.onClick = [this] { processor.clearCapture(); processor.resetClip(); };
    generateButton.onClick = [this] { doGenerate(); };
    previewButton.onClick = [this] { processor.isPreviewPlaying() ? processor.stopPreview() : processor.startPreview(); };
    exportButton.onClick = [this] { doExport(); };
    newSeedButton.onClick = [this] { processor.seed.store ((int) juce::Random::getSystemRandom().nextInt()); };
    storeAButton.onClick = [this] { processor.storeAB (false); infoLabel.setText ("Stored A snapshot.", juce::dontSendNotification); };
    storeBButton.onClick = [this] { processor.storeAB (true); infoLabel.setText ("Stored B snapshot.", juce::dontSendNotification); };
    abButton.onClick = [this] { processor.toggleAB(); };
    undoButton.onClick = [this] { if (! processor.undoParameterChange()) infoLabel.setText ("Nothing to undo.", juce::dontSendNotification); };
    savePresetButton.onClick = [this]
    {
        const auto def = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("STUTTER_LAB_Preset.slpreset");
        fileChooser = std::make_unique<juce::FileChooser> ("Save STUTTER LAB preset", def, "*.slpreset");
        juce::Component::SafePointer<StutterLabAudioProcessorEditor> safe (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe] (const juce::FileChooser& fc) { if (safe == nullptr || fc.getResult() == juce::File()) return; juce::String err;
                if (! safe->processor.saveUserPreset (fc.getResult().withFileExtension ("slpreset"), err)) safe->infoLabel.setText (err, juce::dontSendNotification);
                else safe->infoLabel.setText ("Preset saved.", juce::dontSendNotification); });
    };
    loadPresetButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load STUTTER LAB preset", juce::File(), "*.slpreset");
        juce::Component::SafePointer<StutterLabAudioProcessorEditor> safe (this);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe] (const juce::FileChooser& fc) { if (safe == nullptr || fc.getResult() == juce::File()) return; juce::String err;
                if (! safe->processor.loadUserPreset (fc.getResult(), err)) safe->infoLabel.setText (err, juce::dontSendNotification);
                else safe->infoLabel.setText ("Preset loaded.", juce::dontSendNotification); });
    };

    for (auto* c : std::initializer_list<juce::Component*> { &captureButton, &stopButton, &clearButton, &generateButton, &previewButton, &exportButton,
                                                             &phoneButton, &scanButton, &loopButton, &divisionBox, &directionBox, &newSeedButton, &statusLabel, &infoLabel, &delayRateBox, &presetBox, &storeAButton, &storeBButton, &abButton, &undoButton, &savePresetButton, &loadPresetButton, &pitchSlider, &formantSlider, &delayMixSlider, &delayFeedbackSlider, &reverbMixSlider, &reverbSizeSlider })
        addAndMakeVisible (*c);
    captureButton.setColour (juce::TextButton::buttonColourId, kRed.darker (0.3f));
    generateButton.setColour (juce::TextButton::buttonColourId, kGold.darker (0.5f));
    statusLabel.setColour (juce::Label::textColourId, kIvory);
    infoLabel.setColour (juce::Label::textColourId, kDim);
    infoLabel.setFont (juce::Font (juce::FontOptions (12.0f)));

    auto& state = processor.getParameters();
    repeatsAttachment = std::make_unique<SliderAttachment> (state, "repeats", repeatsSlider);
    swingAttachment = std::make_unique<SliderAttachment> (state, "swing", swingSlider);
    gateAttachment = std::make_unique<SliderAttachment> (state, "gate", gateSlider);
    humanizeAttachment = std::make_unique<SliderAttachment> (state, "humanize", humanizeSlider);
    driveAttachment = std::make_unique<SliderAttachment> (state, "drive", driveSlider);
    bpmAttachment = std::make_unique<SliderAttachment> (state, "bpm", bpmSlider);
    pitchAttachment = std::make_unique<SliderAttachment> (state, "pitch", pitchSlider);
    formantAttachment = std::make_unique<SliderAttachment> (state, "formant", formantSlider);
    delayRateAttachment = std::make_unique<ComboAttachment> (state, "delayDivision", delayRateBox);
    delayMixAttachment = std::make_unique<SliderAttachment> (state, "delayMix", delayMixSlider);
    delayFeedbackAttachment = std::make_unique<SliderAttachment> (state, "delayFeedback", delayFeedbackSlider);
    reverbMixAttachment = std::make_unique<SliderAttachment> (state, "reverbMix", reverbMixSlider);
    reverbSizeAttachment = std::make_unique<SliderAttachment> (state, "reverbSize", reverbSizeSlider);
    divisionAttachment = std::make_unique<ComboAttachment> (state, "division", divisionBox);
    directionAttachment = std::make_unique<ComboAttachment> (state, "direction", directionBox);
    phoneAttachment = std::make_unique<ButtonAttachment> (state, "phone", phoneButton);
    scanAttachment = std::make_unique<ButtonAttachment> (state, "scan", scanButton);
    loopAttachment = std::make_unique<ButtonAttachment> (state, "loop", loopButton);

    refreshStatus();
    startTimerHz (20);
}

StutterLabAudioProcessorEditor::~StutterLabAudioProcessorEditor() { stopTimer(); }

void StutterLabAudioProcessorEditor::addLabelled (juce::Slider& s, juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, kDim);
    l.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (s); addAndMakeVisible (l);
}

void StutterLabAudioProcessorEditor::resized()
{
    auto a = getLocalBounds().reduced (16);
    a.removeFromTop (46);                                              // header (painted)
    auto top = a.removeFromTop (34);
    captureButton.setBounds (top.removeFromLeft (110).reduced (2));
    stopButton.setBounds (top.removeFromLeft (80).reduced (2));
    clearButton.setBounds (top.removeFromLeft (68).reduced (2));
    presetBox.setBounds (top.removeFromLeft (148).reduced (2));
    storeAButton.setBounds (top.removeFromLeft (58).reduced (2));
    storeBButton.setBounds (top.removeFromLeft (58).reduced (2));
    abButton.setBounds (top.removeFromLeft (42).reduced (2));
    undoButton.setBounds (top.removeFromLeft (52).reduced (2));
    savePresetButton.setBounds (top.removeFromLeft (48).reduced (2));
    loadPresetButton.setBounds (top.removeFromLeft (48).reduced (2));
    statusLabel.setBounds (top.reduced (4, 0));

    captureArea = a.removeFromTop (juce::jmax (110, (int) (a.getHeight() * 0.30f)));
    a.removeFromTop (6);
    auto grid = a.removeFromTop (118);
    const int colW = grid.getWidth() / 3;
    auto row = [] (juce::Rectangle<int>& col, juce::Label& lab, juce::Component& c)
    { auto r = col.removeFromTop (26); lab.setBounds (r.removeFromLeft (84)); c.setBounds (r); };
    auto c1 = grid.removeFromLeft (colW).reduced (4, 0), c2 = grid.removeFromLeft (colW).reduced (4, 0), c3 = grid.reduced (4, 0);
    row (c1, repeatsLabel, repeatsSlider); row (c1, divisionLabel, divisionBox); row (c1, directionLabel, directionBox);
    auto sw = c1.removeFromTop (26); scanButton.setBounds (sw.removeFromLeft (150)); newSeedButton.setBounds (sw.reduced (2));
    row (c2, swingLabel, swingSlider); row (c2, gateLabel, gateSlider); row (c2, humanizeLabel, humanizeSlider); row (c2, bpmLabel, bpmSlider);
    row (c3, driveLabel, driveSlider); phoneButton.setBounds (c3.removeFromTop (26)); loopButton.setBounds (c3.removeFromTop (26));

    effectsArea = a.removeFromBottom (104);
    a.removeFromBottom (5);
    auto bottom = a.removeFromBottom (40);
    generateButton.setBounds (bottom.removeFromLeft (130).reduced (2));
    previewButton.setBounds (bottom.removeFromLeft (120).reduced (2));
    exportButton.setBounds (bottom.removeFromRight (130).reduced (2));
    dragArea = bottom.removeFromRight (170).reduced (2);
    infoLabel.setBounds (bottom.reduced (8, 0));
    a.removeFromBottom (6);
    renderArea = a;

    auto fx = effectsArea.reduced (4, 2);
    const int fxCol = fx.getWidth() / 3;
    auto fxRow = [] (juce::Rectangle<int>& area, juce::Label& label, juce::Slider& slider)
    {
        auto line = area.removeFromTop (34);
        label.setBounds (line.removeFromLeft (78));
        slider.setBounds (line.reduced (2, 3));
    };
    auto fx1 = fx.removeFromLeft (fxCol), fx2 = fx.removeFromLeft (fxCol), fx3 = fx;
    fxRow (fx1, pitchLabel, pitchSlider); fxRow (fx1, formantLabel, formantSlider);
    fxRow (fx2, delayMixLabel, delayMixSlider); fxRow (fx2, delayFeedbackLabel, delayFeedbackSlider);
    auto delayRateLine = fx2.removeFromTop (26); delayRateLabel.setBounds (delayRateLine.removeFromLeft (78)); delayRateBox.setBounds (delayRateLine.reduced (2, 1));
    fxRow (fx3, reverbMixLabel, reverbMixSlider); fxRow (fx3, reverbSizeLabel, reverbSizeSlider);
}

void StutterLabAudioProcessorEditor::drawPeaks (juce::Graphics& g, juce::Rectangle<int> area, const std::vector<float>& mm, juce::Colour c)
{
    g.setColour (kWell); g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (kBrass.withAlpha (0.55f)); g.drawRoundedRectangle (area.toFloat(), 4.0f, 1.0f);
    g.setColour (juce::Colour (0xff2a2c31)); g.drawHorizontalLine (area.getCentreY(), (float) area.getX(), (float) area.getRight());
    const int bins = (int) (mm.size() / 2);
    if (bins <= 0) return;
    g.setColour (c);
    const float mid = (float) area.getCentreY(), half = area.getHeight() * 0.46f;
    for (int x = 0; x < area.getWidth(); ++x)
    {
        const int b = juce::jlimit (0, bins - 1, x * bins / juce::jmax (1, area.getWidth()));
        g.drawVerticalLine (area.getX() + x, mid - mm[(size_t) b * 2 + 1] * half, mid - mm[(size_t) b * 2] * half + 1.0f);
    }
}

void StutterLabAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (8), 8.0f);
    g.setColour (kBrass); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (8), 8.0f, 1.4f);
    g.setColour (kOlive.withAlpha (0.55f)); g.fillRoundedRectangle (12.0f, 10.0f, (float) getWidth() - 24.0f, 42.0f, 5.0f);
    g.setColour (kGold); g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    g.drawText ("STUTTER LAB", 24, 14, 300, 30, juce::Justification::centredLeft);
    g.setColour (kDim); g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("CRAY LDR AUDIO SOFTWARE  |  CAPTURE > SELECT > GENERATE > PREVIEW > DRAG", 24, 38, 520, 14, juce::Justification::centredLeft);
    g.setColour (kIvory); g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (juce::String (processor.getEffectiveBpm(), 1) + " BPM" + (processor.hostTempoAvailable() ? "  (HOST)" : "  (MANUAL)"),
                getWidth() - 300, 18, 180, 22, juce::Justification::centredRight);
    // Small analogue-style input meter and peak LED.
    auto meter = juce::Rectangle<float> ((float) getWidth() - 108.0f, 25.0f, 72.0f, 9.0f);
    g.setColour (kWell); g.fillRoundedRectangle (meter, 2.0f);
    g.setColour (meterPeak > 0.98f ? kRed : kGold);
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, meterPeak)), 2.0f);
    g.setColour (kBrass); g.drawRoundedRectangle (meter, 2.0f, 1.0f);
    g.setColour (processor.inputClipped() ? kRed : kOlive.brighter (0.5f));
    g.fillEllipse ((float) getWidth() - 28.0f, 22.0f, 8.0f, 8.0f);

    // source waveform
    g.setColour (kGold); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("TAPE INPUT / CAPTURE", captureArea.getX() + 7, captureArea.getY() + 2, 160, 12, juce::Justification::centredLeft);
    drawPeaks (g, captureArea, capturePeaks, kGold);
    const int total = processor.getCapturedSamples();
    if (total > 0)
    {
        const double dur = processor.getCaptureSeconds();
        const int sx = captureArea.getX() + (int) (processor.getSelectionStart() / dur * captureArea.getWidth());
        const int ex = captureArea.getX() + (int) (processor.getSelectionEnd() / dur * captureArea.getWidth());
        g.setColour (juce::Colour (0x33ffffff)); g.fillRect (sx, captureArea.getY(), juce::jmax (2, ex - sx), captureArea.getHeight());
        g.setColour (kIvory); g.drawVerticalLine (sx, (float) captureArea.getY(), (float) captureArea.getBottom());
        g.drawVerticalLine (ex, (float) captureArea.getY(), (float) captureArea.getBottom());
    }
    else
    {
        g.setColour (kDim); g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText ("Press CAPTURE, play or sing the phrase, then STOP. Drag on the waveform to select.", captureArea, juce::Justification::centred);
    }
    g.setColour (kDim); g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("SOURCE", captureArea.getX() + 6, captureArea.getY() + 2, 80, 12, juce::Justification::centredLeft);

    // render waveform
    g.setColour (kGold); g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("GENERATED PERFORMANCE", renderArea.getX() + 7, renderArea.getY() + 2, 200, 12, juce::Justification::centredLeft);
    drawPeaks (g, renderArea, renderPeaks, kIvory);
    if (processor.getRenderState() == StutterLabAudioProcessor::RenderState::Rendering)
    {
        auto renderBarArea = renderArea;
        auto bar = renderBarArea.removeFromBottom (5).reduced (1, 1);
        g.setColour (kWell); g.fillRect (bar);
        g.setColour (kGold); g.fillRect (bar.withWidth ((int) std::round (bar.getWidth() * processor.getRenderProgress())));
    }
    g.setColour (kDim);
    g.drawText ("RENDER", renderArea.getX() + 6, renderArea.getY() + 2, 80, 12, juce::Justification::centredLeft);
    if (renderPeaks.empty())
    {
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText ("Select a region, then press GENERATE.", renderArea, juce::Justification::centred);
    }
    else if (processor.isPreviewPlaying())
    {
        const int px = renderArea.getX() + (int) (processor.getPreviewProgress() * renderArea.getWidth());
        g.setColour (kGold); g.drawVerticalLine (px, (float) renderArea.getY(), (float) renderArea.getBottom());
    }

    // drag handle
    const bool ready = processor.hasRender();
    g.setColour (ready ? kGold.darker (0.4f) : juce::Colour (0xff2a2c31)); g.fillRoundedRectangle (dragArea.toFloat(), 4.0f);
    g.setColour (ready ? kIvory : kDim); g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("DRAG TO DAW", dragArea, juce::Justification::centred);
}

void StutterLabAudioProcessorEditor::timerCallback()
{
    meterPeak = processor.getInputPeak();
    const bool rec = processor.isCapturing();
    if (wasRecording && ! rec)                                          // take just ended: select all of it
        processor.setSelection (0.0, processor.getCaptureSeconds());
    wasRecording = rec;

    const int samples = processor.getCapturedSamples();
    if (samples != lastCaptureSamples || captureArea.getWidth() != lastW)
    {
        const int bins = juce::jmax (1, captureArea.getWidth());
        std::vector<float> mn ((size_t) bins), mx ((size_t) bins);
        processor.getCapture().peaks (0, (size_t) samples, bins, mn.data(), mx.data());
        capturePeaks.assign ((size_t) bins * 2, 0.0f);
        for (int i = 0; i < bins; ++i) { capturePeaks[(size_t) i * 2] = mn[(size_t) i]; capturePeaks[(size_t) i * 2 + 1] = mx[(size_t) i]; }
        lastCaptureSamples = samples; lastW = captureArea.getWidth();
    }
    if (processor.getRenderId() != shownRenderId && processor.hasRender())
    {
        renderPeaks = processor.getRenderInfo().peaks;
        shownRenderId = processor.getRenderId();
    }
    if (processor.getRenderState() == StutterLabAudioProcessor::RenderState::Idle && ! processor.hasRender() && shownRenderId == 0) renderPeaks.clear();
    stopButton.setButtonText (processor.getRenderState() == StutterLabAudioProcessor::RenderState::Rendering ? "CANCEL" : "STOP");
    previewButton.setButtonText (processor.isPreviewPlaying() ? "STOP PREVIEW" : "PREVIEW");
    previewButton.setEnabled (processor.hasRender());
    exportButton.setEnabled (processor.hasRender());
    generateButton.setEnabled (! rec);
    refreshStatus();
    repaint();
}

void StutterLabAudioProcessorEditor::refreshStatus()
{
    using RS = StutterLabAudioProcessor::RenderState;
    juce::String s;
    if (processor.isCapturing()) s = "RECORDING  " + juce::String (processor.getCaptureSeconds(), 1) + " s";
    else if (processor.getRenderState() == RS::Rendering) s = "RENDERING  " + juce::String (processor.getRenderProgress() * 100.0f, 0) + "%";
    else if (processor.isPreviewPlaying()) s = "PREVIEW";
    else s = "IDLE  |  take " + juce::String (processor.getCaptureSeconds(), 2) + " s  |  selection "
             + juce::String (processor.getSelectionEnd() - processor.getSelectionStart(), 3) + " s";
    if (processor.captureHitLimit()) s += "  |  60 s LIMIT REACHED";
    if (processor.inputClipped()) s += "  |  INPUT CLIP";
    statusLabel.setText (s, juce::dontSendNotification);

    const auto ri = processor.getRenderInfo();
    if (processor.getRenderState() == RS::Failed) infoLabel.setText (ri.message, juce::dontSendNotification);
    else if (processor.hasRender())
        infoLabel.setText (juce::String (ri.seconds, 2) + " s  |  " + juce::String (ri.sampleRate / 1000.0, 1) + " kHz  |  "
                           + juce::String (ri.bitDepth) + "-bit  |  " + juce::String (ri.slices) + " slices  |  peak "
                           + juce::String (juce::Decibels::gainToDecibels ((float) ri.peak), 1) + " dBFS", juce::dontSendNotification);
    else infoLabel.setText ({}, juce::dontSendNotification);
}

void StutterLabAudioProcessorEditor::doGenerate()
{
    juce::String err;
    if (! processor.generate (err)) infoLabel.setText (err, juce::dontSendNotification);
}

void StutterLabAudioProcessorEditor::doExport()
{
    if (! processor.hasRender()) return;
    const auto def = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("STUTTER_LAB_Render.wav");
    fileChooser = std::make_unique<juce::FileChooser> ("Export WAV", def, "*.wav");
    juce::Component::SafePointer<StutterLabAudioProcessorEditor> safe (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;                                // editor closed while the dialog was open
            auto dest = fc.getResult();
            if (dest == juce::File()) return;
            juce::String err;
            if (! safe->processor.exportRenderTo (dest.withFileExtension ("wav"), err)) safe->infoLabel.setText (err, juce::dontSendNotification);
            else safe->infoLabel.setText ("Exported " + dest.getFileName(), juce::dontSendNotification);
        });
}

void StutterLabAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    pressPoint = e.getPosition(); dragStarted = false;
    if (captureArea.contains (e.getPosition()) && processor.getCapturedSamples() > 0 && ! processor.isCapturing())
    {
        draggingSelection = true;
        dragAnchor = (double) (e.x - captureArea.getX()) / captureArea.getWidth() * processor.getCaptureSeconds();
    }
}

void StutterLabAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingSelection)
    {
        const double t = juce::jlimit (0.0, 1.0, (double) (e.x - captureArea.getX()) / captureArea.getWidth()) * processor.getCaptureSeconds();
        processor.setSelection (dragAnchor, t);
        return;
    }
    // Drag-out only starts from the DRAG TO DAW handle or the render waveform, and only the exact rendered file.
    if (! dragStarted && processor.hasRender() && e.getDistanceFromDragStart() > 6
        && (dragArea.contains (pressPoint) || renderArea.contains (pressPoint)))
    {
        const auto file = processor.getRenderInfo().file;
        if (file.existsAsFile())
        {
            dragStarted = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
        }
    }
}

void StutterLabAudioProcessorEditor::mouseUp (const juce::MouseEvent&) { draggingSelection = false; dragStarted = false; }
