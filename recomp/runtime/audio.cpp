// Audio. The game mixes with its statically linked XAudio2, which expects two
// pieces of console hardware:
//  - the XMA decoder: an array of 64-byte contexts in physical memory, driven
//    through kick/lock/clear registers. Each context names input buffers of
//    2 KB XMA packets and an output ring of 16-bit PCM blocks.
//  - the render driver: a periodic callback asking the mixer for a 256-sample
//    frame of 5.1 float audio, which it submits back for playback.
// XMA is decoded with FFmpeg; playback goes through waveOut.
#include "config.h"
#ifdef CD_DARKLAUNCHER
#include "dl_bridge.h"
#endif
#include "content.h"
#include "gpu.h"
#include "kernel_objects.h"
#include "renderer.h"
#include "xex_header.h"

#include <atomic>
#include <map>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <mmsystem.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
}

// ---- XMA decoder ----

constexpr uint32_t kXmaRegisterBase = 0x7FEA0000;
constexpr uint32_t kXmaContextArrayRegister = kXmaRegisterBase + 0x600 * 4;
constexpr uint32_t kXmaKickRegister = kXmaRegisterBase + 0x650 * 4;
constexpr uint32_t kXmaClearRegister = kXmaRegisterBase + 0x6A0 * 4;
constexpr uint32_t kXmaContextCount = 320;
constexpr uint32_t kXmaContextSize = 64;
constexpr uint32_t kXmaPacketSize = 2048;
constexpr uint32_t kXmaPacketBits = kXmaPacketSize * 8;
constexpr uint32_t kXmaBlockSize = 256; // output ring granularity, bytes

struct XmaState
{
    AVCodecContext* codec{};
    bool stereo{};
    std::vector<int16_t> pcm;      // decoded samples not yet delivered, interleaved
    size_t pcmRead{};
    uint32_t expectedReadOffset{}; // input position we left the context at
    bool positioned{};
    bool drained{};                // nothing left inside the codec
    bool bufferFinished{};         // current input buffer fully fed, output still pending
    size_t skipSamples{};          // decoded samples to discard (after a loop seek)

    // Music loop taken from the wave bank, in sample frames. Zero length: none.
    uint32_t loopStartFrame{};
    uint32_t loopLengthFrame{};
    uint32_t totalFrames{};
};

static std::mutex g_xmaMutex;
static uint32_t g_xmaArray; // guest virtual address
static bool g_xmaUsed[kXmaContextCount];
static XmaState g_xmaStates[kXmaContextCount];
static AVPacket* g_xmaPacket;
static AVFrame* g_xmaFrame;

// CD_STATS counters, reported by the render thread.
static std::atomic<uint32_t> g_statKicks;
static std::atomic<uint32_t> g_statPackets;
static std::atomic<uint32_t> g_statFrames;
static std::atomic<uint32_t> g_statSilentFrames;
static std::atomic<int> g_statPeak;
static std::atomic<bool> g_xmaIdleSeen; // a music stream has run out of input

static void XmaReset(XmaState& state)
{
    if (state.codec != nullptr)
        avcodec_flush_buffers(state.codec);
    state.pcm.clear();
    state.pcmRead = 0;
    state.positioned = false;
    state.drained = true;
    state.bufferFinished = false;
    state.skipSamples = 0;
    state.loopLengthFrame = 0;
}

static void XmaAppendFrames(XmaState& state)
{
    uint32_t channels = state.stereo ? 2 : 1;
    while (avcodec_receive_frame(state.codec, g_xmaFrame) == 0)
    {
        for (int i = 0; i < g_xmaFrame->nb_samples; i++)
        {
            for (uint32_t c = 0; c < channels; c++)
            {
                if (state.skipSamples > 0)
                {
                    state.skipSamples--;
                    continue;
                }
                float sample = ((const float*)g_xmaFrame->extended_data[c])[i] * 32767.0f;
                state.pcm.push_back(int16_t(sample > 32767.0f ? 32767.0f : sample < -32768.0f ? -32768.0f : sample));
            }
        }
        av_frame_unref(g_xmaFrame);
    }
}

