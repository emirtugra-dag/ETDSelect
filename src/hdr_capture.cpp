#include "hdr_capture.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wingdi.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>

bool HDRCapture::s_initialized = false;
bool HDRCapture::s_anyHdrActive = false;
std::vector<MonitorHdrInfo> HDRCapture::s_monitors;

// Pre-computed Half-Float to Float table (65536 entries, 256 KB)
static float s_halfToFloatTable[65536];
// Fast 12-bit Linear to sRGB table (4096 entries, 4 KB)
static uint8_t s_linearToSrgbLut[4096];
static bool s_tablesInitialized = false;

static float HalfToFloatSlow(uint16_t h) {
    uint32_t sign = (h & 0x8000) << 16;
    uint32_t exp  = (h & 0x7C00) >> 10;
    uint32_t mant = (h & 0x03FF);

    if (exp == 0) {
        if (mant == 0) {
            uint32_t val = sign;
            float f;
            memcpy(&f, &val, 4);
            return f;
        }
        while (!(mant & 0x0400)) {
            mant <<= 1;
            exp--;
        }
        exp++;
        mant &= ~0x0400;
    } else if (exp == 31) {
        uint32_t val = sign | 0x7F800000 | (mant << 13);
        float f;
        memcpy(&f, &val, 4);
        return f;
    }

    exp = exp + (127 - 15);
    uint32_t val = sign | (exp << 23) | (mant << 13);
    float f;
    memcpy(&f, &val, 4);
    return f;
}

void HDRCapture::InitLookupTables() {
    if (s_tablesInitialized) return;

    for (int i = 0; i < 65536; ++i) {
        s_halfToFloatTable[i] = HalfToFloatSlow((uint16_t)i);
    }

    for (int i = 0; i < 4096; ++i) {
        float x = (float)i / 4095.0f;
        float s = (x <= 0.0031308f) ? (12.92f * x) : (1.055f * powf(x, 1.0f / 2.4f) - 0.055f);
        int v = (int)(s * 255.0f + 0.5f);
        s_linearToSrgbLut[i] = (uint8_t)std::clamp(v, 0, 255);
    }

    s_tablesInitialized = true;
}

static inline uint8_t FastLinearToSrgb(float x) {
    if (x <= 0.0f) return 0;
    if (x >= 1.0f) return 255;
    int idx = (int)(x * 4095.0f + 0.5f);
    return s_linearToSrgbLut[idx];
}

// Chromaticity-Preserving Desktop Tone Mapper
// Tailored specifically for Windows HDR Desktop to eliminate "cırtlak" (harsh / overexposed) whites.
// - Normal SDR content (x <= 0.80) is kept 100% linear and identical to SDR.
// - Standard SDR white (x = 1.00) maps smoothly to paper-white (~248 sRGB), clean and comfortable.
// - HDR highlights and extreme whites (x > 1.00 up to 10.0+) roll off with a soft shoulder into 248..255.
static inline float TonemapDesktop(float x) {
    if (x <= 0.80f) return x;
    if (x <= 1.0f) {
        float t = (x - 0.80f) * 5.0f; // 0.0 to 1.0
        return 0.80f + 0.14f * (3.0f * t * t - 2.0f * t * t * t);
    }
    float excess = x - 1.0f;
    float hdrKnee = excess / (excess + 1.2f);
    return 0.94f + 0.06f * hdrKnee;
}

