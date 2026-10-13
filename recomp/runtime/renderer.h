// Host renderer: a window and a Direct3D 11 device drawing what the GPU command
// processor decodes. Every call must come from the command processor thread.
#pragma once

#include <cstdint>
#include <vector>

struct HostVertex
{
    float x, y, z, w;   // clip space, already transformed on the CPU
    float u, v;
    float r, g, b, a;
};

struct HostTexture
{
    bool present{};
    uint32_t address{};  // guest virtual address of the texel data
    uint32_t width{};
    uint32_t height{};
    uint32_t pitch{};    // in texels
    uint32_t format{};   // Xenos texture format
    bool tiled{};
    bool linear{};
};

struct HostBlend
{
    bool enabled{};
    uint32_t control{};   // RB_BLENDCONTROL value
    uint32_t writeMask{}; // RGBA bits
};

// Depth test: the game masks sprites with invisible rectangles that only write
// depth, for instance to hide whatever sticks out of the map.
struct HostDepth
{
    bool test{};
    bool write{};
    uint32_t function{}; // Xenos compare function: 0 never .. 3 less or equal .. 7 always
};

struct HostDraw
{
    std::vector<HostVertex> vertices; // triangle list
    HostTexture texture;
    HostBlend blend;
    HostDepth depth;
    // Face culling as the game asks for it: bit 0 hides front faces, bit 1 back
    // faces, bit 2 says clockwise triangles are the front ones. 0 = show all.
    uint32_t cull{};
    int scissor[4]{};                 // left, top, right, bottom in pixels
};

constexpr uint32_t kHostWidth = 1280;
constexpr uint32_t kHostHeight = 720;

bool RendererInit();
void RendererDraw(const HostDraw& draw);
// Colour the frame is cleared to after each present.
void RendererSetClearColor(float r, float g, float b, float a);
void RendererPresent();

// State of player one's pad: the first XInput controller if one is connected,
// merged with the keyboard bindings. Buttons use XINPUT_GAMEPAD bits.
struct HostPad
{
    uint16_t buttons{};
    uint8_t leftTrigger{};
    uint8_t rightTrigger{};
    int16_t leftX{}, leftY{}, rightX{}, rightY{};
};
// True while the opening logos are being skipped after a relaunch: nothing is
// shown, and nothing should be heard.
bool RendererFastStart();
// True whenever nothing should be heard: during that, and while the game is
// paused because its window is in the background.
bool RendererSilent();
HostPad RendererPad();
// Motor speeds for every connected controller, 0 to 65535.
void RendererRumble(uint16_t left, uint16_t right);
