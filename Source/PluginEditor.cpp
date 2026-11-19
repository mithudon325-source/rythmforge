#include "PluginEditor.h"

using namespace rf;

// =================================================================================================
// Look and feel
// =================================================================================================
RhythmForgeLookAndFeel::RhythmForgeLookAndFeel()
{
    setColourScheme (getMidnightColourScheme());
    setColour (juce::Slider::thumbColourId, juce::Colour (0xffffb74d));
    setColour (juce::Slider::trackColourId, juce::Colour (0xffe08a1e));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe08a1e));
    setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffffb74d));
    setColour (juce::ProgressBar::foregroundColourId, juce::Colour (0xffe08a1e));
}

// =================================================================================================
// Step grid
// =================================================================================================
juce::Colour StepGridComponent::colourFor (int inst)
{
    switch (inst)
    {
        case Kick:      return juce::Colour (0xffff7043);
        case Snare:     return juce::Colour (0xffffca28);
        case ClosedHat: return juce::Colour (0xff26c6da);
        default:        return juce::Colour (0xff6d8dff);
    }
}

StepGridComponent::Layout StepGridComponent::getLayout() const
{
    Layout l;
    l.steps = proc.getStepsPerBar();
    l.cells = getLocalBounds().toFloat().withTrimmedLeft (84.0f).withTrimmedTop (18.0f);
    l.cellW = l.cells.getWidth() / (float) l.steps;
    l.rowH  = l.cells.getHeight() / (float) numInstruments;
    return l;
}

bool StepGridComponent::findCell (juce::Point<float> p, int& inst, int& step) const
{
    const auto l = getLayout();
    if (! l.cells.contains (p)) return false;

    step = juce::jlimit (0, l.steps - 1, (int) ((p.x - l.cells.getX()) / l.cellW));
    inst = juce::jlimit (0, numInstruments - 1, (int) ((p.y - l.cells.getY()) / l.rowH));
    return true;
}

void StepGridComponent::paint (juce::Graphics& g)
{
    const auto l = getLayout();
    const int ts = proc.getTimeSigIndex(), sd = proc.getSubdivIndex();
    const double stepLen = stepLengthInQuarters (sd);
    const double pulseLen = isCompoundMeter (ts) ? 1.5 : 1.0;
    const int playhead = proc.getCurrentStep();
    const int fillStart = l.steps / 2;

    // instrument names
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    for (int i = 0; i < numInstruments; ++i)
    {
        g.setColour (colourFor (i));
        g.drawText (instrumentNames[i], 4, (int) (l.cells.getY() + (float) i * l.rowH), 78, (int) l.rowH,
                    juce::Justification::centredLeft);
    }

    // cells
    for (int i = 0; i < numInstruments; ++i)
    {
        for (int k = 0; k < l.steps; ++k)
        {
            const auto cell = juce::Rectangle<float> (l.cells.getX() + (float) k * l.cellW,
                                                      l.cells.getY() + (float) i * l.rowH,
                                                      l.cellW, l.rowH).reduced (2.0f);
            const bool unused = (layer == 1 && k < fillStart);
            const uint8_t v = proc.getPattern().get (layer, i, k);
            const auto col = colourFor (i);

            g.setColour (juce::Colour (0xff262a33));
            g.fillRoundedRectangle (cell, 4.0f);

            if (v == CellNormal)
            {
                g.setColour (col.withAlpha (unused ? 0.25f : 0.80f));
                g.fillRoundedRectangle (cell, 4.0f);
            }
            else if (v == CellAccent)
            {
                g.setColour (col.brighter (0.3f).withAlpha (unused ? 0.3f : 1.0f));
                g.fillRoundedRectangle (cell, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (unused ? 0.3f : 0.95f));
                g.drawRoundedRectangle (cell, 4.0f, 1.6f);
            }
            else if (v == CellGhost)
            {
                g.setColour (col.withAlpha (unused ? 0.2f : 0.55f));
                g.fillRoundedRectangle (cell.reduced (cell.getWidth() * 0.18f, cell.getHeight() * 0.28f), 3.0f);
            }

            if (unused)
            {
                g.setColour (juce::Colour (0x66000000));
                g.fillRoundedRectangle (cell, 4.0f);
            }
        }
    }

    // beat markers and numbers
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (int k = 0; k < l.steps; ++k)
    {
        const double t = (double) k * stepLen;
        const double rem = std::fmod (t + 1.0e-9, pulseLen);
        if (rem < 1.0e-6 + 1.0e-9)
        {
            const float x = l.cells.getX() + (float) k * l.cellW;
            g.setColour (juce::Colour (0x66ffffff));
            g.drawLine (x, l.cells.getY(), x, l.cells.getBottom(), 1.2f);
            g.setColour (juce::Colours::lightgrey);
            g.drawText (juce::String ((int) std::floor ((t + 1.0e-6) / pulseLen) + 1),
                        (int) x + 3, 0, 24, 16, juce::Justification::centredLeft);
        }
    }

    // playhead
    if (playhead >= 0 && playhead < l.steps)
    {
        const auto r = juce::Rectangle<float> (l.cells.getX() + (float) playhead * l.cellW, l.cells.getY(),
                                               l.cellW, l.cells.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.fillRect (r);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawRect (r, 1.5f);
    }
}

void StepGridComponent::mouseDown (const juce::MouseEvent& e)
{
    int inst = 0, step = 0;
    if (! findCell (e.position, inst, step)) return;
    if (layer == 1 && step < proc.getStepsPerBar() / 2) return;      // the first half of a fill bar plays the main pattern

    const uint8_t current = proc.getPattern().get (layer, inst, step);
    dragValue = (e.mods.isRightButtonDown() || e.mods.isCtrlDown()) ? 0 : (current + 1) % 4;   // off > normal > accent > ghost > off

    proc.getPattern().set (layer, inst, step, (uint8_t) dragValue);
    lastInst = inst; lastStep = step;
    repaint();
}

void StepGridComponent::mouseDrag (const juce::MouseEvent& e)
{
    int inst = 0, step = 0;
    if (! findCell (e.position, inst, step)) return;
    if (inst == lastInst && step == lastStep) return;
    if (layer == 1 && step < proc.getStepsPerBar() / 2) return;

    proc.getPattern().set (layer, inst, step, (uint8_t) dragValue);
    lastInst = inst; lastStep = step;
    repaint();
}

// =================================================================================================
// Editor
// =================================================================================================
void RhythmForgeEditor::setupLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::Font (juce::FontOptions (12.0f)));
    l.setColour (juce::Label::textColourId, juce::Colour (0xffaab0bd));
    l.setJustificationType (juce::Justification::centredLeft);
    l.setBorderSize ({ 0, 0, 0, 0 });
    addAndMakeVisible (l);
}

