// Xenos GPU command processor interface.
#pragma once

#include "guest.h"

// Maps the register block. Returns false on failure.
bool GpuInit();
void GpuInitRingBuffer(uint32_t physicalAddress, uint32_t sizeLog2);
void GpuEnableReadPointerWriteBack(uint32_t physicalAddress);
void GpuSetInterruptCallback(uint32_t callback, uint32_t context);
// Fills the 64-word slot VdSwap is given with a swap packet.
void GpuWriteSwapPacket(uint32_t buffer, uint32_t frontBuffer, uint32_t width, uint32_t height);
uint32_t GpuFrameCount();

// Runs a guest function on the calling host thread, creating a guest thread
// block for it on first use. A non-negative processor is written to the PCR as
// the current processor number first. Returns the guest's r3.
uint32_t GuestCall(uint32_t address, uint32_t r3, uint32_t r4, int processor = -1);
