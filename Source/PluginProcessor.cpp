#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace rf;

namespace
{
    constexpr double minBpm = 20.0, maxBpm = 400.0;

    juce::String patternToString (const Pattern& p)
    {
        juce::String s;
        for (int l = 0; l < numLayers; ++l)
            for (int i = 0; i < numInstruments; ++i)
                for (int k = 0; k < maxSteps; ++k)
                    s << (char) ('0' + juce::jlimit (0, 3, (int) p.get (l, i, k)));
        return s;
    }

    bool stringToPattern (const juce::String& s, Pattern& p)
    {
        if (s.length() != numLayers * numInstruments * maxSteps)
            return false;

        int idx = 0;
        for (int l = 0; l < numLayers; ++l)
            for (int i = 0; i < numInstruments; ++i)
                for (int k = 0; k < maxSteps; ++k)
                    p.set (l, i, k, (uint8_t) juce::jlimit (0, 3, (int) (s[idx++] - '0')));
        return true;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout RhythmForgeProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    StringArray tsNames, sdNames, styleList, fillNames;
    for (int i = 0; i < numTimeSignatures; ++i) tsNames.add (timeSignatures[i].name);
    for (int i = 0; i < numSubdivisions; ++i)   sdNames.add (subdivisions[i].name);
    for (int i = 0; i < numStyles; ++i)         styleList.add (styleNames[i]);
    for (int i = 0; i < numFillEvery; ++i)      fillNames.add (fillEveryNames[i]);

    params.push_back (std::make_unique<AudioParameterBool>   (id (pid::sync), "Host Sync", true));
    params.push_back (std::make_unique<AudioParameterBool>   (id (pid::play), "Play (internal clock)", false));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::bpm), "Tempo", NormalisableRange<float> (40.0f, 240.0f, 0.01f), 120.0f));
    params.push_back (std::make_unique<AudioParameterChoice> (id (pid::timeSig), "Time Signature", tsNames, 2));
    params.push_back (std::make_unique<AudioParameterChoice> (id (pid::subdiv), "Subdivision", sdNames, 1));
    params.push_back (std::make_unique<AudioParameterChoice> (id (pid::style), "Style", styleList, 0));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::density), "Density", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::accent), "Accents", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::swing), "Swing", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::fillAmt), "Fill Intensity", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<AudioParameterChoice> (id (pid::fillEvery), "Fill Every", fillNames, 0));
    params.push_back (std::make_unique<AudioParameterInt>    (id (pid::seed), "Pattern Seed", 0, 9999, 1));
    params.push_back (std::make_unique<AudioParameterBool>   (id (pid::lock), "Lock Pattern", false));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::kickLvl),  "Kick Level",      NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::snareLvl), "Snare Level",     NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::chhLvl),   "Closed HH Level", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::ohhLvl),   "Open HH Level",   NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back (std::make_unique<AudioParameterFloat>  (id (pid::master),   "Master (dB)",     NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -3.0f));

    return { params.begin(), params.end() };
}

RhythmForgeProcessor::RhythmForgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    pSync = apvts.getRawParameterValue (pid::sync);
    pPlay = apvts.getRawParameterValue (pid::play);
    pBpm = apvts.getRawParameterValue (pid::bpm);
    pTimeSig = apvts.getRawParameterValue (pid::timeSig);
    pSubdiv = apvts.getRawParameterValue (pid::subdiv);
    pStyle = apvts.getRawParameterValue (pid::style);
    pDensity = apvts.getRawParameterValue (pid::density);
    pAccent = apvts.getRawParameterValue (pid::accent);
    pSwing = apvts.getRawParameterValue (pid::swing);
    pFillAmt = apvts.getRawParameterValue (pid::fillAmt);
    pFillEvery = apvts.getRawParameterValue (pid::fillEvery);
    pSeed = apvts.getRawParameterValue (pid::seed);
    pLock = apvts.getRawParameterValue (pid::lock);
    pMaster = apvts.getRawParameterValue (pid::master);
    pLevels[Kick] = apvts.getRawParameterValue (pid::kickLvl);
    pLevels[Snare] = apvts.getRawParameterValue (pid::snareLvl);
    pLevels[ClosedHat] = apvts.getRawParameterValue (pid::chhLvl);
    pLevels[OpenHat] = apvts.getRawParameterValue (pid::ohhLvl);

    lastGenerated = readGeneratorSettings();
    applyGenerated (lastGenerated);

    startTimerHz (30);
}

RhythmForgeProcessor::~RhythmForgeProcessor()
{
    stopTimer();
    exportJob.reset();   // stops and joins the export thread
}