void RhythmForgeEditor::setupSlider (juce::Slider& s, int textBoxWidth)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, textBoxWidth, 20);
    addAndMakeVisible (s);
}

RhythmForgeEditor::RhythmForgeEditor (RhythmForgeProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), grid (p)
{
    setLookAndFeel (&laf);

    title.setText ("RhythmForge", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffffb74d));
    addAndMakeVisible (title);

    tempoInfo.setJustificationType (juce::Justification::centredRight);
    tempoInfo.setColour (juce::Label::textColourId, juce::Colour (0xffaab0bd));
    addAndMakeVisible (tempoInfo);

    addAndMakeVisible (syncToggle);
    addAndMakeVisible (playToggle);
    syncToggle.setTooltip ("Follow the host's tempo and play/stop. Falls back to the manual tempo if the host gives no tempo.");
    playToggle.setTooltip ("Start/stop the internal clock (used when Host sync is off, or in the standalone app).");

    setupLabel (bpmLabel, "Manual tempo (BPM)");
    setupSlider (bpmSlider, 60);
    bpmAtt = std::make_unique<SliderAttachment> (proc.apvts, pid::bpm, bpmSlider);
    syncAtt = std::make_unique<ButtonAttachment> (proc.apvts, pid::sync, syncToggle);
    playAtt = std::make_unique<ButtonAttachment> (proc.apvts, pid::play, playToggle);

    // generator row
    setupLabel (tsLabel, "Time signature");
    setupLabel (sdLabel, "Subdivision");
    setupLabel (styleLabel, "Style");
    setupLabel (fillEveryLabel, "Fill");
    setupLabel (seedLabel, "Pattern seed");

    for (int i = 0; i < numTimeSignatures; ++i) tsBox.addItem (timeSignatures[i].name, i + 1);
    for (int i = 0; i < numSubdivisions; ++i)   sdBox.addItem (subdivisions[i].name, i + 1);
    for (int i = 0; i < numStyles; ++i)         styleBox.addItem (styleNames[i], i + 1);
    for (int i = 0; i < numFillEvery; ++i)      fillEveryBox.addItem (fillEveryNames[i], i + 1);
    addAndMakeVisible (tsBox); addAndMakeVisible (sdBox); addAndMakeVisible (styleBox); addAndMakeVisible (fillEveryBox);

    tsAtt = std::make_unique<ComboAttachment> (proc.apvts, pid::timeSig, tsBox);
    sdAtt = std::make_unique<ComboAttachment> (proc.apvts, pid::subdiv, sdBox);
    styleAtt = std::make_unique<ComboAttachment> (proc.apvts, pid::style, styleBox);
    fillEveryAtt = std::make_unique<ComboAttachment> (proc.apvts, pid::fillEvery, fillEveryBox);

    setupSlider (seedSlider, 52);
    seedAtt = std::make_unique<SliderAttachment> (proc.apvts, pid::seed, seedSlider);

    addAndMakeVisible (randomiseButton); addAndMakeVisible (revertButton); addAndMakeVisible (clearButton);
    addAndMakeVisible (lockToggle);
    randomiseButton.setTooltip ("Pick a new random seed and generate a new pattern.");
    revertButton.setTooltip ("Regenerate the pattern from the current seed (discards manual edits).");
    clearButton.setTooltip ("Clear the pattern that is currently shown (main or fill).");
    lockToggle.setTooltip ("Keep the current pattern (including your edits) when generator settings change.");
    lockAtt = std::make_unique<ButtonAttachment> (proc.apvts, pid::lock, lockToggle);

    randomiseButton.onClick = [this] { proc.randomisePattern(); };
    revertButton.onClick    = [this] { proc.regenerateFromSeed(); };
    clearButton.onClick     = [this] { proc.clearPattern (fillLayerButton.getToggleState() ? 1 : 0); grid.repaint(); };

    // variation row
    setupLabel (densityLabel, "Density");
    setupLabel (accentLabel, "Accents");
    setupLabel (swingLabel, "Swing (1/8 and 1/16 only)");
    setupLabel (fillAmtLabel, "Fill intensity");
    setupSlider (densitySlider); setupSlider (accentSlider); setupSlider (swingSlider); setupSlider (fillAmtSlider);
    densityAtt = std::make_unique<SliderAttachment> (proc.apvts, pid::density, densitySlider);
    accentAtt = std::make_unique<SliderAttachment> (proc.apvts, pid::accent, accentSlider);
    swingAtt = std::make_unique<SliderAttachment> (proc.apvts, pid::swing, swingSlider);
    fillAmtAtt = std::make_unique<SliderAttachment> (proc.apvts, pid::fillAmt, fillAmtSlider);

    // grid
    addAndMakeVisible (grid);
    addAndMakeVisible (mainLayerButton);
    addAndMakeVisible (fillLayerButton);
    for (auto* b : { &mainLayerButton, &fillLayerButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (101);
    }
    mainLayerButton.setToggleState (true, juce::dontSendNotification);
    mainLayerButton.onClick = [this] { grid.setLayer (0); };
    fillLayerButton.onClick = [this] { grid.setLayer (1); };
    gridHint.setText ("Click a step: off > normal > accent > ghost. Right-click / Ctrl-click clears. Drag to paint. "
                      "The fill pattern replaces the 2nd half of the bar on fill bars.", juce::dontSendNotification);
    gridHint.setFont (juce::Font (juce::FontOptions (11.0f)));
    gridHint.setColour (juce::Label::textColourId, juce::Colour (0xff8b92a1));
    addAndMakeVisible (gridHint);

    // mixer
    const char* mixNames[5] = { "Kick", "Snare", "Closed HH", "Open HH", "Master (dB)" };
    const char* mixIds[5] = { pid::kickLvl, pid::snareLvl, pid::chhLvl, pid::ohhLvl, pid::master };
    for (int i = 0; i < 5; ++i)
    {
        setupLabel (mixLabels[i], mixNames[i]);
        setupSlider (mixSliders[i], 44);
        mixAtt[i] = std::make_unique<SliderAttachment> (proc.apvts, mixIds[i], mixSliders[i]);
    }

    // export
    exportTitle.setText ("Export WAV (renders offline, independent of playback)", juce::dontSendNotification);
    exportTitle.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    exportTitle.setColour (juce::Label::textColourId, juce::Colour (0xffffb74d));
    addAndMakeVisible (exportTitle);

    setupLabel (exportBarsLabel, "Bars");
    setupLabel (exportRateLabel, "Sample rate");
    setupLabel (exportBitsLabel, "Bit depth");

    exportBarsSlider.setSliderStyle (juce::Slider::IncDecButtons);
    exportBarsSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 24);
    exportBarsSlider.setRange (1, 512, 1);
    exportBarsSlider.setValue (proc.exportBars.load(), juce::dontSendNotification);
    exportBarsSlider.onValueChange = [this] { proc.exportBars = (int) exportBarsSlider.getValue(); };
    addAndMakeVisible (exportBarsSlider);

    for (int i = 0; i < numExportSampleRates; ++i) exportRateBox.addItem (juce::String (exportSampleRates[i]) + " Hz", i + 1);
    for (int i = 0; i < numExportBitDepths; ++i)   exportBitsBox.addItem (exportBitDepthNames[i], i + 1);
    exportRateBox.setSelectedId (proc.exportRateIndex.load() + 1, juce::dontSendNotification);
    exportBitsBox.setSelectedId (proc.exportBitDepthIndex.load() + 1, juce::dontSendNotification);
    exportRateBox.onChange = [this] { proc.exportRateIndex = exportRateBox.getSelectedId() - 1; };
    exportBitsBox.onChange = [this] { proc.exportBitDepthIndex = exportBitsBox.getSelectedId() - 1; };
    addAndMakeVisible (exportRateBox); addAndMakeVisible (exportBitsBox);

    exportTailToggle.setToggleState (proc.exportAddTail.load(), juce::dontSendNotification);
    exportTailToggle.onClick = [this] { proc.exportAddTail = exportTailToggle.getToggleState(); };
    exportTailToggle.setTooltip ("Appends one second after the last bar so open hats can ring out. Turn off for exact loop length.");
    addAndMakeVisible (exportTailToggle);

    addAndMakeVisible (exportButton);
    addAndMakeVisible (cancelButton);
    cancelButton.setEnabled (false);
    exportButton.onClick = [this] { beginExport(); };
    cancelButton.onClick = [this] { proc.cancelExport(); };

    addAndMakeVisible (progressBar);
    exportStatus.setColour (juce::Label::textColourId, juce::Colour (0xffaab0bd));
    exportStatus.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (exportStatus);

    setSize (900, 596);
    startTimerHz (30);
}

