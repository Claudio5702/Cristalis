#include "overlay.h"
#include "guest.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <cmath>
#include <map>

// ---- Fonts ----
// One bold sans-serif face for everything; a character it lacks (Japanese, the
// check mark) is taken from whichever installed font has it.

static FT_Library g_library;
static std::map<std::string, FT_Face> g_faces; // by file
static std::map<uint32_t, FT_Face> g_faceFor;  // by character, once looked up
static FT_Face g_mainFace;

static FT_Face OpenFace(const std::string& file)
{
    auto it = g_faces.find(file);
    if (it != g_faces.end())
        return it->second;
    FT_Face face = nullptr;
    if (FT_New_Face(g_library, file.c_str(), 0, &face) != 0)
        face = nullptr;
    return g_faces[file] = face;
}

// The file of the best installed font for a pattern, optionally one that has a
// given character.
static std::string FindFont(const char* family, uint32_t character)
{
    std::string file;
    FcPattern* pattern = FcNameParse((const FcChar8*)family);
    if (character != 0)
    {
        FcCharSet* set = FcCharSetCreate();
        FcCharSetAddChar(set, character);
        FcPatternAddCharSet(pattern, FC_CHARSET, set);
        FcCharSetDestroy(set);
    }
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    if (FcPattern* match = FcFontMatch(nullptr, pattern, &result))
    {
        FcChar8* path = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch)
            file = (const char*)path;
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return file;
}

static bool InitFonts()
{
    static bool tried, ok;
    if (tried)
        return ok;
    tried = true;
    if (FT_Init_FreeType(&g_library) != 0)
        return false;
    FcInit();
    g_mainFace = OpenFace(FindFont("sans:bold", 0));
    // Under WSL there may be no Linux font at all, but Windows' own are within reach.
    for (const char* fallback : { "/mnt/c/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
             "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" })
    {
        if (g_mainFace == nullptr)
            g_mainFace = OpenFace(fallback);
    }
    if (g_mainFace == nullptr)
        Log("Overlay: no font found, the menu will have no text");
    ok = g_mainFace != nullptr;
    return ok;
}

static FT_Face FaceFor(uint32_t character)
{
    if (FT_Get_Char_Index(g_mainFace, character) != 0)
        return g_mainFace;
    auto it = g_faceFor.find(character);
    if (it != g_faceFor.end())
        return it->second;

    FT_Face face = OpenFace(FindFont("sans", character));
    if (face == nullptr || FT_Get_Char_Index(face, character) == 0)
    {
        face = nullptr;
        for (const char* fallback : { "/mnt/c/Windows/Fonts/YuGothB.ttc", "/mnt/c/Windows/Fonts/msgothic.ttc", "/mnt/c/Windows/Fonts/seguisym.ttf" })
        {
            FT_Face candidate = OpenFace(fallback);
            if (face == nullptr && candidate != nullptr && FT_Get_Char_Index(candidate, character) != 0)
                face = candidate;
        }
    }
    return g_faceFor[character] = face ? face : g_mainFace;
}

// ---- Drawing ----

struct Canvas
{
    OverlayImage& image;

    void Blend(int x, int y, uint32_t color, float coverage)
    {
        if (x < 0 || y < 0 || x >= int(image.width) || y >= int(image.height) || coverage <= 0)
            return;
        uint32_t& pixel = image.pixels[size_t(y) * image.width + x];
        float a = std::min(coverage, 1.0f);
        float under = float(pixel >> 24) / 255.0f;
        float outAlpha = a + under * (1 - a);
        uint32_t out = uint32_t(outAlpha * 255.0f + 0.5f) << 24;
        for (int shift = 0; shift < 24; shift += 8)
        {
            float source = float((color >> shift) & 0xFF), target = float((pixel >> shift) & 0xFF);
            float value = outAlpha > 0 ? (source * a + target * under * (1 - a)) / outAlpha : 0;
            out |= uint32_t(value + 0.5f) << shift;
        }
        pixel = out;
    }

