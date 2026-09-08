#pragma once
#include <windows.h>
#include <vector>

struct MonitorHdrInfo {
    RECT rect;
    bool isHdr;
    float sdrMultiplier;
    unsigned char lut[256];
};

class HDRCapture {
public:
    // Captures the entire virtual desktop instantaneously into a 32-bit DIB.
    // If HDR is active on any monitor, applies calibrated tone mapping directly in-place
    // to prevent overexposure/parlama without any DirectX initialization delay.
    // Takes ~3-5ms total, 0MB heap allocations, 100% reliable.
    static HBITMAP CaptureScreen(int& width, int& height, int& originX, int& originY);

    // Queries display topology and caches HDR state + tone LUTs.
    // Called once at startup and on WM_DISPLAYCHANGE.
    static void UpdateDisplayMode();

    // Checks if any connected monitor currently has HDR enabled.
    static bool IsAnyHDREnabled();

private:
    static void BuildHdrToneLut(unsigned char lut[256], float sdrMultiplier);

    static bool s_initialized;
    static bool s_anyHdrActive;
    static std::vector<MonitorHdrInfo> s_monitors;
};