// ---- Music loops ----
// WORKAROUND. The game submits each music track to its voice once, with no
// loop count, and on this runtime nothing ever resubmits it when it ends: the
// track plays once and stops. What makes it repeat on the console was not
// found. The loop region of every track is stored in the game's wave banks,
// so the decoder loops the track itself and never reports the end. This only
// applies to waves from the BGM_* banks, recognised by their first packet.

struct MusicWave
{
    uint32_t loopStartFrame;
    uint32_t loopLengthFrame;
    uint32_t totalFrames;
};

static uint64_t HashPacket(const uint8_t* data)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < kXmaPacketSize; i++)
        hash = (hash ^ data[i]) * 0x100000001B3ull;
    return hash;
}

static const std::map<uint64_t, MusicWave>& MusicWaves()
{
    static std::map<uint64_t, MusicWave> waves;
    static bool loaded = false;
    if (loaded)
        return waves;
    loaded = true;

    std::vector<uint8_t> file;
    if (FILE* in = _wfopen((GameRoot() / "Media" / "sound.bin").c_str(), L"rb"))
    {
        fseek(in, 0, SEEK_END);
        file.resize(size_t(ftell(in)));
        fseek(in, 0, SEEK_SET);
        if (fread(file.data(), 1, file.size(), in) != file.size())
            file.clear();
        fclose(in);
    }

    auto be32 = [&](size_t offset) -> uint32_t
    {
        if (offset + 4 > file.size())
            return 0;
        return (uint32_t(file[offset]) << 24) | (file[offset + 1] << 16) | (file[offset + 2] << 8) | file[offset + 3];
    };

    // XACT wave banks, stored big-endian: "WBND" reads "DNBW".
    for (size_t i = 0; i + 96 < file.size(); i++)
    {
        if (memcmp(&file[i], "DNBW", 4) != 0)
            continue;
        size_t bankData = i + be32(i + 12);
        if (bankData + 76 > file.size() || memcmp(&file[bankData + 8], "BGM", 3) != 0)
            continue;

        uint32_t entryCount = be32(bankData + 4);
        uint32_t entrySize = be32(bankData + 72);
        for (uint32_t e = 0; e < entryCount && entrySize >= 24; e++)
        {
            size_t entry = i + be32(i + 20) + e * entrySize;
            size_t data = i + be32(i + 44) + be32(entry + 8);
            if (data + kXmaPacketSize > file.size())
                continue;
            waves[HashPacket(&file[data])] = { be32(entry + 16), be32(entry + 20), be32(entry) >> 4 };
        }
    }
    Log("Audio: %zu music tracks with loop regions found in sound.bin", waves.size());
    return waves;
}

// Number of XMA frames that begin in a packet: the top six bits of its header.
static uint32_t PacketFrameCount(uint32_t packetAddress)
{
    return g_base[packetAddress] >> 2;
}

// Collects the samples the codec holds back until it knows the stream ended.
// The game's mixer counts samples and waits for every last one before looping.
static void XmaDrain(XmaState& state)
{
    avcodec_send_packet(state.codec, nullptr);
    XmaAppendFrames(state);
    avcodec_flush_buffers(state.codec);
    state.drained = true;
}

static bool XmaOpenCodec(XmaState& state, bool stereo)
{
    if (state.codec != nullptr && state.stereo == stereo)
        return true;
    if (state.codec != nullptr)
        avcodec_free_context(&state.codec);

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    if (codec == nullptr)
        return false;
    state.codec = avcodec_alloc_context3(codec);
    state.codec->sample_rate = 48000;
    av_channel_layout_default(&state.codec->ch_layout, stereo ? 2 : 1);
    state.codec->block_align = kXmaPacketSize;

    // XMA2WAVEFORMATEX extension, little-endian: a single stream.
    state.codec->extradata_size = 34;
    state.codec->extradata = (uint8_t*)av_mallocz(34 + AV_INPUT_BUFFER_PADDING_SIZE);
    state.codec->extradata[0] = 1;              // NumStreams
    state.codec->extradata[2] = stereo ? 3 : 4; // ChannelMask
    state.codec->extradata[31] = 4;             // EncoderVersion
    state.codec->extradata[32] = 1;             // BlockCount
    if (avcodec_open2(state.codec, codec, nullptr) < 0)
    {
        avcodec_free_context(&state.codec);
        return false;
    }
    state.stereo = stereo;
    XmaReset(state);
    return true;
}

