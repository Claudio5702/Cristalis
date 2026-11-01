// A small PNG reader, for the pictures inside the player's copy of the game. See png.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// The picture's pixels as 0xAARRGGBB with straight alpha, row by row, and its size; empty when the data
// is not a PNG this reader knows (8 bits per sample, grey, RGB, palette or RGBA, not interlaced).
std::vector<uint32_t> DecodePng(const uint8_t* data, size_t size, int& width, int& height);
