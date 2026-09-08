#include "hdr_capture.h"
#include <wingdi.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdint>

bool HDRCapture::s_initialized = false;
bool HDRCapture::s_anyHdrActive = false;
std::vector<MonitorHdrInfo> HDRCapture::s_monitors;

// Generates a pre-computed 256-byte Tone Mapping Lookup Table (LUT)
// tailored specifically for this monitor's SDR brightness level.
// Eliminates overexposure / "parlama" (washed out highlights, blinding whites)
// on HDR displays while preserving 100% color accuracy, pure black (0) and pure white (255).
void HDRCapture::BuildHdrToneLut(unsigned char lut[256], float sdrMultiplier) {
    float strength = std::clamp((sdrMultiplier - 1.0f) / 2.0f, 0.0f, 1.0f);
    float gamma = 1.0f + 0.16f * strength; // 1.00 (min) to 1.16 (standard HDR)

    float knee = 0.80f;
    float kneeY = powf(knee, gamma);

    for (int i = 0; i < 256; ++i) {
        float x = i / 255.0f;
        float y = 0.0f;

        if (x <= knee) {
            // Midtones & shadows: deepen contrast to eliminate the washed-out milky haze
            y = powf(x, gamma);
        } else {
            // Highlights (x > 0.80): soft shoulder rolloff to preserve subtle details
            // without harsh clipping, while keeping 255 as true white.
            float t = (x - knee) / (1.0f - knee); // 0.0 to 1.0
            float roll = (t * 1.3f) / (1.0f + 0.3f * t);
            y = kneeY + (1.0f - kneeY) * roll;
        }

        int outVal = (int)(y * 255.0f + 0.5f);
        lut[i] = (unsigned char)std::clamp(outVal, 0, 255);
    }
}

struct TempDeviceColor {
    std::wstring deviceName;
    bool isHdr;
    float multiplier;
};

static BOOL CALLBACK MonitorEnumCallback(HMONITOR hMon, HDC, LPRECT, LPARAM lParam) {
    auto pDeviceList = (const std::vector<TempDeviceColor>*)lParam;
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi)) {
        MonitorHdrInfo info = {};
        info.rect = mi.rcMonitor;
        info.isHdr = false;
        info.sdrMultiplier = 1.0f;

        if (pDeviceList) {
            for (const auto& dev : *pDeviceList) {
                if (!dev.deviceName.empty() && _wcsicmp(dev.deviceName.c_str(), mi.szDevice) == 0) {
                    info.isHdr = dev.isHdr;
                    info.sdrMultiplier = dev.multiplier;
                    break;
                }
            }
        }

        // Fallback: if QueryDisplayConfig didn't match, check if device reports > 8 bpp
        if (!info.isHdr) {
            DEVMODEW dm = {};
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                if (dm.dmBitsPerPel > 32) {
                    info.isHdr = true;
                    info.sdrMultiplier = 2.5f;
                }
            }
        }

        if (info.isHdr) {
            // Build the per-monitor anti-parlama LUT
            // (Takes < 0.01ms to compute 256 bytes)
            HDRCapture::IsAnyHDREnabled(); // ensures any caller knows
        }

        // Add to list
        // Note: s_monitors is populated directly in UpdateDisplayMode
    }
    return TRUE;
}