// Feeds one 2 KB packet and appends whatever it decodes to the pending samples.
static void XmaDecodePacket(XmaState& state, uint32_t packetAddress)
{
    if (av_new_packet(g_xmaPacket, kXmaPacketSize) < 0)
        return;
    memcpy(g_xmaPacket->data, g_base + packetAddress, kXmaPacketSize);
    g_statPackets++;
    avcodec_send_packet(state.codec, g_xmaPacket);
    av_packet_unref(g_xmaPacket);
    state.drained = false;
    XmaAppendFrames(state);
}

// Runs the decoder for one context until its output ring is full or it has no
// more input, exactly as far as the hardware would on a kick.
static void XmaKick(uint32_t index)
{
    if (index >= kXmaContextCount || g_xmaArray == 0)
        return;
    uint32_t context = g_xmaArray + index * kXmaContextSize;
    XmaState& state = g_xmaStates[index];
    g_statKicks++;

    uint32_t word0 = Load32(context + 0);
    uint32_t word1 = Load32(context + 4);
    uint32_t word2 = Load32(context + 8);
    uint32_t word4 = Load32(context + 16);

    // Looping: positions are bit offsets into the current input buffer, and a
    // count of 255 means forever.
    uint32_t loopCount = (word0 >> 12) & 0xFF;
    uint32_t loopStart = Load32(context + 12) & 0x03FFFFFF;
    uint32_t loopEnd = word4 & 0x03FFFFFF;

    uint32_t packetCount[2] = { word0 & 0xFFF, word1 & 0xFFF };
    bool inputValid[2] = { bool((word0 >> 20) & 1), bool((word0 >> 21) & 1) };
    uint32_t inputAddress[2] = { Load32(context + 20), Load32(context + 24) };
    uint32_t blockCount = (word0 >> 22) & 0x1F;
    uint32_t writeBlock = (word0 >> 27) & 0x1F;
    bool stereo = (word1 >> 29) & 1;
    bool outputValid = word1 >> 31;
    uint32_t readOffset = word2 & 0x03FFFFFF;
    uint32_t current = word4 >> 31;
    uint32_t output = Load32(context + 28);
    uint32_t readBlock = Load32(context + 36) & 0x1F;

    // CD_STATS=1: describe each stereo stream (the music) the first time it runs.
    static const bool stats = getenv("CD_STATS") != nullptr;
    if (stats && stereo && state.positioned && !inputValid[0] && !inputValid[1] && !outputValid)
    {
        static uint32_t reported;
        if (reported++ < 3)
            Log("Audio: context %u kicked with no input and output not valid: %08X %08X %08X .. out read %08X", index, word0, word1,
                word2, Load32(context + 36));
    }

    if (!outputValid || blockCount == 0 || output == 0 || !XmaOpenCodec(state, stereo))
        return;

    if (stats && stereo && !state.positioned)
    {
        Log("Audio: context %u starts: %u+%u packets, read bit %u, loop count %u, loop bits %u..%u", index, packetCount[0],
            packetCount[1], readOffset, loopCount, loopStart, loopEnd);
    }
    output = kPhysicalHeapBase | (output & 0x1FFFFFFF);

    // The game moved the input position (new sound, seek, loop): start afresh.
    if (state.positioned && readOffset != state.expectedReadOffset)
        XmaReset(state);

    // The hardware outputs the 512-sample priming frame that opens every
    // stream; FFmpeg drops it. Sample positions in the game's wave banks (loop
    // points start at 512) count it, so put it back as silence.
    if (!state.positioned && readOffset <= 32)
    {
        state.pcm.insert(state.pcm.end(), 512 * (stereo ? 2 : 1), 0);

        // Is this one of the music tracks? (See "Music loops" above.)
        uint32_t first = inputValid[current] ? current : current ^ 1;
        if (inputValid[first] && inputAddress[first] != 0 && packetCount[first] != 0)
        {
            const auto& waves = MusicWaves();
            auto it = waves.find(HashPacket(g_base + (kPhysicalHeapBase | (inputAddress[first] & 0x1FFFFFFF))));
            if (it != waves.end())
            {
                state.loopStartFrame = it->second.loopStartFrame;
                state.loopLengthFrame = it->second.loopLengthFrame;
                state.totalFrames = it->second.totalFrames;
            }
        }
    }

    const uint32_t capacity = blockCount * kXmaBlockSize;
    const uint32_t unit = kXmaBlockSize << (stereo ? 1 : 0); // one 128-sample subframe
    uint32_t writeOffset = writeBlock * kXmaBlockSize;
    const uint32_t readPosition = readBlock * kXmaBlockSize;
    // With the output marked valid, equal offsets mean an empty ring.
    uint32_t space = writeOffset == readPosition ? capacity : (readPosition + capacity - writeOffset) % capacity;
    bool wrote = false;

    while (space >= unit)
    {
        size_t pendingBytes = (state.pcm.size() - state.pcmRead) * 2;
        if (pendingBytes >= unit)
        {
            uint32_t bytes = uint32_t(pendingBytes < space ? pendingBytes : space);
            bytes -= bytes % unit;
            for (uint32_t i = 0; i < bytes; i += 2)
            {
                Store16(output + writeOffset, uint16_t(state.pcm[state.pcmRead++]));
                writeOffset = (writeOffset + 2) % capacity;
            }
            space -= bytes;
            wrote = true;
            // Forget what has been delivered. A stream that never pauses (the
            // music) always keeps a few samples pending, so waiting for the
            // buffer to empty would let it grow for as long as the game runs.
            if (state.pcmRead == state.pcm.size())
            {
                state.pcm.clear();
                state.pcmRead = 0;
            }
            else if (state.pcmRead >= 16384)
            {
                state.pcm.erase(state.pcm.begin(), state.pcm.begin() + ptrdiff_t(state.pcmRead));
                state.pcmRead = 0;
            }
            continue;
        }

        // Need more input: take the next packet of the current buffer.
        if (!inputValid[current])
        {
            if (!inputValid[current ^ 1])
                break;
            current ^= 1;
            readOffset = 32;
            state.bufferFinished = false;
        }

        // Everything this buffer produced has now been delivered: only at this
        // point is it handed back. The game stops reading the decoder's state
        // once its input is no longer valid, so releasing the buffer any earlier
        // would strand the samples still waiting here.
        if (state.bufferFinished)
        {
            state.bufferFinished = false;
            inputValid[current] = false;
            current ^= 1;
            readOffset = 32;
            continue;
        }
        uint32_t packet = readOffset / kXmaPacketBits;
        bool looped = false;
        if (packet < packetCount[current] && inputAddress[current] != 0)
        {
            XmaDecodePacket(state, (kPhysicalHeapBase | (inputAddress[current] & 0x1FFFFFFF)) + packet * kXmaPacketSize);

            // Having played the packet holding the loop end, jump back to the
            // one holding the loop start. This is packet accurate, not sample
            // accurate: the seam can be off by a fraction of a packet.
            if (loopCount != 0 && loopEnd > loopStart && packet == loopEnd / kXmaPacketBits)
            {
                if (loopCount != 255)
                    loopCount--;
                avcodec_flush_buffers(state.codec);
                readOffset = (loopStart / kXmaPacketBits) * kXmaPacketBits + 32;
                looped = true;

                static std::atomic<uint32_t> reported;
                if (reported.fetch_add(1) < 8)
                    Log("Audio: context %u looped back (start bit %u, end bit %u, %u repeats left)", index, loopStart, loopEnd, loopCount);
            }
            packet++;
        }
        if (looped)
            continue;
        if (packet >= packetCount[current] && state.loopLengthFrame != 0 && !inputValid[current ^ 1])
        {
            // End of a music track: loop it here instead of ending the stream.
            XmaDrain(state);
            const uint32_t channels = stereo ? 2 : 1;
            const uint32_t buffer = kPhysicalHeapBase | (inputAddress[current] & 0x1FFFFFFF);

            // Drop what was decoded past the end of the loop region.
            uint32_t loopEndFrame = state.loopStartFrame + state.loopLengthFrame;
            if (state.totalFrames > loopEndFrame)
            {
                size_t excess = size_t(state.totalFrames - loopEndFrame) * channels;
                if (state.pcm.size() - state.pcmRead >= excess)
                    state.pcm.resize(state.pcm.size() - excess);
            }

            // After a flush the codec decodes the first frame it meets without
            // outputting it, so restarting at packet p resumes at sample
            // 512 * (frames before p + 1). Take the last packet that resumes at
            // or before the loop start, and discard the difference.
            uint32_t loopPacket = 0;
            uint32_t resumeFrame = 512;
            uint32_t framesBefore = 0;
            for (uint32_t p = 0; p < packetCount[current]; p++)
            {
                uint32_t resume = 512 * (framesBefore + 1);
                if (resume > state.loopStartFrame)
                    break;
                loopPacket = p;
                resumeFrame = resume;
                framesBefore += PacketFrameCount(buffer + p * kXmaPacketSize);
            }
            state.skipSamples = size_t(state.loopStartFrame - resumeFrame) * channels;
            readOffset = loopPacket * kXmaPacketBits + 32;

            static std::atomic<uint32_t> reported;
            if (stats && reported.fetch_add(1) < 6)
                Log("Audio: context %u music loop: back to sample %u (packet %u, skipping %zu samples)", index, state.loopStartFrame,
                    loopPacket, state.skipSamples / channels);
        }
        else if (packet >= packetCount[current])
        {
            // Last packet fed. If no other buffer follows, the stream ends here
            // and the codec must give up the samples it holds back. The read
            // position stays on this packet until its output has gone out.
            if (!inputValid[current ^ 1])
                XmaDrain(state);
            state.bufferFinished = true;
        }
        else
        {
            readOffset = packet * kXmaPacketBits + 32;
        }
    }

    // A ring written until it met the read position is full: the game marks
    // the output valid again once it has consumed some of it.
    writeBlock = writeOffset / kXmaBlockSize;
    if (wrote && writeBlock == readBlock)
        outputValid = false;

    word0 = (word0 & ~((0xFFu << 12) | (1u << 20) | (1u << 21) | (0x1Fu << 27))) | (loopCount << 12) |
        (uint32_t(inputValid[0]) << 20) | (uint32_t(inputValid[1]) << 21) | (writeBlock << 27);
    word1 = (word1 & 0x7FFFFFFF) | (uint32_t(outputValid) << 31);
    word2 = (word2 & ~0x03FFFFFFu) | (readOffset & 0x03FFFFFF);
    word4 = (word4 & 0x7FFFFFFF) | (current << 31);
    Store32(context + 0, word0);
    Store32(context + 4, word1);
    Store32(context + 8, word2);
    Store32(context + 16, word4);

    state.expectedReadOffset = readOffset;
    state.positioned = true;

    // CD_STATS=1: when a stereo stream has run out of input, show the context
    // as the game sees it, a few times.
    if (stats && stereo && !inputValid[0] && !inputValid[1])
    {
        g_xmaIdleSeen = true;
        static uint32_t idleKicks;
        uint32_t n = idleKicks++;
        if (n < 3 || n == 200 || n == 1000)
        {
            Log("Audio: context %u idle #%u: %08X %08X %08X %08X %08X in %08X %08X out %08X work %08X %08X; pending %zu samples",
                index, n, Load32(context), Load32(context + 4), Load32(context + 8), Load32(context + 12), Load32(context + 16),
                Load32(context + 20), Load32(context + 24), Load32(context + 28), Load32(context + 32), Load32(context + 36),
                state.pcm.size() - state.pcmRead);
        }
    }
}

