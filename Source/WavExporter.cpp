#include "WavExporter.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace rf
{
    ExportJob::ExportJob (const Pattern& sourcePattern, const Request& req)
        : juce::Thread ("RhythmForge WAV export"), request (req)
    {
        pattern.copyFrom (sourcePattern);
    }

    ExportJob::~ExportJob()
    {
        stopThread (15000);
    }

    void ExportJob::run()
    {
        // Render into a temporary file next to the target; only replace the target when we succeed.
        juce::TemporaryFile temp (request.file);

        const bool ok = writeWav (temp.getFile());

        if (threadShouldExit())
        {
            temp.getFile().deleteFile();
            message = "Export cancelled.";
            state = State::Cancelled;
            return;
        }

        if (ok && temp.overwriteTargetFileWithTemporary())
        {
            progress = 1.0f;
            state = State::Done;       // message was filled in by writeWav
            return;
        }

        temp.getFile().deleteFile();
        if (message.isEmpty())
            message = "Could not write " + request.file.getFullPathName();
        state = State::Failed;
    }

    bool ExportJob::writeWav (const juce::File& target)
    {
        const int bits = (request.bitDepth == 16 || request.bitDepth == 24 || request.bitDepth == 32) ? request.bitDepth : 24;
        const bool isFloat = (bits == 32);
        const int bytesPerSample = bits / 8;
        const int numChannels = 2;
        const double sr = request.sampleRate;

        std::unique_ptr<juce::FileOutputStream> out (target.createOutputStream());
        if (out == nullptr || ! out->openedOk())
        {
            message = "Cannot open the output file. Check the folder and permissions.";
            return false;
        }
        out->setPosition (0);
        out->truncate();

        // --- length of the render ---
        const double bpm = std::max (20.0, std::min (400.0, request.bpm));
        const double ppqPerSample = bpm / (60.0 * sr);
        const int ts = request.engineSettings.timeSig;
        const double bodyQuarters = (double) std::max (1, request.bars) * barLengthInQuarters (ts);
        const long long bodySamples = (long long) std::llround (bodyQuarters / ppqPerSample);
        const long long tailSamples = request.addTail ? (long long) std::llround (1.0 * sr) : 0;
        const long long totalSamples = bodySamples + tailSamples;

        // --- header (sizes are patched at the end) ---
        out->write ("RIFF", 4);
        out->writeInt (0);
        out->write ("WAVE", 4);
        out->write ("fmt ", 4);
        out->writeInt (16);
        out->writeShort (isFloat ? (short) 3 : (short) 1);
        out->writeShort ((short) numChannels);
        out->writeInt ((int) std::llround (sr));
        out->writeInt ((int) std::llround (sr) * numChannels * bytesPerSample);
        out->writeShort ((short) (numChannels * bytesPerSample));
        out->writeShort ((short) bits);

        juce::int64 factFramesPos = -1;
        if (isFloat)
        {
            out->write ("fact", 4);
            out->writeInt (4);
            factFramesPos = out->getPosition();
            out->writeInt (0);
        }

        out->write ("data", 4);
        const juce::int64 dataSizePos = out->getPosition();
        out->writeInt (0);
        const juce::int64 dataStart = out->getPosition();

        // --- render ---
        RhythmEngine engine;
        engine.prepare (sr);
        engine.reset();

        constexpr int blockSize = 1024;
        std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize);
        std::vector<unsigned char> bytes ((size_t) blockSize * (size_t) numChannels * (size_t) bytesPerSample);

        uint32_t ditherState = 0x2545F491u;
        auto rnd01 = [&ditherState]() -> float
        {
            ditherState ^= ditherState << 13; ditherState ^= ditherState >> 17; ditherState ^= ditherState << 5;
            return (float) (ditherState >> 8) * (1.0f / 16777216.0f);
        };

        float peak = 0.0f;

        for (long long pos = 0; pos < totalSamples; )
        {
            if (threadShouldExit())
                return false;

            const int n = (int) std::min<long long> (blockSize, totalSamples - pos);

            RhythmEngine::BlockTiming timing;
            timing.ppqPerSample = ppqPerSample;
            timing.ppqStart     = (double) pos * ppqPerSample;
            timing.trigFrom     = (double) pos * ppqPerSample;
            timing.trigTo       = std::min ((double) (pos + n) * ppqPerSample, bodyQuarters);

            engine.renderBlock (pattern, request.engineSettings, timing, left.data(), right.data(), n);

            unsigned char* p = bytes.data();
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float raw = ch == 0 ? left[(size_t) i] : right[(size_t) i];
                    const float x = std::max (-1.0f, std::min (1.0f, raw));
                    peak = std::max (peak, std::abs (x));

                    if (bits == 16)
                    {
                        const float dithered = x * 32767.0f + (rnd01() - rnd01());   // TPDF dither
                        const int v = std::max (-32768, std::min (32767, (int) std::lrintf (dithered)));
                        p[0] = (unsigned char) (v & 0xFF);
                        p[1] = (unsigned char) ((v >> 8) & 0xFF);
                        p += 2;
                    }
                    else if (bits == 24)
                    {
                        const int v = std::max (-8388608, std::min (8388607, (int) std::lrintf (x * 8388607.0f)));
                        p[0] = (unsigned char) (v & 0xFF);
                        p[1] = (unsigned char) ((v >> 8) & 0xFF);
                        p[2] = (unsigned char) ((v >> 16) & 0xFF);
                        p += 3;
                    }
                    else
                    {
                        // IEEE float, little endian (all platforms JUCE supports for VST3 are little endian)
                        std::memcpy (p, &x, 4);
                        p += 4;
                    }
                }
            }

            if (! out->write (bytes.data(), (size_t) (p - bytes.data())))
            {
                message = "Writing failed (disk full or file not writable).";
                return false;
            }

            pos += n;
            progress = (float) ((double) pos / (double) totalSamples);
        }

        // --- patch header sizes ---
        const juce::int64 dataBytes = out->getPosition() - dataStart;
        const juce::int64 fileEnd   = out->getPosition();

        out->setPosition (4);
        out->writeInt ((int) (fileEnd - 8));
        if (factFramesPos >= 0)
        {
            out->setPosition (factFramesPos);
            out->writeInt ((int) totalSamples);
        }
        out->setPosition (dataSizePos);
        out->writeInt ((int) dataBytes);
        out->flush();

        if (out->getStatus().failed())
        {
            message = "Writing failed: " + out->getStatus().getErrorMessage();
            return false;
        }

        out.reset();

        const double seconds = (double) totalSamples / sr;
        const float peakDb = peak > 0.0f ? 20.0f * std::log10 (peak) : -120.0f;
        message = "Saved " + juce::String (request.bars) + " bar(s), " + juce::String (seconds, 2) + " s, "
                  + juce::String ((int) std::llround (sr)) + " Hz / " + juce::String (bits) + "-bit"
                  + (isFloat ? " float" : "") + ", peak " + juce::String (peakDb, 1) + " dBFS";
        return true;
    }
}
