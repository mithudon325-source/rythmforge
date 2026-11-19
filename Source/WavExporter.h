#pragma once

#include <atomic>
#include <juce_core/juce_core.h>
#include "Pattern.h"
#include "RhythmEngine.h"

namespace rf
{
    // Renders N bars to a WAV file on a background thread. It owns a private copy of the pattern,
    // a private RhythmEngine and a private file stream, so it cannot touch (or block) the audio thread.
    class ExportJob : public juce::Thread
    {
    public:
        enum class State { Running, Done, Failed, Cancelled };

        struct Request
        {
            RhythmEngine::Settings engineSettings;
            double bpm = 120.0;
            int    bars = 4;
            double sampleRate = 48000.0;
            int    bitDepth = 24;          // 16, 24 (integer PCM) or 32 (IEEE float)
            bool   addTail = true;         // append 1 s so open hats can ring out
            juce::File file;
        };

        ExportJob (const Pattern& sourcePattern, const Request& req);
        ~ExportJob() override;

        void run() override;

        State getState() const noexcept        { return state.load(); }
        float getProgress() const noexcept     { return progress.load(); }

        // Only valid once getState() != Running.
        juce::String getMessage() const        { return message; }

    private:
        bool writeWav (const juce::File& target);

        Pattern pattern;
        Request request;
        std::atomic<State> state { State::Running };
        std::atomic<float> progress { 0.0f };
        juce::String message;
    };
}