static void XmaClear(uint32_t index)
{
    if (index >= kXmaContextCount || g_xmaArray == 0)
        return;
    uint32_t context = g_xmaArray + index * kXmaContextSize;
    XmaReset(g_xmaStates[index]);

    // Input and output buffers invalid, ring offsets back to zero.
    Store32(context + 0, Load32(context + 0) & ~((1u << 20) | (1u << 21) | (0x1Fu << 27)));
    Store32(context + 4, Load32(context + 4) & 0x7FFFFFFF);
    Store32(context + 36, Load32(context + 36) & ~0x1Fu);
}

template <typename Action>
static void ForEachContextBit(uint32_t registerAddress, uint32_t registerBase, uint32_t mask, Action action)
{
    std::lock_guard lock(g_xmaMutex);
    uint32_t first = ((registerAddress - registerBase) / 4) * 32;
    for (uint32_t bit = 0; bit < 32; bit++)
    {
        if (mask & (1u << bit))
            action(first + bit);
    }
}

// Called by the recompiled code right before the game stores to an XMA
// register (see [[midasm_hook]] in config/crystal_defenders.toml). r10 holds
// the register address, r11 the bit mask of contexts.
void XmaKickHook(PPCRegister& r10, PPCRegister& r11)
{
    ForEachContextBit(r10.u32, kXmaKickRegister, r11.u32, XmaKick);
}