RhythmForgeEditor::~RhythmForgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void RhythmForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1e24));

    g.setColour (juce::Colour (0xff22262e));
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop (456.0f).reduced (8.0f, 4.0f), 8.0f);
}

void RhythmForgeEditor::resized()
{
    auto area = getLocalBounds().reduced (14, 10);

    auto placeLabelled = [] (juce::Label& label, juce::Component& comp, juce::Rectangle<int> r)
    {
        label.setBounds (r.removeFromTop (16));
        comp.setBounds (r.withHeight (26));
    };

    // header
    auto header = area.removeFromTop (36);
    title.setBounds (header.removeFromLeft (190));
    syncToggle.setBounds (header.removeFromLeft (110));
    playToggle.setBounds (header.removeFromLeft (140));
    tempoInfo.setBounds (header.removeFromRight (230));
    auto bpmArea = header.reduced (8, 0);
    bpmLabel.setBounds (bpmArea.removeFromTop (14));
    bpmSlider.setBounds (bpmArea.removeFromTop (22));

    area.removeFromTop (8);

    // generator row
    auto row = area.removeFromTop (44);
    placeLabelled (tsLabel, tsBox, row.removeFromLeft (100)); row.removeFromLeft (8);
    placeLabelled (sdLabel, sdBox, row.removeFromLeft (170)); row.removeFromLeft (8);
    placeLabelled (styleLabel, styleBox, row.removeFromLeft (110)); row.removeFromLeft (8);
    placeLabelled (fillEveryLabel, fillEveryBox, row.removeFromLeft (120)); row.removeFromLeft (8);
    placeLabelled (seedLabel, seedSlider, row.removeFromLeft (170)); row.removeFromLeft (8);
    auto buttons = row.withTrimmedTop (16).withHeight (26);
    randomiseButton.setBounds (buttons.removeFromLeft (84)); buttons.removeFromLeft (4);
    revertButton.setBounds (buttons.removeFromLeft (62));    buttons.removeFromLeft (4);
    clearButton.setBounds (buttons.removeFromLeft (52));     buttons.removeFromLeft (6);
    lockToggle.setBounds (buttons);

    area.removeFromTop (6);

    // variation row
    row = area.removeFromTop (44);
    const int w4 = (row.getWidth() - 3 * 12) / 4;
    placeLabelled (densityLabel, densitySlider, row.removeFromLeft (w4)); row.removeFromLeft (12);
    placeLabelled (accentLabel, accentSlider, row.removeFromLeft (w4)); row.removeFromLeft (12);
    placeLabelled (swingLabel, swingSlider, row.removeFromLeft (w4)); row.removeFromLeft (12);
    placeLabelled (fillAmtLabel, fillAmtSlider, row);

    area.removeFromTop (8);

    // grid
    auto layerRow = area.removeFromTop (26);
    mainLayerButton.setBounds (layerRow.removeFromLeft (110)); layerRow.removeFromLeft (4);
    fillLayerButton.setBounds (layerRow.removeFromLeft (110)); layerRow.removeFromLeft (10);
    gridHint.setBounds (layerRow);
    area.removeFromTop (4);
    grid.setBounds (area.removeFromTop (214));

    area.removeFromTop (6);

    // mixer
    row = area.removeFromTop (44);
    const int w5 = (row.getWidth() - 4 * 10) / 5;
    for (int i = 0; i < 5; ++i)
    {
        placeLabelled (mixLabels[i], mixSliders[i], row.removeFromLeft (w5));
        row.removeFromLeft (10);
    }

    // export panel (drawn on the rounded rectangle from paint())
    area.removeFromTop (14);
    auto ex = area.reduced (8, 0);
    exportTitle.setBounds (ex.removeFromTop (22));
    auto exRow = ex.removeFromTop (44);
    placeLabelled (exportBarsLabel, exportBarsSlider, exRow.removeFromLeft (130)); exRow.removeFromLeft (10);
    placeLabelled (exportRateLabel, exportRateBox, exRow.removeFromLeft (110)); exRow.removeFromLeft (10);
    placeLabelled (exportBitsLabel, exportBitsBox, exRow.removeFromLeft (120)); exRow.removeFromLeft (14);
    auto exButtons = exRow.withTrimmedTop (16).withHeight (26);
    exportTailToggle.setBounds (exButtons.removeFromLeft (110)); exButtons.removeFromLeft (6);
    exportButton.setBounds (exButtons.removeFromLeft (110)); exButtons.removeFromLeft (6);
    cancelButton.setBounds (exButtons.removeFromLeft (70));

    ex.removeFromTop (6);
    progressBar.setBounds (ex.removeFromTop (14));
    exportStatus.setBounds (ex.removeFromTop (22));
}

