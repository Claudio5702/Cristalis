// Loads a XEX through XenonUtils, dumps the decrypted/decompressed image and
// locates the compiler helper routines XenonRecomp needs in its config.
// Usage: xex_dump <default.xex> <image.bin>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <file.h>
#include <image.h>

struct Pattern
{
    const char* key;
    uint8_t bytes[8];
};

// Byte patterns from the XenonRecomp README.
static const Pattern PATTERNS[] =
{
    { "restgprlr_14_address", { 0xE9, 0xC1, 0xFF, 0x68, 0xE9, 0xE1, 0xFF, 0x70 } },
    { "savegprlr_14_address", { 0xF9, 0xC1, 0xFF, 0x68, 0xF9, 0xE1, 0xFF, 0x70 } },
    { "restfpr_14_address",   { 0xC9, 0xCC, 0xFF, 0x70, 0xC9, 0xEC, 0xFF, 0x78 } },
    { "savefpr_14_address",   { 0xD9, 0xCC, 0xFF, 0x70, 0xD9, 0xEC, 0xFF, 0x78 } },
    { "restvmx_14_address",   { 0x39, 0x60, 0xFE, 0xE0, 0x7D, 0xCB, 0x60, 0xCE } },
    { "savevmx_14_address",   { 0x39, 0x60, 0xFE, 0xE0, 0x7D, 0xCB, 0x61, 0xCE } },
    { "restvmx_64_address",   { 0x39, 0x60, 0xFC, 0x00, 0x10, 0x0B, 0x60, 0xCB } },
    { "savevmx_64_address",   { 0x39, 0x60, 0xFC, 0x00, 0x10, 0x0B, 0x61, 0xCB } },
};

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: xex_dump <default.xex> <image.bin>\n");
        return 1;
    }

    const auto file = LoadFile(argv[1]);
    if (file.empty())
    {
        printf("ERROR: unable to read %s\n", argv[1]);
        return 1;
    }

    Image image = Image::ParseImage(file.data(), file.size());
    if (image.data == nullptr || image.size == 0)
    {
        printf("ERROR: unable to parse image\n");
        return 1;
    }

    printf("base = 0x%08zX\nsize = 0x%08X\nentry_point = 0x%08zX\n", image.base, image.size, image.entry_point);
    for (auto& section : image.sections)
    {
        printf("section %-10s base=0x%08zX size=0x%08X flags=0x%02X\n",
            section.name.c_str(), section.base, section.size, section.flags);
    }

    FILE* out = fopen(argv[2], "wb");
    if (out == nullptr)
    {
        printf("ERROR: unable to write %s\n", argv[2]);
        return 1;
    }
    fwrite(image.data.get(), 1, image.size, out);
    fclose(out);

    for (auto& pattern : PATTERNS)
    {
        int matches = 0;
        for (size_t i = 0; i + sizeof(pattern.bytes) <= image.size; i += 4)
        {
            if (memcmp(image.data.get() + i, pattern.bytes, sizeof(pattern.bytes)) == 0)
            {
                printf("%s = 0x%08zX\n", pattern.key, image.base + i);
                ++matches;
            }
        }
        if (matches != 1)
        {
            printf("# WARNING: %s matched %d times\n", pattern.key, matches);
        }
    }

    return 0;
}
