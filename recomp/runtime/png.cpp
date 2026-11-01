// A small PNG reader: the game keeps its achievements' pictures as PNG files in its title data, and the
// program has no picture library on every system (Windows and Linux alike), so it reads them itself.
// Only what those pictures need: 8 bits per sample, not interlaced; any other file gives nothing.
#include "png.h"

#include <cstring>

namespace
{

// ---- Inflate (RFC 1951) ----

struct Bits
{
    const uint8_t* data;
    size_t size;
    size_t at = 0; // in bits
    bool over = false;

    uint32_t Get(int count)
    {
        uint32_t value = 0;
        for (int i = 0; i < count; i++, at++)
        {
            if (at / 8 >= size)
            {
                over = true;
                return 0;
            }
            value |= uint32_t((data[at / 8] >> (at % 8)) & 1) << i;
        }
        return value;
    }
};

// A canonical Huffman code: how many codes of each length, and the symbols in code order.
struct Huffman
{
    uint16_t counts[16]{};
    uint16_t symbols[320]{};

    void Build(const uint8_t* lengths, int n)
    {
        memset(counts, 0, sizeof(counts));
        for (int i = 0; i < n; i++)
            counts[lengths[i]]++;
        counts[0] = 0;
        uint16_t offsets[16]{};
        for (int i = 1; i < 16; i++)
            offsets[i] = uint16_t(offsets[i - 1] + counts[i - 1]);
        for (int i = 0; i < n; i++)
            if (lengths[i] != 0)
                symbols[offsets[lengths[i]]++] = uint16_t(i);
    }

    int Decode(Bits& bits) const
    {
        int code = 0, first = 0, index = 0;
        for (int length = 1; length < 16; length++)
        {
            code |= int(bits.Get(1));
            int count = counts[length];
            if (code - first < count)
                return symbols[index + code - first];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
            if (bits.over)
                break;
        }
        return -1;
    }
};

const uint16_t kLengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t kDistanceBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const uint8_t kDistanceExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool InflateBlock(Bits& bits, std::vector<uint8_t>& out, const Huffman& literals, const Huffman& distances)
{
    for (;;)
    {
        int symbol = literals.Decode(bits);
        if (symbol < 0 || bits.over)
            return false;
        if (symbol < 256)
            out.push_back(uint8_t(symbol));
        else if (symbol == 256)
            return true;
        else
        {
            symbol -= 257;
            if (symbol >= 29)
                return false;
            size_t length = kLengthBase[symbol] + bits.Get(kLengthExtra[symbol]);
            int code = distances.Decode(bits);
            if (code < 0 || code >= 30)
                return false;
            size_t distance = kDistanceBase[code] + bits.Get(kDistanceExtra[code]);
            if (distance > out.size() || bits.over)
                return false;
            size_t from = out.size() - distance;
            for (size_t i = 0; i < length; i++)
                out.push_back(out[from + i]);
        }
    }
}

// The data after the zlib header (two bytes); false when it is damaged.
bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
    if (size < 2 || (data[0] & 0x0F) != 8)
        return false;
    Bits bits{ data + 2, size - 2 };
    for (bool last = false; !last;)
    {
        last = bits.Get(1) != 0;
        uint32_t type = bits.Get(2);
        if (type == 0)
        {
            bits.at = (bits.at + 7) / 8 * 8;
            uint32_t length = bits.Get(16);
            bits.Get(16); // its complement
            if (bits.at / 8 + length > bits.size)
                return false;
            out.insert(out.end(), bits.data + bits.at / 8, bits.data + bits.at / 8 + length);
            bits.at += size_t(length) * 8;
        }
        else if (type == 1)
        {
            uint8_t lengths[288];
            for (int i = 0; i < 288; i++)
                lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            Huffman literals, distances;
            literals.Build(lengths, 288);
            uint8_t five[30];
            memset(five, 5, sizeof(five));
            distances.Build(five, 30);
            if (!InflateBlock(bits, out, literals, distances))
                return false;
        }
        else if (type == 2)
        {
            int literalCount = int(bits.Get(5)) + 257;
            int distanceCount = int(bits.Get(5)) + 1;
            int codeCount = int(bits.Get(4)) + 4;
            static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            uint8_t codeLengths[19]{};
            for (int i = 0; i < codeCount; i++)
                codeLengths[order[i]] = uint8_t(bits.Get(3));
            Huffman codes;
            codes.Build(codeLengths, 19);
            uint8_t lengths[320]{};
            for (int i = 0; i < literalCount + distanceCount;)
            {
                int symbol = codes.Decode(bits);
                if (symbol < 0 || bits.over)
                    return false;
                if (symbol < 16)
                {
                    lengths[i++] = uint8_t(symbol);
                    continue;
                }
                uint8_t repeated = 0;
                int count;
                if (symbol == 16)
                {
                    if (i == 0)
                        return false;
                    repeated = lengths[i - 1];
                    count = 3 + int(bits.Get(2));
                }
                else
                    count = symbol == 17 ? 3 + int(bits.Get(3)) : 11 + int(bits.Get(7));
                if (i + count > literalCount + distanceCount)
                    return false;
                while (count-- > 0)
                    lengths[i++] = repeated;
            }
            Huffman literals, distances;
            literals.Build(lengths, literalCount);
            distances.Build(lengths + literalCount, distanceCount);
            if (!InflateBlock(bits, out, literals, distances))
                return false;
        }
        else
            return false;
        if (bits.over)
            return false;
    }
    return true;
}

