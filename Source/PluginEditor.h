#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Clickable step grid: one row per instrument, one column per step of the bar.
class StepGridComponent : public juce::Component
{
public:
    explicit StepGridComponent (RhythmForgeProcessor& p) : proc (p) {}

    void setLayer (int newLayer) { layer = newLayer; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    struct Layout
    {
        juce::Rectangle<float> cells;
        int steps = 16;
        float cellW = 0, rowH = 0;
    };

    Layout getLayout() const;
    bool findCell (juce::Point<float> p, int& inst, int& step) const;
    static juce::Colour colourFor (int inst);

    RhythmForgeProcessor& proc;
    int layer = 0;
    int dragValue = 0;
    int lastInst = -1, lastStep = -1;
};

class RhythmForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RhythmForgeLookAndFeel();
};

class RhythmForgeEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit RhythmForgeEditor (RhythmForgeProcessor&);
    ~RhythmForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment    = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void setupLabel (juce::Label&, const juce::String& text);
    void setupSlider (juce::Slider&, int textBoxWidth = 52);
    void beginExport();
    void updateExportUi();

    RhythmForgeProcessor& proc;
    RhythmForgeLookAndFeel laf;

    // header
    juce::Label title, tempoInfo;
    juce::ToggleButton syncToggle { "Host sync" }, playToggle { "Play (internal)" };
    juce::Label bpmLabel;  juce::Slider bpmSlider;

    // generator row
    juce::Label tsLabel, sdLabel, styleLabel, fillEveryLabel, seedLabel;
    juce::ComboBox tsBox, sdBox, styleBox, fillEveryBox;
    juce::Slider seedSlider;
    juce::TextButton randomiseButton { "Randomise" }, revertButton { "Revert" }, clearButton { "Clear" };
    juce::ToggleButton lockToggle { "Lock pattern" };

    // variation row
    juce::Label densityLabel, accentLabel, swingLabel, fillAmtLabel;
    juce::Slider densitySlider, accentSlider, swingSlider, fillAmtSlider;

    // grid
    StepGridComponent grid;
    juce::TextButton mainLayerButton { "Main pattern" }, fillLayerButton { "Fill pattern" };
    juce::Label gridHint;

    // mixer
    juce::Label mixLabels[5];
    juce::Slider mixSliders[5];

    // export
    juce::Label exportTitle, exportBarsLabel, exportRateLabel, exportBitsLabel, exportStatus;
    juce::Slider exportBarsSlider;
    juce::ComboBox exportRateBox, exportBitsBox;
    juce::ToggleButton exportTailToggle { "Add 1 s tail" };
    juce::TextButton exportButton { "Export WAV..." }, cancelButton { "Cancel" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    std::unique_ptr<juce::FileChooser> chooser;
    bool exportWasRunning = false;

    std::unique_ptr<SliderAttachment> bpmAtt, seedAtt, densityAtt, accentAtt, swingAtt, fillAmtAtt, mixAtt[5];
    std::unique_ptr<ComboAttachment> tsAtt, sdAtt, styleAtt, fillEveryAtt;
    std::unique_ptr<ButtonAttachment> syncAtt, playAtt, lockAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhythmForgeEditor)
};
