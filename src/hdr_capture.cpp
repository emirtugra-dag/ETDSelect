#include "hdr_capture.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wingdi.h>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

static float s_halfToFloatTable[65536];
static bool s_lutInitialized = false;

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

void HDRCapture::InitToneMapLUT() {
    if (s_lutInitialized) return;
    for (int i = 0; i < 65536; ++i) {
        s_halfToFloatTable[i] = HalfToFloatSlow((uint16_t)i);
    }
    s_lutInitialized = true;
}

static inline float FastHalfToFloat(uint16_t h) {
    return s_halfToFloatTable[h];
}

static inline uint8_t LinearToSrgbByte(float x) {
    if (x <= 0.0f) return 0;
    if (x >= 1.0f) return 255;
    float s = (x <= 0.0031308f) ? (12.92f * x) : (1.055f * powf(x, 1.0f / 2.4f) - 0.055f);
    int v = (int)(s * 255.0f + 0.5f);
    return (uint8_t)std::clamp(v, 0, 255);
}

// Tone mapping to prevent HDR overexposure / "parlama" (washed out highlights).
// Normalizes scRGB linear values by the display's SDR white level multiplier,
// keeping SDR content 100% accurate, and smoothly rolling off HDR highlights.
static inline float ToneMapHDR(float linearVal, float sdrWhiteMultiplier) {
    if (linearVal <= 0.0f) return 0.0f;
    float x = linearVal / sdrWhiteMultiplier;

    // Normal SDR range: 100% exact linear fidelity up to 0.90
    if (x <= 0.90f) {
        return x;
    }

    // Soft shoulder for bright whites and HDR highlights (prevents harsh clipping & blow-out)
    float excess = x - 0.90f;
    float mapped = 0.90f + 0.10f * (excess / (excess + 0.35f));
    return mapped > 1.0f ? 1.0f : mapped;
}

static float GetSdrWhiteLevelForDevice(LPCWSTR deviceName) {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS || pathCount == 0) {
        return 2.5f;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) {
        return 2.5f;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
        sourceName.header.id = paths[i].sourceInfo.id;

        if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
            if (!deviceName || deviceName[0] == L'\0' || _wcsicmp(sourceName.viewGdiDeviceName, deviceName) == 0) {
                DISPLAYCONFIG_SDR_WHITE_LEVEL sdrWhite = {};
                sdrWhite.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
                sdrWhite.header.size = sizeof(sdrWhite);
                sdrWhite.header.adapterId = paths[i].targetInfo.adapterId;
                sdrWhite.header.id = paths[i].targetInfo.id;

                if (DisplayConfigGetDeviceInfo(&sdrWhite.header) == ERROR_SUCCESS) {
                    float multiplier = (float)sdrWhite.SDRWhiteLevel / 1000.0f;
                    if (multiplier > 0.1f && multiplier < 20.0f) {
                        return multiplier;
                    }
                }
            }
        }
    }
    return 2.5f;
}

bool HDRCapture::IsAnyHDREnabled() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        return false;
    }

    bool hdrFound = false;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; !hdrFound && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        IDXGIOutput* output = nullptr;
        for (UINT o = 0; !hdrFound && adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            IDXGIOutput6* output6 = nullptr;
            if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void**)&output6))) {
                DXGI_OUTPUT_DESC1 desc1;
                if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                    if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || desc1.BitsPerColor > 8) {
                        hdrFound = true;
                    }
                }
                output6->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return hdrFound;
}