// ------------------------------------------------------------------------------------------------
int RhythmForgeProcessor::getTimeSigIndex() const noexcept  { return clampIndex ((int) std::lround (pTimeSig->load()), numTimeSignatures); }
int RhythmForgeProcessor::getSubdivIndex() const noexcept   { return clampIndex ((int) std::lround (pSubdiv->load()), numSubdivisions); }
int RhythmForgeProcessor::getFillEveryBars() const noexcept { return fillEveryValues[clampIndex ((int) std::lround (pFillEvery->load()), numFillEvery)]; }
int RhythmForgeProcessor::getStepsPerBar() const noexcept   { return stepsPerBar (getTimeSigIndex(), getSubdivIndex()); }

GeneratorSettings RhythmForgeProcessor::readGeneratorSettings() const
{
    GeneratorSettings g;
    g.timeSig    = getTimeSigIndex();
    g.subdiv     = getSubdivIndex();
    g.style      = clampIndex ((int) std::lround (pStyle->load()), numStyles);
    g.seed       = (int) std::lround (pSeed->load());
    g.fillEvery  = clampIndex ((int) std::lround (pFillEvery->load()), numFillEvery);
    g.density    = pDensity->load();
    g.accent     = pAccent->load();
    g.fillAmount = pFillAmt->load();
    return g;
}

RhythmEngine::Settings RhythmForgeProcessor::readEngineSettings() const
{
    RhythmEngine::Settings s;
    s.timeSig = getTimeSigIndex();
    s.subdiv = getSubdivIndex();
    s.swing = pSwing->load();
    s.fillEveryBars = getFillEveryBars();
    for (int i = 0; i < numInstruments; ++i)
        s.levels[i] = pLevels[i]->load();

    const float db = pMaster->load();
    s.masterGain = db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
    return s;
}

void RhythmForgeProcessor::applyGenerated (const GeneratorSettings& gs)
{
    // Generate into a plain local struct first, then publish each cell with a single atomic store.
    const GeneratedPattern g = generatePattern (gs);
    for (int l = 0; l < numLayers; ++l)
        for (int i = 0; i < numInstruments; ++i)
            for (int k = 0; k < maxSteps; ++k)
                pattern.set (l, i, k, g.cells[l][i][k]);
}

void RhythmForgeProcessor::timerCallback()
{
    const auto gs = readGeneratorSettings();

    if (forceRegenerate.exchange (false))
    {
        lastGenerated = gs;
        applyGenerated (gs);
    }
    else if (gs != lastGenerated)
    {
        lastGenerated = gs;
        if (pLock->load() < 0.5f)       // locked patterns keep manual edits
            applyGenerated (gs);
    }
}

void RhythmForgeProcessor::randomisePattern()
{
    juce::Random rng;
    if (auto* p = apvts.getParameter (pid::seed))
    {
        const int newSeed = rng.nextInt (10000);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) newSeed));
    }
    forceRegenerate = true;
}

void RhythmForgeProcessor::regenerateFromSeed() { forceRegenerate = true; }

void RhythmForgeProcessor::clearPattern (int layer) { pattern.clearLayer (layer); }

// ------------------------------------------------------------------------------------------------
void RhythmForgeProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    engine.reset();
    wasRunning = false;
    lastSourceWasHost = false;
    internalPpq = 0.0;
    lastTrigTo = 0.0;
}

