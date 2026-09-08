#pragma once
#include <windows.h>

class HDRCapture {
public:
    // Captures the entire virtual desktop.
    // If HDR is active on any monitor, uses DXGI Desktop Duplication in FP16 (scRGB)
    // and applies tone mapping to prevent overexposure/parlama.
    // Gracefully falls back to GDI BitBlt if DXGI is unavailable.
    static HBITMAP CaptureScreen(int& width, int& height, int& originX, int& originY);

    // Checks if any connected monitor currently has HDR enabled.
    static bool IsAnyHDREnabled();

private:
    static void InitToneMapLUT();
};