static bool CaptureOutputDXGI(
    ID3D11Device* d3dDevice,
    ID3D11DeviceContext* d3dContext,
    IDXGIOutput* dxgiOutput,
    bool isHDR,
    float sdrWhiteMultiplier,
    std::vector<DWORD>& outPixels,
    int& outWidth,
    int& outHeight
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

    // Try up to 3 times to get a frame (handles static desktop where nothing moved)
    for (int retry = 0; retry < 3; ++retry) {
        hr = dupl->AcquireNextFrame(80, &frameInfo, &desktopResource);
        if (hr == S_OK) {
            break;
        }
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }
        break;
    }

    if (FAILED(hr) || !desktopResource) {
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
    dupl->Release();

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTexture->Release();
        return false;
    }

    outWidth = desc.Width;
    outHeight = desc.Height;
    outPixels.resize(outWidth * outHeight);

    if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        // HDR scRGB (16-bit float per component)
        const uint8_t* pSrcRow = (const uint8_t*)mapped.pData;
        DWORD* pDst = outPixels.data();

        for (UINT y = 0; y < desc.Height; ++y) {
            const uint16_t* pSrc = (const uint16_t*)pSrcRow;
            for (UINT x = 0; x < desc.Width; ++x) {
                float r = FastHalfToFloat(pSrc[0]);
                float g = FastHalfToFloat(pSrc[1]);
                float b = FastHalfToFloat(pSrc[2]);

                float tmR = ToneMapHDR(r, sdrWhiteMultiplier);
                float tmG = ToneMapHDR(g, sdrWhiteMultiplier);
                float tmB = ToneMapHDR(b, sdrWhiteMultiplier);

                uint8_t byteR = LinearToSrgbByte(tmR);
                uint8_t byteG = LinearToSrgbByte(tmG);
                uint8_t byteB = LinearToSrgbByte(tmB);

                pDst[x] = 0xFF000000 | (byteR << 16) | (byteG << 8) | byteB;
                pSrc += 4;
            }
            pSrcRow += mapped.RowPitch;
            pDst += outWidth;
        }
    } else {
        // Standard SDR 8-bit BGRA
        const uint8_t* pSrcRow = (const uint8_t*)mapped.pData;
        DWORD* pDst = outPixels.data();
        for (UINT y = 0; y < desc.Height; ++y) {
            memcpy(pDst, pSrcRow, outWidth * sizeof(DWORD));
            for (int x = 0; x < outWidth; ++x) {
                pDst[x] |= 0xFF000000;
            }
            pSrcRow += mapped.RowPitch;
            pDst += outWidth;
        }
    }

    d3dContext->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    // SAFETY CHECK: Verify that the frame is not pure black
    // (e.g. GPU mismatch or black frame bug on some hybrid laptops)
    bool hasContent = false;
    for (size_t i = 0; i < outPixels.size(); i += 80) {
        if ((outPixels[i] & 0x00FFFFFF) != 0) {
            hasContent = true;
            break;
        }
    }

    return hasContent;
}

// Anti-overexposure filter applied to BitBlt captures of HDR monitors
// when DXGI Desktop Duplication is unavailable or fails.
// Restores contrast and softens blown-out whites.
static void ApplyHdrDeoverexposureFilter(std::vector<DWORD>& pixels, int width, int height, RECT monitorRect, int originX, int originY, float sdrMultiplier) {
    int startX = std::max(0, (int)(monitorRect.left - originX));
    int startY = std::max(0, (int)(monitorRect.top - originY));
    int endX = std::min(width, (int)(monitorRect.right - originX));
    int endY = std::min(height, (int)(monitorRect.bottom - originY));

    float gamma = 1.0f + 0.15f * (std::min)(sdrMultiplier - 1.0f, 2.5f);

    for (int y = startY; y < endY; ++y) {
        DWORD* row = pixels.data() + y * width;
        for (int x = startX; x < endX; ++x) {
            DWORD p = row[x];
            float r = ((p >> 16) & 0xFF) / 255.0f;
            float g = ((p >> 8) & 0xFF) / 255.0f;
            float b = (p & 0xFF) / 255.0f;

            r = powf(r, gamma);
            g = powf(g, gamma);
            b = powf(b, gamma);

            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            float satBoost = 1.04f;
            r = lum + (r - lum) * satBoost;
            g = lum + (g - lum) * satBoost;
            b = lum + (b - lum) * satBoost;

            int outR = std::clamp((int)(r * 255.0f + 0.5f), 0, 255);
            int outG = std::clamp((int)(g * 255.0f + 0.5f), 0, 255);
            int outB = std::clamp((int)(b * 255.0f + 0.5f), 0, 255);

            row[x] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
        }
    }
}

