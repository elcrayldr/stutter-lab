#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// STUTTER LAB 0.4 editor: capture, waveform selection, render progress, FX rack and production controls.
class StutterLabAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StutterLabAudioProcessorEditor (StutterLabAudioProcessor&);
    ~StutterLabAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void refreshStatus();
    void doGenerate();
    void doExport();
    void drawPeaks (juce::Graphics&, juce::Rectangle<int>, const std::vector<float>& minMax, juce::Colour);
    void addLabelled (juce::Slider&, juce::Label&, const juce::String& text);

    StutterLabAudioProcessor& processor;
    juce::TextButton captureButton { "CAPTURE" }, stopButton { "STOP" }, clearButton { "CLEAR" },
                     generateButton { "GENERATE" }, previewButton { "PREVIEW" }, exportButton { "EXPORT WAV" };
    juce::ToggleButton phoneButton { "PHONE" }, scanButton { "SCAN SOURCE" }, loopButton { "LOOP PREVIEW" };
    juce::ComboBox divisionBox, directionBox, delayRateBox;
    juce::Slider repeatsSlider, swingSlider, gateSlider, humanizeSlider, driveSlider, bpmSlider;
    juce::Slider pitchSlider, formantSlider, delayMixSlider, delayFeedbackSlider, reverbMixSlider, reverbSizeSlider;
    juce::Label repeatsLabel, swingLabel, gateLabel, humanizeLabel, driveLabel, bpmLabel, divisionLabel, directionLabel;
    juce::Label pitchLabel, formantLabel, delayMixLabel, delayFeedbackLabel, reverbMixLabel, reverbSizeLabel, delayRateLabel;
    juce::Label statusLabel, infoLabel;
    juce::TextButton newSeedButton { "NEW SEED" }, storeAButton { "STORE A" }, storeBButton { "STORE B" }, abButton { "A/B" }, undoButton { "UNDO" }, savePresetButton { "SAVE" }, loadPresetButton { "LOAD" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::FileChooser> fileChooser;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> repeatsAttachment, swingAttachment, gateAttachment, humanizeAttachment, driveAttachment, bpmAttachment;
    std::unique_ptr<SliderAttachment> pitchAttachment, formantAttachment, delayMixAttachment, delayFeedbackAttachment, reverbMixAttachment, reverbSizeAttachment;
    std::unique_ptr<ComboAttachment> divisionAttachment, directionAttachment, delayRateAttachment;
    std::unique_ptr<ButtonAttachment> phoneAttachment, scanAttachment, loopAttachment;

    juce::Rectangle<int> captureArea, renderArea, dragArea, effectsArea;
    std::vector<float> capturePeaks, renderPeaks;
    int lastCaptureSamples = -1, lastW = 0;
    uint64_t shownRenderId = 0;
    bool wasRecording = false;
    float meterPeak = 0.0f;
    bool draggingSelection = false, dragStarted = false;
    juce::Point<int> pressPoint;
    double dragAnchor = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StutterLabAudioProcessorEditor)
};
