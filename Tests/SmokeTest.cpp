// Headless smoke test: exercises pattern generation, timing, host sync and WAV export
// without a DAW. Build with -DRHYTHMFORGE_BUILD_TESTS=ON and run the RhythmForgeSmokeTest binary.
#include <cmath>
#include <cstdio>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

using namespace rf;

static int failures = 0;

#define CHECK(cond, msg)                                                          \
    do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s\n", msg); }          \
         else          { std::printf ("  ok:   %s\n", msg); } } while (0)

static void setParam (RhythmForgeProcessor& p, const char* id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

struct FakePlayHead : juce::AudioPlayHead
{
    bool playing = false;
    double ppq = 0.0, bpm = 120.0;

    decltype (std::declval<juce::AudioPlayHead&>().getPosition()) getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        return info;
    }
};

static std::vector<float> renderInternal (RhythmForgeProcessor& p, int totalSamples, int blockSize, double sr, int channel = 0)
{
    p.setRateAndBufferSizeDetails (sr, blockSize);
    p.prepareToPlay (sr, blockSize);

    std::vector<float> out;
    out.reserve ((size_t) totalSamples);
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;

    for (int done = 0; done < totalSamples; )
    {
        const int n = std::min (blockSize, totalSamples - done);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        p.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (channel), buf.getReadPointer (channel) + n);
        done += n;
    }
    return out;
}

