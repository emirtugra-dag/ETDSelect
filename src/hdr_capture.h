#pragma once
#include <windows.h>
#include <vector>

struct MonitorHdrInfo {
    RECT rect;
    bool isHdr;
    float sdrMultiplier;
    unsigned char fallbackLut[256];
};

class HDRCapture {
public:
    // Captures the entire virtual desktop.
    // If HDR is active on any display, captures native FP16 scRGB via DXGI
    // and applies chromaticity-preserving Desktop Tone Mapping to SDR.
    // Eliminates "cırtlak" harsh/blown-out whites and restores natural contrast and colors.
    // Falls back seamlessly to GDI if DXGI is unavailable.
    static HBITMAP CaptureScreen(int& width, int& height, int& originX, int& originY);

    // Queries display topology and caches HDR state and D3D11 devices.
    // Called once at app startup and on WM_DISPLAYCHANGE.
    static void UpdateDisplayMode();

    // Checks if any connected monitor currently has HDR enabled.
    static bool IsAnyHDREnabled();

    // Cleans up cached DirectX resources on app exit.
    static void Cleanup();

private:
    static void InitLookupTables();
    static void BuildFallbackLut(unsigned char lut[256], float sdrMultiplier);

    static bool s_initialized;
    static bool s_anyHdrActive;
    static std::vector<MonitorHdrInfo> s_monitors;
};