bool RhythmForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void RhythmForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    // ---- 1. work out where we are in musical time ---------------------------------------------
    double bpm = juce::jlimit (minBpm, maxBpm, (double) pBpm->load());
    const bool syncRequested = pSync->load() > 0.5f;
    const bool internalPlay  = pPlay->load() > 0.5f;

    bool hostDriving = false, hostPlaying = false;
    double hostPpq = 0.0;

    if (syncRequested)
    {
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                if (auto hostBpm = pos->getBpm())
                {
                    bpm = juce::jlimit (minBpm, maxBpm, (double) *hostBpm);
                    lastHostBpm = bpm;
                }

                bool havePosition = false;
                if (auto ppq = pos->getPpqPosition())
                {
                    hostPpq = *ppq;
                    havePosition = true;
                }
                else if (auto samples = pos->getTimeInSamples())
                {
                    hostPpq = ((double) *samples / currentSampleRate) * bpm / 60.0;   // assumes constant tempo
                    havePosition = true;
                }

                hostPlaying = pos->getIsPlaying();
                hostDriving = havePosition;
            }
        }
    }

    const double ppqPerSample = bpm / (60.0 * currentSampleRate);

    bool trigger = false;
    double ppqStart = 0.0;

    if (hostDriving)
    {
        if (hostPlaying) { trigger = true; ppqStart = hostPpq; }
    }
    else if (internalPlay)
    {
        if (! wasRunning || lastSourceWasHost)
            internalPpq = 0.0;                       // (re)start from the top of the bar
        trigger = true;
        ppqStart = internalPpq;
    }

    RhythmEngine::BlockTiming timing;
    timing.ppqPerSample = ppqPerSample;
    timing.ppqStart = ppqStart;

    if (trigger)
    {
        const double ppqEnd = ppqStart + (double) numSamples * ppqPerSample;
        double from = ppqStart;

        // Keep consecutive blocks gap-free and overlap-free even if the host's reported position
        // differs from our prediction by a rounding error (a few samples). Bigger jumps = relocate/loop.
        if (wasRunning && lastSourceWasHost == hostDriving && std::abs (ppqStart - lastTrigTo) < 8.0 * ppqPerSample)
            from = lastTrigTo;

        timing.trigFrom = from;
        timing.trigTo = ppqEnd;
        lastTrigTo = ppqEnd;
        if (! hostDriving)
            internalPpq = ppqEnd;
    }
    // else: empty window (trigFrom == trigTo == 0) -> no new hits, voices keep ringing out

    // ---- 2. render -----------------------------------------------------------------------------
    const auto settings = readEngineSettings();
    engine.renderBlock (pattern, settings, timing, buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // ---- 3. publish state for the UI -----------------------------------------------------------
    if (trigger)
    {
        const double barQ = barLengthInQuarters (settings.timeSig);
        double inBar = std::fmod (ppqStart, barQ);
        if (inBar < 0.0) inBar += barQ;
        const int steps = stepsPerBar (settings.timeSig, settings.subdiv);
        const int step = (int) std::floor (inBar / stepLengthInQuarters (settings.subdiv) + 1.0e-6);
        currentStep.store (juce::jlimit (0, steps - 1, step));
    }
    else
        currentStep.store (-1);

    usingHost.store (hostDriving);
    displayBpm.store (bpm);

    wasRunning = trigger;
    lastSourceWasHost = hostDriving;
}

// ------------------------------------------------------------------------------------------------
bool RhythmForgeProcessor::startExport (const juce::File& file)
{
    if (exportJob != nullptr && exportJob->isThreadRunning())
        return false;

    exportJob.reset();

    ExportJob::Request req;
    req.engineSettings = readEngineSettings();
    req.bars = juce::jlimit (1, 512, exportBars.load());
    req.sampleRate = (double) exportSampleRates[clampIndex (exportRateIndex.load(), numExportSampleRates)];
    req.bitDepth = exportBitDepths[clampIndex (exportBitDepthIndex.load(), numExportBitDepths)];
    req.addTail = exportAddTail.load();
    req.file = file;

    // Tempo: the host's tempo if we are following a host that has reported one, otherwise the manual tempo.
    double bpm = (double) pBpm->load();
    if (pSync->load() > 0.5f && lastHostBpm.load() > 0.0)
        bpm = lastHostBpm.load();
    req.bpm = bpm;

    exportJob = std::make_unique<ExportJob> (pattern, req);
    exportJob->startThread (juce::Thread::Priority::background);
    return true;
}

void RhythmForgeProcessor::cancelExport()
{
    if (exportJob != nullptr && exportJob->isThreadRunning())
        exportJob->signalThreadShouldExit();
}

// ------------------------------------------------------------------------------------------------
void RhythmForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    state.setProperty ("exportBars", exportBars.load(), nullptr);
    state.setProperty ("exportRate", exportRateIndex.load(), nullptr);
    state.setProperty ("exportBits", exportBitDepthIndex.load(), nullptr);
    state.setProperty ("exportTail", exportAddTail.load(), nullptr);

    juce::ValueTree patternTree ("PATTERN");
    patternTree.setProperty ("cells", patternToString (pattern), nullptr);
    state.removeChild (state.getChildWithName ("PATTERN"), nullptr);
    state.appendChild (patternTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RhythmForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const juce::String savedPattern = tree.getChildWithName ("PATTERN").getProperty ("cells").toString();

    exportBars = juce::jlimit (1, 512, (int) tree.getProperty ("exportBars", 4));
    exportRateIndex = clampIndex ((int) tree.getProperty ("exportRate", 1), numExportSampleRates);
    exportBitDepthIndex = clampIndex ((int) tree.getProperty ("exportBits", 1), numExportBitDepths);
    exportAddTail = (bool) tree.getProperty ("exportTail", true);

    apvts.replaceState (tree);

    lastGenerated = readGeneratorSettings();
    forceRegenerate = false;

    if (! stringToPattern (savedPattern, pattern))
        applyGenerated (lastGenerated);     // old / missing pattern data: regenerate from the parameters
}

juce::AudioProcessorEditor* RhythmForgeProcessor::createEditor()
{
    return new RhythmForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RhythmForgeProcessor();
}