// Calibrated De-Overexposure LUT for GDI BitBlt
// Applied when DXGI is unavailable or on static desktops.
// Smoothly maps blown-out 255 whites down to a crisp, natural paper-white (240)
// and restores 18+ levels of gradation in the 220..255 highlight range.
void HDRCapture::BuildFallbackLut(unsigned char lut[256], float sdrMultiplier) {
    float m = std::clamp(sdrMultiplier, 1.0f, 4.0f);
    float maxWhite = 255.0f - 4.0f * (m - 1.0f);
    maxWhite = std::clamp(maxWhite, 238.0f, 255.0f);

    float gamma = 1.0f + 0.05f * (m - 1.0f);

    for (int i = 0; i < 256; ++i) {
        float x = i / 255.0f;
        float y = powf(x, gamma);

        if (x > 0.75f) {
            float t = (x - 0.75f) / 0.25f;
            float compressed = t / (1.0f + 0.15f * t);
            y = powf(0.75f, gamma) + (1.0f - powf(0.75f, gamma)) * compressed;
        }

        float outF = y * maxWhite;
        int outVal = (int)(outF + 0.5f);
        lut[i] = (unsigned char)std::clamp(outVal, 0, 255);
    }
}

void HDRCapture::Cleanup() {
    // No persistent COM pointers to prevent cross-call corruption
}

struct TempDeviceColor {
    std::wstring deviceName;
    bool isHdr;
    float multiplier;
};