void XmaClearHook(PPCRegister& r10, PPCRegister& r11)
{
    ForEachContextBit(r10.u32, kXmaClearRegister, r11.u32, XmaClear);
}

// Locking waits for the decoder to leave a context alone. Decoding happens
// synchronously inside the kick, so there is never anything to wait for.
void XmaLockHook(PPCRegister&, PPCRegister&)
{
}

GUEST_IMPORT(XMACreateContext)
{
    std::lock_guard lock(g_xmaMutex);
    for (uint32_t i = 0; i < kXmaContextCount; i++)
    {
        if (g_xmaUsed[i])
            continue;
        g_xmaUsed[i] = true;
        memset(g_base + g_xmaArray + i * kXmaContextSize, 0, kXmaContextSize);
        XmaReset(g_xmaStates[i]);
        Store32(ctx.r3.u32, g_xmaArray + i * kXmaContextSize);
        ctx.r3.u64 = 0;
        return;
    }
    ctx.r3.u64 = 0x8007000E; // E_OUTOFMEMORY
}

GUEST_IMPORT(XMAReleaseContext)
{
    std::lock_guard lock(g_xmaMutex);
    uint32_t address = kPhysicalHeapBase | (ctx.r3.u32 & 0x1FFFFFFF);
    if (address >= g_xmaArray && address < g_xmaArray + kXmaContextCount * kXmaContextSize)
    {
        uint32_t index = (address - g_xmaArray) / kXmaContextSize;
        g_xmaUsed[index] = false;
        XmaReset(g_xmaStates[index]);
    }
    ctx.r3.u64 = 0;
}

