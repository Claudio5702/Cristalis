// The project's own pictures for DarkLauncher's theme and start (see art.cpp): w x h pixels 0xAARRGGBB,
// colours multiplied by their opacity, valid while the program runs; NULL when there is none.
#pragma once

#include <cstdint>

const uint32_t* ArtBackdrop(int* w, int* h);              // DlTheme.backdrop: the land, its sky clear
const uint32_t* ArtStarting(int* w, int* h);              // DlGame.starting_picture: the three defenders
const uint32_t* ArtIcon(unsigned icon, int* w, int* h);   // DlTheme.icon: the navigation bar's icons