    // A rectangle with rounded corners, edges softened over one pixel.
    void RoundRect(float left, float top, float right, float bottom, float radius, uint32_t color)
    {
        for (int y = int(top); y < int(std::ceil(bottom)); y++)
        {
            for (int x = int(left); x < int(std::ceil(right)); x++)
            {
                float px = float(x) + 0.5f, py = float(y) + 0.5f;
                float dx = std::max({ left + radius - px, px - (right - radius), 0.0f });
                float dy = std::max({ top + radius - py, py - (bottom - radius), 0.0f });
                float inside = radius - std::sqrt(dx * dx + dy * dy) + 0.5f;
                float edge = std::min({ px - left, right - px, py - top, bottom - py }) + 0.5f;
                Blend(x, y, color, std::min(inside, edge));
            }
        }
    }

    void Circle(float cx, float cy, float radius, uint32_t color)
    {
        RoundRect(cx - radius, cy - radius, cx + radius, cy + radius, radius, color);
    }

    // Returns the width; with `draw` false only measures.
    int Text(const std::wstring& text, int x, int baseline, int size, uint32_t color, bool draw)
    {
        int pen = x;
        for (wchar_t character : text)
        {
            FT_Face face = FaceFor(uint32_t(character));
            FT_Set_Pixel_Sizes(face, 0, FT_UInt(size));
            if (FT_Load_Char(face, FT_ULong(character), draw ? FT_LOAD_RENDER : FT_LOAD_DEFAULT) != 0)
                continue;
            FT_GlyphSlot glyph = face->glyph;
            if (draw)
            {
                for (unsigned row = 0; row < glyph->bitmap.rows; row++)
                {
                    for (unsigned column = 0; column < glyph->bitmap.width; column++)
                    {
                        uint8_t coverage = glyph->bitmap.buffer[row * unsigned(glyph->bitmap.pitch) + column];
                        if (coverage != 0)
                            Blend(pen + glyph->bitmap_left + int(column), baseline - glyph->bitmap_top + int(row), color, coverage / 255.0f);
                    }
                }
            }
            pen += int(glyph->advance.x >> 6);
        }
        return pen - x;
    }

    // White text with the dark drop shadow the game gives its own.
    int Shadowed(const std::wstring& text, int x, int baseline, int size, uint32_t color, int offset)
    {
        Text(text, x + offset, baseline + offset, size, 0x0E283A, true);
        return Text(text, x, baseline, size, color, true);
    }
};

static uint32_t Rgb(int r, int g, int b)
{
    return uint32_t((r << 16) | (g << 8) | b);
}

