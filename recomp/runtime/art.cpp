// The project's own pictures for DarkLauncher (resources\art: painted with DarkLauncher's tools\art by
// the user's local ComfyUI, each with its provenance beside it; nothing taken from the game), built into
// the program and read with png.cpp when DarkLauncher first asks for them.
#include "art.h"
#include "png.h"

#include <map>
#include <mutex>

// The PNG files themselves, as bytes in the program (CD_ART_DIR: their folder, from CMake).
#define CD_ART(name, file)                                                         \
    asm(".pushsection .rodata\n.balign 4\n"                                        \
        ".global cd_art_" #name "\ncd_art_" #name ":\n.incbin \"" CD_ART_DIR "/" file "\"\n" \
        ".global cd_art_" #name "_end\ncd_art_" #name "_end:\n.byte 0\n.popsection\n"); \
    extern "C" const uint8_t cd_art_##name[], cd_art_##name##_end[];

CD_ART(backdrop, "backdrop.png")
CD_ART(starting, "starting.png")
CD_ART(E768, "navicons/E768.png")
CD_ART(E74E, "navicons/E74E.png")
CD_ART(E722, "navicons/E722.png")
CD_ART(E734, "navicons/E734.png")
CD_ART(E713, "navicons/E713.png")
CD_ART(E7E8, "navicons/E7E8.png")
CD_ART(E774, "navicons/E774.png")
CD_ART(E7F4, "navicons/E7F4.png")
CD_ART(E767, "navicons/E767.png")
CD_ART(E765, "navicons/E765.png")
CD_ART(E8B7, "navicons/E8B7.png")
CD_ART(E7A7, "navicons/E7A7.png")
CD_ART(E7FC, "navicons/E7FC.png")

namespace
{

struct Picture
{
    std::vector<uint32_t> pixels;
    int width = 0, height = 0;
};

// Decoded once, colours multiplied by their opacity as DarkLauncher wants them; kept while the program runs.
const uint32_t* Give(unsigned key, const uint8_t* begin, const uint8_t* end, int* w, int* h)
{
    static std::mutex mutex;
    static std::map<unsigned, Picture> pictures;
    std::lock_guard lock(mutex);
    auto found = pictures.find(key);
    if (found == pictures.end())
    {
        Picture picture;
        picture.pixels = DecodePng(begin, size_t(end - begin), picture.width, picture.height);
        for (uint32_t& p : picture.pixels)
        {
            uint32_t a = p >> 24;
            p = a << 24 | ((p >> 16 & 255) * a / 255) << 16 | ((p >> 8 & 255) * a / 255) << 8 | (p & 255) * a / 255;
        }
        found = pictures.emplace(key, std::move(picture)).first;
    }
    if (found->second.pixels.empty())
        return nullptr;
    *w = found->second.width;
    *h = found->second.height;
    return found->second.pixels.data();
}

} // namespace

const uint32_t* ArtBackdrop(int* w, int* h)
{
    return Give(0, cd_art_backdrop, cd_art_backdrop_end, w, h);
}

const uint32_t* ArtStarting(int* w, int* h)
{
    return Give(1, cd_art_starting, cd_art_starting_end, w, h);
}

const uint32_t* ArtIcon(unsigned icon, int* w, int* h)
{
    switch (icon)
    {
#define CD_ICON(code) case 0x##code: return Give(0x##code, cd_art_##code, cd_art_##code##_end, w, h);
    CD_ICON(E768) CD_ICON(E74E) CD_ICON(E722) CD_ICON(E734) CD_ICON(E713) CD_ICON(E7E8)
    CD_ICON(E774) CD_ICON(E7F4) CD_ICON(E767) CD_ICON(E765) CD_ICON(E8B7) CD_ICON(E7A7) CD_ICON(E7FC)
#undef CD_ICON
    default: return nullptr; // the others (Restart, the window's icon): DarkLauncher's own
    }
}