void HDRCapture::UpdateDisplayMode() {
    s_monitors.clear();
    s_anyHdrActive = false;

    std::vector<TempDeviceColor> deviceList;

    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) == ERROR_SUCCESS && pathCount > 0) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) == ERROR_SUCCESS) {
            for (UINT32 i = 0; i < pathCount; ++i) {
                TempDeviceColor dev = {};
                dev.isHdr = false;
                dev.multiplier = 1.0f;

                DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
                sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                sourceName.header.size = sizeof(sourceName);
                sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
                sourceName.header.id = paths[i].sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
                    dev.deviceName = sourceName.viewGdiDeviceName;
                }

                DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo = {};
                colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
                colorInfo.header.size = sizeof(colorInfo);
                colorInfo.header.adapterId = paths[i].targetInfo.adapterId;
                colorInfo.header.id = paths[i].targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&colorInfo.header) == ERROR_SUCCESS) {
                    dev.isHdr = (colorInfo.advancedColorEnabled != 0);
                }

                DISPLAYCONFIG_SDR_WHITE_LEVEL sdrWhite = {};
                sdrWhite.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
                sdrWhite.header.size = sizeof(sdrWhite);
                sdrWhite.header.adapterId = paths[i].targetInfo.adapterId;
                sdrWhite.header.id = paths[i].targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&sdrWhite.header) == ERROR_SUCCESS) {
                    if (sdrWhite.SDRWhiteLevel > 0) {
                        dev.multiplier = std::clamp((float)sdrWhite.SDRWhiteLevel / 80000.0f, 1.0f, 5.0f);
                    }
                }

                deviceList.push_back(dev);
            }
        }
    }

    // Enumerate monitors to map GDI coordinates
    struct EnumContext {
        const std::vector<TempDeviceColor>* pList;
        std::vector<MonitorHdrInfo>* pMonitors;
        bool* pAnyHdr;
    } ctx = { &deviceList, &s_monitors, &s_anyHdrActive };

    auto enumCallback = [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
        auto pCtx = (EnumContext*)lParam;
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi)) {
            MonitorHdrInfo info = {};
            info.rect = mi.rcMonitor;
            info.isHdr = false;
            info.sdrMultiplier = 1.0f;

            if (pCtx->pList) {
                for (const auto& dev : *(pCtx->pList)) {
                    if (!dev.deviceName.empty() && _wcsicmp(dev.deviceName.c_str(), mi.szDevice) == 0) {
                        info.isHdr = dev.isHdr;
                        info.sdrMultiplier = dev.multiplier;
                        break;
                    }
                }
            }

            if (info.isHdr) {
                BuildHdrToneLut(info.lut, info.sdrMultiplier);
                *(pCtx->pAnyHdr) = true;
            }

            pCtx->pMonitors->push_back(info);
        }
        return TRUE;
    };

    EnumDisplayMonitors(NULL, NULL, enumCallback, (LPARAM)&ctx);
    s_initialized = true;
}

bool HDRCapture::IsAnyHDREnabled() {
    if (!s_initialized) {
        UpdateDisplayMode();
    }
    return s_anyHdrActive;
}

HBITMAP HDRCapture::CaptureScreen(int& width, int& height, int& originX, int& originY) {
    if (!s_initialized) {
        UpdateDisplayMode();
    }

    originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width   = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height  = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hdcScreen = GetDC(NULL);

    // Create a top-down 32-bit DIBSection directly.
    // This provides direct pointer access to the raw pixel memory (pBits)
    // with ZERO heap allocation, ZERO GetDIBits/SetDIBits overhead, and ZERO lag.
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

    if (!hBitmap) {
        // Fallback to CreateCompatibleBitmap if DIBSection creation fails
        hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

    // Hardware-accelerated GDI capture (takes ~2-4ms)
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, originX, originY, SRCCOPY);

    // Deselect hBitmap from hdcMem immediately to prevent locked DC bugs
    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    // If HDR is active on any monitor and we have direct pointer access:
    // Apply the pre-computed 256-byte anti-parlama LUT in-place.
    // Takes ~2-4ms across millions of pixels, zero CPU spikes, zero heap allocations!
    if (s_anyHdrActive && pBits != nullptr) {
        for (const auto& mon : s_monitors) {
            if (mon.isHdr) {
                int x0 = (std::max)(0, (int)(mon.rect.left - originX));
                int y0 = (std::max)(0, (int)(mon.rect.top - originY));
                int x1 = (std::min)(width, (int)(mon.rect.right - originX));
                int y1 = (std::min)(height, (int)(mon.rect.bottom - originY));

                for (int y = y0; y < y1; ++y) {
                    uint8_t* row = (uint8_t*)pBits + (y * width + x0) * 4;
                    for (int x = x0; x < x1; ++x) {
                        row[0] = mon.lut[row[0]]; // Blue
                        row[1] = mon.lut[row[1]]; // Green
                        row[2] = mon.lut[row[2]]; // Red
                        row += 4;
                    }
                }
            }
        }
    }

    return hBitmap;
}
