#include "processes.h"

// Windows y Direct3D
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>           // NECESARIO
#include <tlhelp32.h>
#include <shellapi.h>

// STL
#include <vector>
#include <unordered_map>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Gdi32.lib")

namespace {

    std::unordered_map<std::wstring, ID3D11ShaderResourceView*> g_IconCache;

    ID3D11ShaderResourceView* IconToSRV(ID3D11Device* device, HICON hIcon) {
        if (!hIcon || !device) return nullptr;

        ICONINFO ii{};
        if (!GetIconInfo(hIcon, &ii)) return nullptr;

        BITMAP bmColor{};
        GetObject(ii.hbmColor, sizeof(bmColor), &bmColor);
        int w = bmColor.bmWidth, h = bmColor.bmHeight;
        if (w <= 0 || h <= 0) {
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
            if (ii.hbmMask)  DeleteObject(ii.hbmMask);
            return nullptr;
        }

        std::vector<unsigned char> pixels((size_t)w * h * 4);

        HDC hdc = GetDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        GetDIBits(hdc, ii.hbmColor, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, hdc);

        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask)  DeleteObject(ii.hbmMask);

        bool hasAlpha = false;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i + 3] != 0) { hasAlpha = true; break; }
        }
        for (size_t i = 0; i < pixels.size(); i += 4) {
            unsigned char b = pixels[i + 0];
            unsigned char r = pixels[i + 2];
            pixels[i + 0] = r;
            pixels[i + 2] = b;
            if (!hasAlpha) pixels[i + 3] = 255;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = pixels.data();
        sub.SysMemPitch = (UINT)w * 4;

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, &sub, &tex)) || !tex) return nullptr;

        ID3D11ShaderResourceView* srv = nullptr;
        HRESULT hr = device->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
        return SUCCEEDED(hr) ? srv : nullptr;
    }

    ID3D11ShaderResourceView* GetOrLoadIcon(ID3D11Device* device, const std::wstring& key, const std::wstring& shellTarget) {
        auto it = g_IconCache.find(key);
        if (it != g_IconCache.end()) return it->second;

        SHFILEINFOW shfi{};
        ID3D11ShaderResourceView* srv = nullptr;
        if (SHGetFileInfoW(shellTarget.c_str(), 0, &shfi, sizeof(shfi), SHGFI_ICON | SHGFI_SMALLICON)) {
            srv = IconToSRV(device, shfi.hIcon);
            if (shfi.hIcon) DestroyIcon(shfi.hIcon);
        }
        g_IconCache[key] = srv;
        return srv;
    }

} // namespace

namespace NexusProcesses {

    std::vector<ProcessEntry> Enumerate(ID3D11Device* device) {
        std::vector<ProcessEntry> out;

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);

        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID == 0) continue;

                ProcessEntry entry;
                entry.pid = pe.th32ProcessID;
                entry.exeName = pe.szExeFile;

                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    wchar_t path[MAX_PATH] = {};
                    DWORD sz = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, path, &sz))
                        entry.fullPath.assign(path);
                    CloseHandle(hProc);
                }

                std::wstring shellTarget = !entry.fullPath.empty() ? entry.fullPath : entry.exeName;
                std::wstring cacheKey = shellTarget;
                entry.icon = GetOrLoadIcon(device, cacheKey, shellTarget);

                out.push_back(std::move(entry));
            } while (Process32NextW(snap, &pe));
        }

        CloseHandle(snap);
        return out;
    }

    void ReleaseIconCache() {
        for (auto& kv : g_IconCache) {
            if (kv.second) kv.second->Release();
        }
        g_IconCache.clear();
    }

}