uint32_t Big32(const uint8_t* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

} // namespace

std::vector<uint32_t> DecodePng(const uint8_t* data, size_t size, int& width, int& height)
{
    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (size < 8 || memcmp(data, signature, 8) != 0)
        return {};
    uint32_t w = 0, h = 0;
    int colour = -1;
    std::vector<uint8_t> packed;
    uint32_t palette[256]{};
    for (size_t at = 8; at + 12 <= size;)
    {
        uint32_t length = Big32(data + at);
        const uint8_t* type = data + at + 4;
        const uint8_t* body = data + at + 8;
        if (length > size - at - 12)
            return {};
        if (memcmp(type, "IHDR", 4) == 0 && length >= 13)
        {
            w = Big32(body);
            h = Big32(body + 4);
            // 8 bits per sample, deflate, standard filters, not interlaced
            if (body[8] != 8 || body[10] != 0 || body[11] != 0 || body[12] != 0)
                return {};
            colour = body[9];
        }
        else if (memcmp(type, "PLTE", 4) == 0)
        {
            for (uint32_t i = 0; i < length / 3 && i < 256; i++)
                palette[i] = 0xFF000000u | uint32_t(body[i * 3]) << 16 | uint32_t(body[i * 3 + 1]) << 8 | body[i * 3 + 2];
        }
        else if (memcmp(type, "tRNS", 4) == 0 && colour == 3)
        {
            for (uint32_t i = 0; i < length && i < 256; i++)
                palette[i] = (palette[i] & 0x00FFFFFF) | uint32_t(body[i]) << 24;
        }
        else if (memcmp(type, "IDAT", 4) == 0)
            packed.insert(packed.end(), body, body + length);
        else if (memcmp(type, "IEND", 4) == 0)
            break;
        at += 12 + size_t(length);
    }
    int channels = colour == 0 ? 1 : colour == 2 ? 3 : colour == 3 ? 1 : colour == 4 ? 2 : colour == 6 ? 4 : 0;
    if (channels == 0 || w == 0 || h == 0 || w > 4096 || h > 4096)
        return {};

    std::vector<uint8_t> raw;
    size_t stride = size_t(w) * size_t(channels);
    if (!Inflate(packed.data(), packed.size(), raw) || raw.size() < (stride + 1) * h)
        return {};

    // Undo each row's filter, in place
    std::vector<uint8_t> rows(stride * h);
    for (uint32_t y = 0; y < h; y++)
    {
        uint8_t filter = raw[y * (stride + 1)];
        const uint8_t* in = &raw[y * (stride + 1) + 1];
        uint8_t* row = &rows[y * stride];
        const uint8_t* up = y > 0 ? row - stride : nullptr;
        for (size_t x = 0; x < stride; x++)
        {
            int a = x >= size_t(channels) ? row[x - size_t(channels)] : 0;
            int b = up ? up[x] : 0;
            int c = up && x >= size_t(channels) ? up[x - size_t(channels)] : 0;
            int predicted = 0;
            switch (filter)
            {
            case 1: predicted = a; break;
            case 2: predicted = b; break;
            case 3: predicted = (a + b) / 2; break;
            case 4:
            {
                int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
                predicted = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                break;
            }
            default: break;
            }
            row[x] = uint8_t(in[x] + predicted);
        }
    }

    std::vector<uint32_t> pixels(size_t(w) * h);
    for (size_t i = 0; i < pixels.size(); i++)
    {
        const uint8_t* p = &rows[i * size_t(channels)];
        switch (colour)
        {
        case 0: pixels[i] = 0xFF000000u | p[0] * 0x010101u; break;
        case 2: pixels[i] = 0xFF000000u | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]; break;
        case 3: pixels[i] = palette[p[0]]; break;
        case 4: pixels[i] = uint32_t(p[1]) << 24 | p[0] * 0x010101u; break;
        default: pixels[i] = uint32_t(p[3]) << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]; break;
        }
    }
    width = int(w);
    height = int(h);
    return pixels;
}