// ---- Render driver ----

constexpr uint32_t kFrameSamples = 256;
constexpr uint32_t kFramesPerBuffer = 4;
constexpr uint32_t kBufferCount = 8;

struct StereoFrame
{
    int16_t samples[kFrameSamples * 2];
};

static uint32_t g_renderCallback; // 0 while no driver is registered
static uint32_t g_renderCallbackArg;
static std::mutex g_renderCallbackMutex;
static std::mutex g_frameMutex;
static std::condition_variable g_frameSignal;
static std::deque<StereoFrame> g_frames;

static float GuestFloatAt(uint32_t address)
{
    uint32_t bits = Load32(address);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}

GUEST_IMPORT(XAudioSubmitRenderDriverFrame)
{
    // Six planar channels of 256 big-endian floats: FL, FR, FC, LFE, BL, BR.
    uint32_t samples = ctx.r4.u32;
    StereoFrame frame;
    for (uint32_t i = 0; i < kFrameSamples; i++)
    {
        float channel[6];
        for (uint32_t c = 0; c < 6; c++)
            channel[c] = GuestFloatAt(samples + (c * kFrameSamples + i) * 4);

        float left = channel[0] + 0.7071f * channel[2] + 0.7071f * channel[4] + 0.5f * channel[3];
        float right = channel[1] + 0.7071f * channel[2] + 0.7071f * channel[5] + 0.5f * channel[3];
        auto convert = [](float value)
        {
            value *= 32767.0f;
            return int16_t(value > 32767.0f ? 32767.0f : value < -32768.0f ? -32768.0f : value);
        };
        frame.samples[i * 2 + 0] = convert(left);
        frame.samples[i * 2 + 1] = convert(right);

        int magnitude = abs(int(frame.samples[i * 2])) > abs(int(frame.samples[i * 2 + 1]))
            ? abs(int(frame.samples[i * 2])) : abs(int(frame.samples[i * 2 + 1]));
        if (magnitude > g_statPeak.load(std::memory_order_relaxed))
            g_statPeak = magnitude;
    }
    g_statFrames++;

    {
        std::lock_guard lock(g_frameMutex);
        if (g_frames.size() < 32) // drop rather than fall behind
            g_frames.push_back(frame);
    }
    g_frameSignal.notify_one();
    ctx.r3.u64 = 0;
}