OverlayImage OverlayPanel(const std::wstring& title, const std::wstring& message, const std::vector<OverlayItem>& items,
    int selected, const std::wstring& confirm, const std::wstring& back, float scale)
{
    OverlayImage image;
    if (!InitFonts())
        return image;
    Canvas canvas{ image };
    auto px = [scale](float value) { return int(value * scale + 0.5f); };

    const int width = px(540), margin = px(30), row = px(40), titleHeight = px(60);
    const int hintHeight = items.empty() ? px(14) : px(46);
    const int textSize = px(22);

    // Break the paragraph into lines that fit.
    std::vector<std::wstring> lines;
    {
        std::wstring line, word;
        auto flush = [&](bool force)
        {
            if (word.empty() && !force)
                return;
            std::wstring candidate = line.empty() ? word : line + L" " + word;
            if (!line.empty() && canvas.Text(candidate, 0, 0, textSize, 0, false) > width - margin * 2)
            {
                lines.push_back(line);
                line = word;
            }
            else
            {
                line = candidate;
            }
            word.clear();
        };
        for (wchar_t character : message)
        {
            if (character == L' ')
                flush(false);
            else if (character == L'\n')
            {
                flush(false);
                lines.push_back(line);
                line.clear();
            }
            else
                word += character;
        }
        flush(false);
        if (!line.empty())
            lines.push_back(line);
    }
    const int lineHeight = px(30);
    const int messageHeight = lines.empty() ? 0 : int(lines.size()) * lineHeight + px(14);

    image.width = uint32_t(width);
    image.height = uint32_t(titleHeight + messageHeight + row * int(items.size()) + px(8) + hintHeight);
    image.pixels.assign(size_t(image.width) * image.height, 0);
    const int height = int(image.height);

    // Dark rim, blue body, lighter inner line, like the game's dialogs.
    canvas.RoundRect(0, 0, float(width), float(height), float(px(14)), Rgb(12, 34, 52));
    canvas.RoundRect(float(px(4)), float(px(4)), float(width - px(4)), float(height - px(4)), float(px(11)), Rgb(96, 168, 200));
    canvas.RoundRect(float(px(5)), float(px(5)), float(width - px(5)), float(height - px(5)), float(px(10)), Rgb(42, 102, 140));

    int titleWidth = canvas.Text(title, 0, 0, px(27), 0, false);
    canvas.Shadowed(title, (width - titleWidth) / 2, px(42), px(27), Rgb(140, 236, 248), px(2));
    canvas.RoundRect(float(margin), float(titleHeight - px(6)), float(width - margin), float(titleHeight - px(4)), 0, Rgb(104, 176, 206));

    int y = titleHeight + px(4);
    for (auto& line : lines)
    {
        canvas.Shadowed(line, margin, y + px(22), textSize, Rgb(240, 244, 248), px(2));
        y += lineHeight;
    }
    if (!lines.empty())
        y += px(14);

    for (int i = 0; i < int(items.size()); i++, y += row)
    {
        bool chosen = i == selected;
        if (chosen)
        {
            canvas.RoundRect(float(margin / 2), float(y + px(2)), float(width - margin / 2), float(y + row - px(2)), float(px(6)), Rgb(130, 200, 228));
            canvas.RoundRect(float(margin / 2 + px(1)), float(y + px(3)), float(width - margin / 2 - px(1)), float(y + row - px(3)), float(px(5)), Rgb(60, 134, 176));
            canvas.Circle(float(margin + px(9)), float(y + row / 2), float(px(10)), Rgb(255, 228, 150));
            canvas.Circle(float(margin + px(9)), float(y + row / 2), float(px(8)), Rgb(255, 150, 40));
        }
        else
        {
            canvas.RoundRect(float(margin + px(5)), float(y + row / 2 - px(4)), float(margin + px(13)), float(y + row / 2 + px(4)), 0, Rgb(22, 56, 80));
        }
        int baseline = y + row / 2 + px(8);
        canvas.Shadowed(items[size_t(i)].label, margin + px(32), baseline, textSize, chosen ? Rgb(255, 255, 255) : Rgb(214, 230, 240), px(2));
        if (!items[size_t(i)].value.empty())
        {
            int valueWidth = canvas.Text(items[size_t(i)].value, 0, 0, textSize, 0, false);
            canvas.Shadowed(items[size_t(i)].value, width - margin - valueWidth, baseline, textSize, Rgb(255, 226, 150), px(2));
        }
    }

    // Button hints, as at the bottom of the game's screens.
    if (!items.empty())
    {
        const struct { const wchar_t* letter; const std::wstring& text; uint32_t color; } hints[] =
        {
            { L"B", back, Rgb(214, 60, 52) }, { L"A", confirm, Rgb(92, 190, 60) },
        };
        int x = width - margin;
        int centre = height - hintHeight + (hintHeight - px(10)) / 2;
        for (auto& hint : hints)
        {
            int textWidth = canvas.Text(hint.text, 0, 0, px(17), 0, false);
            canvas.Shadowed(hint.text, x - textWidth, centre + px(6), px(17), Rgb(236, 242, 246), px(1));
            x -= textWidth + px(6) + px(24);
            canvas.Circle(float(x + px(12)), float(centre), float(px(12)), Rgb(240, 246, 240));
            canvas.Circle(float(x + px(12)), float(centre), float(px(10)), hint.color);
            int letterWidth = canvas.Text(hint.letter, 0, 0, px(17), 0, false);
            canvas.Text(hint.letter, x + px(12) - letterWidth / 2, centre + px(6), px(17), Rgb(255, 255, 255), true);
            x -= px(18);
        }
    }
    return image;
}
