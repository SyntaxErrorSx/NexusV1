#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

// Declaraciones adelantadas (NO incluir d3d11.h aquí para evitar dependencias)
struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace NexusProcesses {

    struct ProcessEntry {
        uint32_t pid = 0;
        std::wstring exeName;
        std::wstring fullPath;
        ID3D11ShaderResourceView* icon = nullptr;
    };

    std::vector<ProcessEntry> Enumerate(ID3D11Device* device);
    void ReleaseIconCache();

}