static void RenderThread()
{
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 48000 * 4;

    HWAVEOUT device = nullptr;
#ifdef CD_DARKLAUNCHER
    if (false) // DarkLauncher plays the sound (DlBridgeAudio below)
#else
    if (waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
#endif
    {
        Log("Audio: no output device, the mixer runs without sound");
        device = nullptr;
    }

    static int16_t storage[kBufferCount][kFramesPerBuffer * kFrameSamples * 2];
    WAVEHDR headers[kBufferCount]{};
    for (uint32_t i = 0; i < kBufferCount; i++)
    {
        headers[i].lpData = (LPSTR)storage[i];
        headers[i].dwBufferLength = sizeof(storage[i]);
        headers[i].dwFlags = WHDR_DONE;
        if (device != nullptr)
        {
            waveOutPrepareHeader(device, &headers[i], sizeof(WAVEHDR));
            headers[i].dwFlags |= WHDR_DONE;
        }
    }

    uint32_t next = 0;
    for (;;)
    {
        // The device clock paces the mixer: keep three buffers (64 ms) queued.
        uint32_t queued = 0;
        for (auto& header : headers)
            queued += !(header.dwFlags & WHDR_DONE);
        if (device != nullptr && (queued >= 3 || !(headers[next].dwFlags & WHDR_DONE)))
        {
            Sleep(2);
            continue;
        }

        for (uint32_t f = 0; f < kFramesPerBuffer; f++)
        {
            // Ask the mixer for a frame. It may answer from another thread.
            // Not once the game has unregistered its driver, as it does when
            // quitting: its mixer and buffers are gone by then.
            {
                std::lock_guard lock(g_renderCallbackMutex);
                if (g_renderCallback != 0)
                    GuestCall(g_renderCallback, g_renderCallbackArg, 0);
            }

            StereoFrame frame{};
            {
                std::unique_lock lock(g_frameMutex);
                g_frameSignal.wait_for(lock, std::chrono::milliseconds(20), [] { return !g_frames.empty(); });
                if (!g_frames.empty())
                {
                    frame = g_frames.front();
                    g_frames.pop_front();
                }
                else
                {
                    g_statSilentFrames++;
                }
            }
            memcpy(storage[next] + f * kFrameSamples * 2, frame.samples, sizeof(frame.samples));
        }

        // General volume, from crystal_defenders.ini or the settings menu.
        const int volume = GetConfig().volume;
        if (RendererSilent())
            memset(storage[next], 0, sizeof(storage[next]));
        else if (volume < 100)
        {
            for (int16_t& sample : storage[next])
                sample = int16_t(sample * volume / 100);
        }

        // CD_AUDIO_DUMP=<file>: also keep everything played, raw 48 kHz stereo 16-bit (tests).
        static FILE* dump = getenv("CD_AUDIO_DUMP") ? fopen(getenv("CD_AUDIO_DUMP"), "wb") : nullptr;
        if (dump != nullptr)
        {
            fwrite(storage[next], 1, sizeof(storage[next]), dump);
            fflush(dump);
        }

#ifdef CD_DARKLAUNCHER
        // Waits while enough sound is already waiting: DarkLauncher's playback sets the pace.
        DlBridgeAudio(storage[next], kFramesPerBuffer * kFrameSamples);
#else
        if (device != nullptr)
        {
            headers[next].dwFlags &= ~WHDR_DONE;
            waveOutWrite(device, &headers[next], sizeof(WAVEHDR));
        }
        else
        {
            Sleep(kFramesPerBuffer * kFrameSamples * 1000 / 48000);
        }
#endif
        next = (next + 1) % kBufferCount;

        // CD_STATS=1: one line roughly every five seconds.
        static const bool stats = getenv("CD_STATS") != nullptr;
        static uint32_t buffers;
        if (stats && ++buffers % 234 == 0)
        {
            Log("Audio: stats: %u kicks, %u XMA packets, %u mixer frames (%u missed), peak %d/32767",
                g_statKicks.exchange(0), g_statPackets.exchange(0), g_statFrames.exchange(0),
                g_statSilentFrames.exchange(0), g_statPeak.exchange(0));
        }
    }
}

GUEST_IMPORT(XAudioRegisterRenderDriverClient)
{
    // The callback receives a pointer to its argument, not the argument itself.
    static uint32_t argumentCell;
    if (argumentCell == 0)
        argumentCell = GuestAllocVirtual(0x1000, 0x1000);
    {
        std::lock_guard lock(g_renderCallbackMutex);
        Store32(argumentCell, Load32(ctx.r3.u32 + 4));
        g_renderCallback = Load32(ctx.r3.u32 + 0);
        g_renderCallbackArg = argumentCell;
    }
    Store32(ctx.r4.u32, 0x41550000);
    Log("Audio: render driver registered (callback %08X)", g_renderCallback);

    static bool started = false;
    if (!started)
    {
        started = true;
        std::thread(RenderThread).detach();
    }
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(XAudioUnregisterRenderDriverClient)
{
    // Waits for a callback in progress, then no more are made.
    std::lock_guard lock(g_renderCallbackMutex);
    g_renderCallback = 0;
    Log("Audio: render driver unregistered");
    ctx.r3.u64 = 0;
}

bool AudioInit()
{
    if (!GuestCommit(kXmaRegisterBase, 0x10000))
        return false;

    // The decoder warns about timestamps on every stream start; only errors matter.
    av_log_set_level(AV_LOG_ERROR);

    g_xmaArray = GuestAllocPhysical(kXmaContextCount * kXmaContextSize);
    g_xmaPacket = av_packet_alloc();
    g_xmaFrame = av_frame_alloc();
    if (g_xmaArray == 0 || g_xmaPacket == nullptr || g_xmaFrame == nullptr)
        return false;
    memset(g_base + g_xmaArray, 0, kXmaContextCount * kXmaContextSize);

    // The game reads the context array's physical address from this register,
    // little-endian.
    uint32_t physical = g_xmaArray & 0x1FFFFFFF;
    memcpy(g_base + kXmaContextArrayRegister, &physical, 4);
    return true;
}