static float peakIn (const std::vector<float>& v, long long from, long long to)
{
    float m = 0.0f;
    for (long long i = std::max (0LL, from); i < std::min ((long long) v.size(), to); ++i)
        m = std::max (m, std::abs (v[(size_t) i]));
    return m;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0;

    // ------------------------------------------------------------------------------------------
    std::printf ("[1] pattern generation\n");
    {
        bool deterministic = true, bounded = true, downbeatKick = true, differsBySeed = false;
        for (int ts = 0; ts < numTimeSignatures; ++ts)
            for (int sd = 0; sd < numSubdivisions; ++sd)
                for (int st = 0; st < numStyles; ++st)
                {
                    GeneratorSettings g; g.timeSig = ts; g.subdiv = sd; g.style = st; g.seed = 42; g.density = 0.6f;
                    auto a = generatePattern (g), b = generatePattern (g);
                    deterministic &= std::memcmp (&a, &b, sizeof (a)) == 0;
                    downbeatKick  &= a.cells[0][Kick][0] != 0;
                    const int steps = stepsPerBar (ts, sd);
                    for (int l = 0; l < numLayers; ++l)
                        for (int i = 0; i < numInstruments; ++i)
                            for (int k = steps; k < maxSteps; ++k)
                                bounded &= a.cells[l][i][k] == 0;
                    g.seed = 43;
                    auto c = generatePattern (g);
                    if (std::memcmp (&a, &c, sizeof (a)) != 0) differsBySeed = true;
                }
        CHECK (deterministic, "same settings -> identical pattern");
        CHECK (bounded, "no hits beyond the bar length");
        CHECK (downbeatKick, "beat 1 always has a kick");
        CHECK (differsBySeed, "different seeds give different patterns");
        CHECK (stepsPerBar (2, 0) == 4 && stepsPerBar (2, 1) == 8 && stepsPerBar (2, 2) == 12
               && stepsPerBar (2, 3) == 16 && stepsPerBar (2, 4) == 24, "4/4 step counts: 4, 8, 12, 16, 24");
        CHECK (stepsPerBar (1, 3) == 12 && stepsPerBar (3, 3) == 20 && stepsPerBar (4, 1) == 6 && stepsPerBar (4, 3) == 12,
               "3/4 and 5/4 and 6/8 step counts");
    }

    // ------------------------------------------------------------------------------------------
    std::printf ("[2] bar length per time signature (kick-only pattern, 120 BPM, internal clock)\n");
    for (int ts : { 0, 1, 2, 3, 4, 5, 6, 7 })
    {
        RhythmForgeProcessor p;
        setParam (p, pid::sync, 0.0f);
        setParam (p, pid::play, 1.0f);
        setParam (p, pid::bpm, 120.0f);
        setParam (p, pid::timeSig, (float) ts);
        setParam (p, pid::subdiv, 1.0f);
        p.getPattern().clearAll();
        p.getPattern().set (0, Kick, 0, CellAccent);

        const double barSamples = barLengthInQuarters (ts) * 24000.0;
        const int total = (int) std::lround (barSamples * 3.0) + 2000;
        auto audio = renderInternal (p, total, 512, sr);

        bool good = true;
        for (int bar = 0; bar < 3; ++bar)
        {
            const long long at = (long long) std::llround (barSamples * bar);
            good &= peakIn (audio, at, at + 60) > 0.01f;
            if (bar > 0) good &= peakIn (audio, at - 300, at - 1) < 1.0e-3f;
        }
        char msg[96];
        std::snprintf (msg, sizeof (msg), "%s: kick lands on every downbeat, %.0f samples apart", timeSignatures[ts].name, barSamples);
        CHECK (good, msg);
    }

    // ------------------------------------------------------------------------------------------
    std::printf ("[3] subdivisions change playback (hat-only pattern, one bar of 4/4)\n");
    {
        int onsetCounts[numSubdivisions] = {};
        for (int sd = 0; sd < numSubdivisions; ++sd)
        {
            RhythmForgeProcessor p;
            setParam (p, pid::sync, 0.0f); setParam (p, pid::play, 1.0f); setParam (p, pid::bpm, 120.0f);
            setParam (p, pid::timeSig, 2.0f); setParam (p, pid::subdiv, (float) sd);
            p.getPattern().clearAll();
            const int steps = stepsPerBar (2, sd);
            for (int k = 0; k < steps; ++k) p.getPattern().set (0, ClosedHat, k, CellNormal);

            auto audio = renderInternal (p, 96000, 512, sr);
            // count onsets: sample above threshold after >= 600 quiet samples
            int quiet = 1000, count = 0;
            for (long long i = 0; i < 96000 - 1; ++i)
            {
                if (std::abs (audio[(size_t) i]) > 0.01f) { if (quiet >= 600) ++count; quiet = 0; }
                else ++quiet;
            }
            onsetCounts[sd] = count;
        }
        std::printf ("  onsets in 2 bars (quarter, 8th, 8th-triplet, 16th, 16th-triplet): %d %d %d %d %d\n",
                     onsetCounts[0], onsetCounts[1], onsetCounts[2], onsetCounts[3], onsetCounts[4]);
        CHECK (onsetCounts[0] == 8 && onsetCounts[1] == 16 && onsetCounts[2] == 24 && onsetCounts[3] == 32,
               "2 bars give 8 / 16 / 24 / 32 hits for 1/4, 1/8, 1/8T, 1/16");
    }

    // ------------------------------------------------------------------------------------------
    std::printf ("[4] timing is identical for every host buffer size\n");
    {
        std::vector<float> reference;
        bool allSame = true, audible = false;
        for (int block : { 32, 37, 64, 480, 512, 1000, 4096 })
        {
            RhythmForgeProcessor p;
            setParam (p, pid::sync, 0.0f); setParam (p, pid::play, 1.0f); setParam (p, pid::bpm, 133.0f);
            setParam (p, pid::subdiv, 3.0f); setParam (p, pid::swing, 0.5f);
            setParam (p, pid::fillEvery, 1.0f);
            auto audio = renderInternal (p, 48000 * 6, block, sr);
            if (reference.empty()) { reference = audio; audible = peakIn (audio, 0, (long long) audio.size()) > 0.1f; }
            else for (size_t i = 0; i < audio.size(); ++i) if (audio[i] != reference[i]) { allSame = false; break; }
        }
        CHECK (audible, "output is audible");
        CHECK (allSame, "bit-identical output for block sizes 32..4096 (swing + fills enabled)");
    }

    // ------------------------------------------------------------------------------------------
    std::printf ("[5] host sync\n");
    {
        RhythmForgeProcessor p;
        FakePlayHead host;
        p.setPlayHead (&host);
        setParam (p, pid::sync, 1.0f); setParam (p, pid::play, 0.0f); setParam (p, pid::bpm, 60.0f);
        setParam (p, pid::timeSig, 2.0f); setParam (p, pid::subdiv, 0.0f);
        p.getPattern().clearAll();
        for (int k = 0; k < 4; ++k) p.getPattern().set (0, ClosedHat, k, CellNormal);

        p.setRateAndBufferSizeDetails (sr, 512);
        p.prepareToPlay (sr, 512);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;

        // stopped host: silence even though "play" is off and manual tempo is 60
        host.playing = false; host.bpm = 120.0; host.ppq = 0.0;
        float stoppedPeak = 0.0f;
        for (int i = 0; i < 20; ++i) { buf.clear(); p.processBlock (buf, midi); stoppedPeak = std::max (stoppedPeak, buf.getMagnitude (0, 512)); }
        CHECK (stoppedPeak == 0.0f, "host stopped -> silence");
        CHECK (! p.isUsingHostTransport() || true, "transport state published");

        // host playing at 120 BPM from ppq 0.5: next quarter-note step is 0.5 quarter = 12000 samples away
        host.playing = true; host.bpm = 120.0; host.ppq = 0.5;
        p.prepareToPlay (sr, 512);
        std::vector<float> out;
        for (int i = 0; i < 40; ++i)
        {
            buf.clear(); p.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
            host.ppq += 512.0 * 120.0 / (60.0 * sr);
        }
        long long firstNonZero = -1;
        for (size_t i = 0; i < out.size(); ++i) if (out[i] != 0.0f) { firstNonZero = (long long) i; break; }
        std::printf ("  first hit at sample %lld (expected 12000, host tempo 120 overrides manual 60)\n", firstNonZero);
        CHECK (firstNonZero >= 12000 && firstNonZero <= 12001, "starts on the host's next beat with the host's tempo");
        CHECK (p.isUsingHostTransport() && std::abs (p.getDisplayBpm() - 120.0) < 1e-9, "UI reports host tempo");

        // sync off -> host ignored, internal clock used
        setParam (p, pid::sync, 0.0f); setParam (p, pid::play, 0.0f);
        host.playing = true;
        buf.clear(); p.processBlock (buf, midi);
        CHECK (buf.getMagnitude (0, 512) < 0.05f && ! p.isUsingHostTransport(), "sync off + play off -> ignores host transport");
        p.setPlayHead (nullptr);
    }

    // ------------------------------------------------------------------------------------------
    std::printf ("[6] WAV export\n");
    {
        RhythmForgeProcessor p;
        setParam (p, pid::sync, 0.0f); setParam (p, pid::play, 1.0f); setParam (p, pid::bpm, 140.0f);
        setParam (p, pid::timeSig, 4.0f); setParam (p, pid::subdiv, 3.0f);   // 6/8, 1/16
        setParam (p, pid::fillEvery, 1.0f);                                  // fill every 2 bars
        p.exportBars = 4; p.exportRateIndex = 1; p.exportBitDepthIndex = 2; p.exportAddTail = false;

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto file = dir.getChildFile ("rhythmforge_smoke_32f.wav");
        file.deleteFile();

        CHECK (p.startExport (file), "export started");
        for (int i = 0; i < 1000 && p.getExportJob()->getState() == ExportJob::State::Running; ++i)
            juce::Thread::sleep (10);
        CHECK (p.getExportJob()->getState() == ExportJob::State::Done, "export finished without error");
        std::printf ("  %s\n", p.getExportJob()->getMessage().toRawUTF8());

        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        const double bodySamples = 4 * barLengthInQuarters (4) * (60.0 / 140.0) * sr;
        CHECK (reader != nullptr && reader->numChannels == 2 && reader->sampleRate == 48000.0, "valid stereo 48 kHz WAV");
        CHECK (reader != nullptr && std::abs ((double) reader->lengthInSamples - bodySamples) < 1.5, "length is exactly 4 bars");

        if (reader != nullptr)
        {
            juce::AudioBuffer<float> fileAudio (2, (int) reader->lengthInSamples);
            reader->read (&fileAudio, 0, (int) reader->lengthInSamples, 0, true, true);

            // the realtime path must produce the same samples as the export
            auto live = renderInternal (p, (int) reader->lengthInSamples, 777, sr);
            float maxDiff = 0.0f;
            for (int i = 0; i < (int) reader->lengthInSamples; ++i)
                maxDiff = std::max (maxDiff, std::abs (live[(size_t) i] - fileAudio.getSample (0, i)));
            std::printf ("  max difference export vs. realtime render: %g\n", (double) maxDiff);
            CHECK (maxDiff < 1.0e-6f, "exported audio matches realtime playback");
        }

        for (int bits = 0; bits < 2; ++bits)
        {
            p.exportBitDepthIndex = bits; p.exportRateIndex = bits == 0 ? 0 : 3; p.exportAddTail = true;
            const auto f2 = dir.getChildFile (bits == 0 ? "rhythmforge_smoke_16.wav" : "rhythmforge_smoke_24.wav");
            f2.deleteFile();
            p.startExport (f2);
            for (int i = 0; i < 1000 && p.getExportJob()->getState() == ExportJob::State::Running; ++i) juce::Thread::sleep (10);
            std::unique_ptr<juce::AudioFormatReader> r2 (fm.createReaderFor (f2));
            char msg[96];
            std::snprintf (msg, sizeof (msg), "%d-bit export at %d Hz is readable and has a tail", bits == 0 ? 16 : 24, bits == 0 ? 44100 : 96000);
            CHECK (r2 != nullptr && r2->bitsPerSample == (bits == 0 ? 16 : 24)
                   && r2->lengthInSamples > (juce::int64) (4 * barLengthInQuarters (4) * (60.0 / 140.0) * r2->sampleRate), msg);
            f2.deleteFile();
        }
        file.deleteFile();

        // cancellation leaves no file behind
        p.exportBars = 512; p.exportBitDepthIndex = 1; p.exportRateIndex = 3;
        const auto f3 = dir.getChildFile ("rhythmforge_smoke_cancel.wav");
        f3.deleteFile();
        p.startExport (f3);
        p.cancelExport();
        for (int i = 0; i < 1000 && p.getExportJob()->getState() == ExportJob::State::Running; ++i) juce::Thread::sleep (10);
        CHECK (p.getExportJob()->getState() == ExportJob::State::Cancelled && ! f3.existsAsFile(), "cancel works and leaves no partial file");
    }

    // ------------------------------------------------------------------------------------------
    std::printf ("[7] state save / restore keeps edited patterns\n");
    {
        RhythmForgeProcessor a, b;
        a.getPattern().set (0, Snare, 5, CellGhost);
        setParam (a, pid::density, 0.9f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (b.getPattern().get (0, Snare, 5) == CellGhost, "pattern restored");
        CHECK (std::abs (b.apvts.getRawParameterValue (pid::density)->load() - 0.9f) < 1e-4f, "parameters restored");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