HBITMAP HDRCapture::CaptureScreen(int& width, int& height, int& originX, int& originY) {
    InitToneMapLUT();

    originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width   = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height  = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // STEP 1: ALWAYS capture the screen using the rock-solid, proven BitBlt first!
    // This ensures that under NO CIRCUMSTANCES will the screen ever be black!
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, originX, originY, SRCCOPY);
    SelectObject(hdcMem, hOld); // Immediately deselect so it is never locked
    DeleteDC(hdcMem);

    // STEP 2: Check if HDR is actually enabled on any monitor.
    // If NO monitors have HDR, return the standard BitBlt bitmap immediately!
    if (!IsAnyHDREnabled()) {
        ReleaseDC(NULL, hdcScreen);
        return hBitmap;
    }

    // STEP 3: HDR is active! Retrieve pixels from hBitmap into memory buffer for enhancement
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<DWORD> masterPixels(width * height);
    if (!GetDIBits(hdcScreen, hBitmap, 0, height, masterPixels.data(), &bmi, DIB_RGB_COLORS)) {
        ReleaseDC(NULL, hdcScreen);
        return hBitmap; // If reading DIB bits failed, return standard image safely
    }

    // STEP 4: Process HDR monitors via DXGI Desktop Duplication (GPU direct scRGB capture)
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            ID3D11Device* d3dDevice = nullptr;
            ID3D11DeviceContext* d3dContext = nullptr;
            D3D_FEATURE_LEVEL fl;

            // Create device on the EXACT adapter that drives these outputs (handles hybrid laptops)
            HRESULT hrDevice = D3D11CreateDevice(
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

            IDXGIOutput* dxgiOutput = nullptr;
            for (UINT o = 0; adapter->EnumOutputs(o, &dxgiOutput) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC desc;
                dxgiOutput->GetDesc(&desc);

                bool isHDR = false;
                IDXGIOutput6* output6 = nullptr;
                if (SUCCEEDED(dxgiOutput->QueryInterface(__uuidof(IDXGIOutput6), (void**)&output6))) {
                    DXGI_OUTPUT_DESC1 desc1;
                    if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                        if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || desc1.BitsPerColor > 8) {
                            isHDR = true;
                        }
                    }
                    output6->Release();
                }

                if (isHDR) {
                    float sdrMultiplier = GetSdrWhiteLevelForDevice(desc.DeviceName);
                    std::vector<DWORD> dxgiPixels;
                    int dxgiW = 0, dxgiH = 0;
                    bool captured = false;

                    if (SUCCEEDED(hrDevice) && d3dDevice && d3dContext) {
                        captured = CaptureOutputDXGI(
                            d3dDevice, d3dContext, dxgiOutput,
                            isHDR, sdrMultiplier,
                            dxgiPixels, dxgiW, dxgiH
                        );
                    }

                    RECT r = desc.DesktopCoordinates;
                    int destX = r.left - originX;
                    int destY = r.top - originY;
                    int destW = r.right - r.left;
                    int destH = r.bottom - r.top;

                    if (captured && dxgiW > 0 && dxgiH > 0) {
                        // DXGI succeeded with valid, non-black, tone-mapped HDR pixels!
                        for (int row = 0; row < destH && row < dxgiH; ++row) {
                            if (destY + row >= 0 && destY + row < height && destX >= 0 && destX < width) {
                                DWORD* pDstRow = masterPixels.data() + (destY + row) * width + destX;
                                const DWORD* pSrcRow = dxgiPixels.data() + row * dxgiW;
                                int copyCount = std::min(destW, dxgiW);
                                if (destX + copyCount > width) copyCount = width - destX;
                                if (copyCount > 0) {
                                    memcpy(pDstRow, pSrcRow, copyCount * sizeof(DWORD));
                                }
                            }
                        }
                    } else {
                        // DXGI failed or returned black: Apply anti-overexposure filter on BitBlt pixels
                        ApplyHdrDeoverexposureFilter(masterPixels, width, height, r, originX, originY, sdrMultiplier);
                    }
                }

                dxgiOutput->Release();
            }

            if (d3dContext) d3dContext->Release();
            if (d3dDevice) d3dDevice->Release();
            adapter->Release();
        }
        factory->Release();
    }

    // STEP 5: Write enhanced pixels back to hBitmap
    SetDIBits(hdcScreen, hBitmap, 0, height, masterPixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdcScreen);

    return hBitmap;
}