void HDRCapture::UpdateDisplayMode() {
    InitLookupTables();

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
                dev.multiplier = 2.5f;

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
                BuildFallbackLut(info.fallbackLut, info.sdrMultiplier);
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

// Captures a single HDR monitor in native FP16 scRGB using Desktop Duplication
// and tone-maps it directly into the master 32-bit DIB virtual buffer.
// SAFETY: Never overwrites pVirtualBits if the frame is black or empty!
static bool CaptureOutputDXGI(
    ID3D11Device* d3dDevice,
    ID3D11DeviceContext* d3dContext,
    IDXGIOutput* dxgiOutput,
    float sdrWhiteMultiplier,
    void* pVirtualBits,
    int virtualW,
    int virtualH,
    int originX,
    int originY,
    RECT desktopCoordinates
) {
    IDXGIOutput5* output5 = nullptr;
    IDXGIOutput1* output1 = nullptr;
    IDXGIOutputDuplication* dupl = nullptr;
    HRESULT hr = E_FAIL;

    if (SUCCEEDED(dxgiOutput->QueryInterface(__uuidof(IDXGIOutput5), (void**)&output5))) {
        DXGI_FORMAT supportedFormats[] = {
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_B8G8R8A8_UNORM
        };
        hr = output5->DuplicateOutput1(d3dDevice, 0, 2, supportedFormats, &dupl);
        output5->Release();
    }

    if (!dupl && SUCCEEDED(dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1))) {
        hr = output1->DuplicateOutput(d3dDevice, &dupl);
        output1->Release();
    }

    if (FAILED(hr) || !dupl) {
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    IDXGIResource* desktopResource = nullptr;

    // Fast acquisition: up to 2 quick attempts (15ms timeout each)
    for (int retry = 0; retry < 2; ++retry) {
        hr = dupl->AcquireNextFrame(15, &frameInfo, &desktopResource);
        if (hr == S_OK) {
            // CRITICAL SAFETY CHECK:
            // If LastPresentTime is 0, the desktop image was NOT updated (only cursor or blank).
            // Do NOT use a zero-timestamp frame as it will produce a black screen!
            if (frameInfo.LastPresentTime.QuadPart > 0 && desktopResource != nullptr) {
                break;
            }
            if (desktopResource) {
                desktopResource->Release();
                desktopResource = nullptr;
            }
            dupl->ReleaseFrame();
        }
        if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
            break;
        }
    }

    if (FAILED(hr) || !desktopResource || frameInfo.LastPresentTime.QuadPart == 0) {
        if (desktopResource) desktopResource->Release();
        dupl->Release();
        return false;
    }

    ID3D11Texture2D* acquiredTexture = nullptr;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquiredTexture);
    desktopResource->Release();

    if (FAILED(hr) || !acquiredTexture) {
        dupl->ReleaseFrame();
        dupl->Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    acquiredTexture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    hr = d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr) || !stagingTexture) {
        acquiredTexture->Release();
        dupl->ReleaseFrame();
        dupl->Release();
        return false;
    }

    d3dContext->CopyResource(stagingTexture, acquiredTexture);
    acquiredTexture->Release();
    dupl->ReleaseFrame();
    dupl->Release(); // Duplication lock released immediately

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTexture->Release();
        return false;
    }

    int destX = desktopCoordinates.left - originX;
    int destY = desktopCoordinates.top - originY;
    int copyW = (std::min)((int)(desktopCoordinates.right - desktopCoordinates.left), (int)desc.Width);
    int copyH = (std::min)((int)(desktopCoordinates.bottom - desktopCoordinates.top), (int)desc.Height);

    if (destX + copyW > virtualW) copyW = virtualW - destX;
    if (destY + copyH > virtualH) copyH = virtualH - destY;

    // CRITICAL SAFETY CHECK:
    // Sample pixels to verify that the acquired frame is NOT completely black!
    // (e.g. hybrid laptop GPU mismatch, DRM protection, or compositor black frame)
    bool hasVisibleContent = false;
    const uint8_t* pCheckRow = (const uint8_t*)mapped.pData;
    int checkRows = (std::min)(copyH, 20);
    for (int r = 0; r < checkRows && !hasVisibleContent; ++r) {
        const uint32_t* pRow32 = (const uint32_t*)(pCheckRow + r * mapped.RowPitch);
        int checkCols = (std::min)(copyW, 100);
        for (int c = 0; c < checkCols; ++c) {
            if ((pRow32[c] & 0x00FFFFFF) != 0) {
                hasVisibleContent = true;
                break;
            }
        }
    }

    if (!hasVisibleContent) {
        // Frame is pure black! DO NOT TOUCH pVirtualBits!
        d3dContext->Unmap(stagingTexture, 0);
        stagingTexture->Release();
        return false;
    }

    if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        // NATIVE HDR scRGB (16-bit float per channel)
        // Apply chromaticity-preserving Desktop Tone Mapping
        float invSdrWhite = (sdrWhiteMultiplier > 0.0f) ? (1.0f / sdrWhiteMultiplier) : (1.0f / 2.5f);
        const uint8_t* pSrcRow = (const uint8_t*)mapped.pData;

        for (int y = 0; y < copyH; ++y) {
            if (destY + y >= 0 && destY + y < virtualH && destX >= 0) {
                const uint16_t* pSrc = (const uint16_t*)pSrcRow;
                uint32_t* pDst = (uint32_t*)pVirtualBits + (destY + y) * virtualW + destX;

                for (int x = 0; x < copyW; ++x) {
                    float r = s_halfToFloatTable[pSrc[0]] * invSdrWhite;
                    float g = s_halfToFloatTable[pSrc[1]] * invSdrWhite;
                    float b = s_halfToFloatTable[pSrc[2]] * invSdrWhite;

                    // Max-channel scaling factor preserves 100% chromaticity (hue & saturation)
                    // Eliminates garish ("cırtlak") whites and washed-out highlights!
                    float maxC = (std::max)(r, (std::max)(g, b));
                    if (maxC > 0.0001f) {
                        float mappedVal = TonemapDesktop(maxC);
                        float scale = mappedVal / maxC;
                        r *= scale;
                        g *= scale;
                        b *= scale;
                    }

                    uint8_t byteR = FastLinearToSrgb(r);
                    uint8_t byteG = FastLinearToSrgb(g);
                    uint8_t byteB = FastLinearToSrgb(b);

                    pDst[x] = 0xFF000000 | (byteR << 16) | (byteG << 8) | byteB;
                    pSrc += 4;
                }
            }
            pSrcRow += mapped.RowPitch;
        }
    } else {
        // 8-bit standard SDR
        const uint8_t* pSrcRow = (const uint8_t*)mapped.pData;
        for (int y = 0; y < copyH; ++y) {
            if (destY + y >= 0 && destY + y < virtualH && destX >= 0) {
                uint32_t* pDst = (uint32_t*)pVirtualBits + (destY + y) * virtualW + destX;
                memcpy(pDst, pSrcRow, copyW * sizeof(uint32_t));
                for (int x = 0; x < copyW; ++x) {
                    pDst[x] |= 0xFF000000;
                }
            }
            pSrcRow += mapped.RowPitch;
        }
    }

    d3dContext->Unmap(stagingTexture, 0);
    stagingTexture->Release();
    return true;
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

    // STEP 1: Allocate top-down 32-bit DIBSection directly.
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBitmap) {
        hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

    // Hardware-accelerated BitBlt (2-3ms) guarantees base desktop is ALWAYS captured!
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, originX, originY, SRCCOPY);

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    // If no monitors have HDR enabled, return the GDI capture immediately (2-3ms total)
    if (!s_anyHdrActive || pBits == nullptr) {
        return hBitmap;
    }

    // STEP 2: HDR is active! Try native FP16 scRGB capture via DXGI Desktop Duplication.
    // If DXGI fails or returns black, seamlessly fall back to applying the calibrated
    // de-overexposure LUT to the BitBlt pixels. (ZERO risk of black screen!)
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            ID3D11Device* d3dDevice = nullptr;
            ID3D11DeviceContext* d3dContext = nullptr;
            D3D_FEATURE_LEVEL fl;

            HRESULT hrDev = D3D11CreateDevice(
                adapter,
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &d3dDevice,
                &fl,
                &d3dContext
            );

            if (SUCCEEDED(hrDev) && d3dDevice && d3dContext) {
                IDXGIOutput* dxgiOutput = nullptr;
                for (UINT o = 0; adapter->EnumOutputs(o, &dxgiOutput) != DXGI_ERROR_NOT_FOUND; ++o) {
                    DXGI_OUTPUT_DESC desc;
                    dxgiOutput->GetDesc(&desc);

                    bool isHdr = false;
                    IDXGIOutput6* output6 = nullptr;
                    if (SUCCEEDED(dxgiOutput->QueryInterface(__uuidof(IDXGIOutput6), (void**)&output6))) {
                        DXGI_OUTPUT_DESC1 desc1;
                        if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                            if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || desc1.BitsPerColor > 8) {
                                isHdr = true;
                            }
                        }
                        output6->Release();
                    }

                    if (isHdr) {
                        float sdrMultiplier = 2.5f;
                        for (const auto& m : s_monitors) {
                            if (m.isHdr && m.rect.left == desc.DesktopCoordinates.left && m.rect.top == desc.DesktopCoordinates.top) {
                                sdrMultiplier = m.sdrMultiplier;
                                break;
                            }
                        }

                        bool captured = CaptureOutputDXGI(
                            d3dDevice, d3dContext, dxgiOutput,
                            sdrMultiplier, pBits,
                            width, height, originX, originY,
                            desc.DesktopCoordinates
                        );

                        if (!captured) {
                            // DXGI was unavailable or static: Apply calibrated de-overexposure LUT on BitBlt pixels
                            int x0 = (std::max)(0, (int)(desc.DesktopCoordinates.left - originX));
                            int y0 = (std::max)(0, (int)(desc.DesktopCoordinates.top - originY));
                            int x1 = (std::min)(width, (int)(desc.DesktopCoordinates.right - originX));
                            int y1 = (std::min)(height, (int)(desc.DesktopCoordinates.bottom - originY));

                            for (const auto& m : s_monitors) {
                                if (m.isHdr && m.rect.left == desc.DesktopCoordinates.left) {
                                    for (int y = y0; y < y1; ++y) {
                                        uint8_t* row = (uint8_t*)pBits + (y * width + x0) * 4;
                                        for (int x = x0; x < x1; ++x) {
                                            row[0] = m.fallbackLut[row[0]];
                                            row[1] = m.fallbackLut[row[1]];
                                            row[2] = m.fallbackLut[row[2]];
                                            row += 4;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    dxgiOutput->Release();
                }
                d3dContext->Release();
                d3dDevice->Release();
            }
            adapter->Release();
        }
        factory->Release();
    }

    return hBitmap;
}
