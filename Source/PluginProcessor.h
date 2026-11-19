#pragma once

#include <atomic>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Pattern.h"
#include "PatternGenerator.h"
#include "RhythmEngine.h"
#include "WavExporter.h"

class RhythmForgeProcessor : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    RhythmForgeProcessor();
    ~RhythmForgeProcessor() override;

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "RhythmForge"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- used by the editor (message thread) ---
    juce::AudioProcessorValueTreeState apvts;

    rf::Pattern& getPattern() noexcept { return pattern; }
    int  getCurrentStep() const noexcept       { return currentStep.load(); }   // -1 when not running
    bool isUsingHostTransport() const noexcept { return usingHost.load(); }
    double getDisplayBpm() const noexcept      { return displayBpm.load(); }

    int getTimeSigIndex() const noexcept;
    int getSubdivIndex() const noexcept;
    int getFillEveryBars() const noexcept;
    int getStepsPerBar() const noexcept;

    void randomisePattern();          // new random seed + regenerate (also when locked)
    void regenerateFromSeed();        // same seed -> discards manual edits (also when locked)
    void clearPattern (int layer);

    // WAV export (non-blocking: runs on its own thread)
    std::atomic<int>  exportBars { 4 };
    std::atomic<int>  exportRateIndex { 1 };      // 48 kHz
    std::atomic<int>  exportBitDepthIndex { 1 };  // 24-bit
    std::atomic<bool> exportAddTail { true };

    bool startExport (const juce::File& file);
    void cancelExport();
    rf::ExportJob* getExportJob() const noexcept { return exportJob.get(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void timerCallback() override;
    rf::GeneratorSettings readGeneratorSettings() const;
    void applyGenerated (const rf::GeneratorSettings&);
    rf::RhythmEngine::Settings readEngineSettings() const;

    rf::Pattern pattern;
    rf::RhythmEngine engine;
    double currentSampleRate = 44100.0;

    // cached raw parameter pointers (safe to read from the audio thread)
    std::atomic<float> *pSync = nullptr, *pPlay = nullptr, *pBpm = nullptr, *pTimeSig = nullptr, *pSubdiv = nullptr,
                       *pStyle = nullptr, *pDensity = nullptr, *pAccent = nullptr, *pSwing = nullptr,
                       *pFillAmt = nullptr, *pFillEvery = nullptr, *pSeed = nullptr, *pLock = nullptr,
                       *pMaster = nullptr;
    std::atomic<float>* pLevels[rf::numInstruments] = { nullptr, nullptr, nullptr, nullptr };

    // audio-thread state
    bool   wasRunning = false;
    bool   lastSourceWasHost = false;
    double lastTrigTo = 0.0;
    double internalPpq = 0.0;

    // published to the UI
    std::atomic<int>    currentStep { -1 };
    std::atomic<bool>   usingHost { false };
    std::atomic<double> displayBpm { 120.0 };
    std::atomic<double> lastHostBpm { 0.0 };    // 0 = host never reported a tempo

    // message-thread state
    rf::GeneratorSettings lastGenerated;
    std::atomic<bool> forceRegenerate { false };
    std::unique_ptr<rf::ExportJob> exportJob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhythmForgeProcessor)
};