void RhythmForgeEditor::beginExport()
{
    const auto defaultFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                 .getChildFile ("RhythmForge.wav");

    chooser = std::make_unique<juce::FileChooser> ("Export WAV to...", defaultFile, "*.wav");
    juce::Component::SafePointer<RhythmForgeEditor> safeThis (this);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safeThis] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;

        auto file = fc.getResult();
        if (file == juce::File()) return;               // cancelled

        file = file.withFileExtension ("wav");
        if (! safeThis->proc.startExport (file))
            safeThis->exportStatus.setText ("An export is already running.", juce::dontSendNotification);
    });
}

void RhythmForgeEditor::updateExportUi()
{
    auto* job = proc.getExportJob();
    const bool running = job != nullptr && job->getState() == ExportJob::State::Running;

    exportButton.setEnabled (! running);
    cancelButton.setEnabled (running);

    if (running)
    {
        progressValue = (double) job->getProgress();
        exportStatus.setColour (juce::Label::textColourId, juce::Colour (0xffaab0bd));
        exportStatus.setText ("Rendering... " + juce::String (juce::roundToInt (job->getProgress() * 100.0f)) + "%",
                              juce::dontSendNotification);
        exportWasRunning = true;
    }
    else if (job != nullptr && exportWasRunning)
    {
        exportWasRunning = false;
        const bool ok = job->getState() == ExportJob::State::Done;
        progressValue = ok ? 1.0 : 0.0;
        exportStatus.setColour (juce::Label::textColourId, ok ? juce::Colour (0xff8bc34a) : juce::Colour (0xffef5350));
        exportStatus.setText (job->getMessage(), juce::dontSendNotification);
    }
    else if (job == nullptr)
        progressValue = 0.0;
}

void RhythmForgeEditor::timerCallback()
{
    grid.repaint();

    juce::String info;
    if (proc.isUsingHostTransport())
        info = "Following host: " + juce::String (proc.getDisplayBpm(), 2) + " BPM";
    else if (playToggle.getToggleState())
        info = "Internal clock: " + juce::String (proc.getDisplayBpm(), 2) + " BPM";
    else
        info = syncToggle.getToggleState() ? "Waiting for host transport" : "Stopped (press Play)";
    tempoInfo.setText (info, juce::dontSendNotification);

    updateExportUi();
}
