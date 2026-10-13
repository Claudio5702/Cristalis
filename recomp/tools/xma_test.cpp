// Offline check of the XMA decoding path: extracts one wave from an XACT wave
// bank inside sound.bin, decodes it with FFmpeg's XMA2 decoder and writes a WAV.
// Usage: xma_test <sound.bin> <bank name> <wave index> <out.wav>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
}

static uint32_t Be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

int main(int argc, char* argv[])
{
    if (argc < 5)
    {
        printf("Usage: xma_test <sound.bin> <bank name> <wave index> <out.wav>\n");
        return 1;
    }

    std::vector<uint8_t> file;
    if (FILE* in = fopen(argv[1], "rb"))
    {
        fseek(in, 0, SEEK_END);
        file.resize(ftell(in));
        fseek(in, 0, SEEK_SET);
        fread(file.data(), 1, file.size(), in);
        fclose(in);
    }

    // Find the wave bank ("WBND" stored big-endian) with the requested name.
    const uint8_t* wave = nullptr;
    uint32_t waveSize = 0, channels = 0, sampleRate = 0;
    for (size_t i = 0; i + 4 < file.size(); i++)
    {
        if (memcmp(&file[i], "DNBW", 4) != 0)
            continue;
        const uint8_t* bank = &file[i];
        const uint8_t* bankData = bank + Be32(bank + 12);
        if (std::string((const char*)bankData + 8) != argv[2])
            continue;

        uint32_t entrySize = Be32(bankData + 72);
        const uint8_t* entry = bank + Be32(bank + 20) + atoi(argv[3]) * entrySize;
        uint32_t format = Be32(entry + 4);
        channels = (format >> 2) & 7;
        sampleRate = (format >> 5) & 0x3FFFF;
        wave = bank + Be32(bank + 44) + Be32(entry + 8);
        waveSize = Be32(entry + 12);
        printf("bank %s wave %s: tag %u, %u channels, %u Hz, %u bytes (%u packets)\n", argv[2], argv[3], format & 3,
            channels, sampleRate, waveSize, waveSize / 2048);
        break;
    }
    if (wave == nullptr)
    {
        printf("wave not found\n");
        return 1;
    }

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    AVCodecContext* context = avcodec_alloc_context3(codec);
    context->sample_rate = 48000;
    av_channel_layout_default(&context->ch_layout, int(channels));
    context->block_align = 2048;

    // XMA2WAVEFORMATEX extension (34 bytes, little-endian): one stream.
    context->extradata_size = 34;
    context->extradata = (uint8_t*)av_mallocz(34 + AV_INPUT_BUFFER_PADDING_SIZE);
    context->extradata[0] = 1;                       // NumStreams
    context->extradata[2] = channels == 2 ? 3 : 4;   // ChannelMask
    context->extradata[31] = 4;                      // EncoderVersion
    context->extradata[32] = 1;                      // BlockCount

    int result = avcodec_open2(context, codec, nullptr);
    if (result < 0)
    {
        printf("avcodec_open2 failed: %d\n", result);
        return 1;
    }

    std::vector<int16_t> pcm;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int errors = 0;
    double energy = 0;
    for (uint32_t offset = 0; offset + 2048 <= waveSize; offset += 2048)
    {
        av_new_packet(packet, 2048);
        memcpy(packet->data, wave + offset, 2048);
        if (avcodec_send_packet(context, packet) < 0)
            errors++;
        av_packet_unref(packet);

        while (avcodec_receive_frame(context, frame) == 0)
        {
            for (int i = 0; i < frame->nb_samples; i++)
            {
                for (uint32_t c = 0; c < channels; c++)
                {
                    float sample = ((const float*)frame->extended_data[c])[i];
                    energy += double(sample) * sample;
                    float scaled = sample * 32767.0f;
                    pcm.push_back(int16_t(scaled > 32767 ? 32767 : scaled < -32768 ? -32768 : scaled));
                }
            }
            av_frame_unref(frame);
        }
    }

    // Drain what the decoder still holds at the end of the stream.
    size_t beforeDrain = pcm.size() / channels;
    avcodec_send_packet(context, nullptr);
    while (avcodec_receive_frame(context, frame) == 0)
    {
        for (int i = 0; i < frame->nb_samples; i++)
        {
            for (uint32_t c = 0; c < channels; c++)
            {
                float scaled = ((const float*)frame->extended_data[c])[i] * 32767.0f;
                pcm.push_back(int16_t(scaled > 32767 ? 32767 : scaled < -32768 ? -32768 : scaled));
            }
        }
        av_frame_unref(frame);
    }
    printf("drain added %zu sample frames\n", pcm.size() / channels - beforeDrain);

    // Optional fifth argument: a packet index. Decode again starting there, as
    // the runtime does when it loops, and find where that output lines up with
    // the full decode. Reports the sample frame the restarted stream begins at.
    if (argc > 5)
    {
        uint32_t startPacket = uint32_t(atoi(argv[5]));
        uint32_t framesBefore = 0;
        for (uint32_t p = 0; p < startPacket; p++)
            framesBefore += wave[p * 2048] >> 2;

        avcodec_flush_buffers(context);
        std::vector<int16_t> restarted;
        for (uint32_t offset = startPacket * 2048; offset + 2048 <= waveSize && restarted.size() < 40000 * channels; offset += 2048)
        {
            av_new_packet(packet, 2048);
            memcpy(packet->data, wave + offset, 2048);
            avcodec_send_packet(context, packet);
            av_packet_unref(packet);
            while (avcodec_receive_frame(context, frame) == 0)
            {
                for (int i = 0; i < frame->nb_samples; i++)
                {
                    for (uint32_t c = 0; c < channels; c++)
                    {
                        float scaled = ((const float*)frame->extended_data[c])[i] * 32767.0f;
                        restarted.push_back(int16_t(scaled > 32767 ? 32767 : scaled < -32768 ? -32768 : scaled));
                    }
                }
                av_frame_unref(frame);
            }
        }

        // Compare a window well past the restart transient.
        const long long window = 8000, lead = 4000;
        long long expected = 512ll * framesBefore; // index in the full decode, which lacks the priming frame
        long long bestOffset = -1;
        double bestError = 1e30;
        for (long long d = expected - 8000 < 0 ? 0 : expected - 8000; d <= expected + 8000; d++)
        {
            double error = 0;
            for (long long k = lead; k < lead + window; k += 3)
            {
                size_t a = size_t(d + k) * channels, b = size_t(k) * channels;
                if (a >= pcm.size() || b >= restarted.size())
                {
                    error = 1e30;
                    break;
                }
                error += fabs(double(pcm[a]) - double(restarted[b]));
            }
            if (error < bestError)
            {
                bestError = error;
                bestOffset = d;
            }
        }
        printf("restart at packet %u: %u frames begin before it; restarted output lines up with full-decode frame %lld "
               "(wave sample %lld), mean error %.1f; formula 512*(frames+1) predicts wave sample %u\n",
            startPacket, framesBefore, bestOffset, bestOffset + 512, bestError / (window / 3.0), 512 * (framesBefore + 1));
    }

    size_t frames = pcm.size() / channels;
    printf("decoded %zu sample frames (%.2f s), %d packet errors, RMS %.4f, format %s\n", frames, frames / 48000.0, errors,
        pcm.empty() ? 0.0 : sqrt(energy / pcm.size()), av_get_sample_fmt_name(context->sample_fmt));

    if (FILE* out = fopen(argv[4], "wb"))
    {
        uint32_t dataSize = uint32_t(pcm.size() * 2);
        uint32_t riffSize = 36 + dataSize, fmtSize = 16, rate = 48000, byteRate = 48000 * channels * 2;
        uint16_t pcmTag = 1, channelCount = uint16_t(channels), blockAlign = uint16_t(channels * 2), bits = 16;
        fwrite("RIFF", 1, 4, out); fwrite(&riffSize, 4, 1, out); fwrite("WAVEfmt ", 1, 8, out);
        fwrite(&fmtSize, 4, 1, out); fwrite(&pcmTag, 2, 1, out); fwrite(&channelCount, 2, 1, out);
        fwrite(&rate, 4, 1, out); fwrite(&byteRate, 4, 1, out); fwrite(&blockAlign, 2, 1, out); fwrite(&bits, 2, 1, out);
        fwrite("data", 1, 4, out); fwrite(&dataSize, 4, 1, out);
        fwrite(pcm.data(), 2, pcm.size(), out);
        fclose(out);
    }
    return 0;
}
