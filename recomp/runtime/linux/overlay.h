// Linux: pictures of the settings menu and of notices, drawn in software with
// FreeType into a pixel buffer the renderer shows over the game's image. The
// Windows build draws the same panels with GDI (renderer.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct OverlayItem
{
    std::wstring label;
    std::wstring value;
};

struct OverlayImage
{
    uint32_t width{}, height{};
    std::vector<uint32_t> pixels; // BGRA, top row first, alpha 0 outside the panel
};

// The panel: a title, an optional paragraph, a list with one line selected
// (-1: none), and the A / B hints when there is a list. `scale` is the size of
// one of the game's 720 lines in window pixels.
OverlayImage OverlayPanel(const std::wstring& title, const std::wstring& message, const std::vector<OverlayItem>& items,
    int selected, const std::wstring& confirm, const std::wstring& back, float scale);
