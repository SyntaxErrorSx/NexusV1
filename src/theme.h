#pragma once
#include "imgui.h"

// ============================================================
// Paleta y estilo visual de la plantilla "Nexus"
// Cambia estos valores para ajustar el look sin tocar main.cpp
// ============================================================
namespace NexusTheme {

    // Colores base (formato IM_COL32: R, G, B, A)
    constexpr ImU32 ColBg = IM_COL32(9, 10, 15, 255);        // fondo general
    constexpr ImU32 ColPanel = IM_COL32(17, 19, 27, 255);    // fondo de paneles
    constexpr ImU32 ColPanelAlt = IM_COL32(22, 24, 34, 255); // inputs / tarjetas
    constexpr ImU32 ColPanelHover = IM_COL32(27, 30, 42, 255);
    constexpr ImU32 ColBorder = IM_COL32(36, 39, 53, 255);

    constexpr ImU32 ColAccent = IM_COL32(74, 118, 255, 255); // azul principal
    constexpr ImU32 ColAccentHi = IM_COL32(120, 155, 255, 255);
    constexpr ImU32 ColAccentSoft = IM_COL32(74, 118, 255, 40);

    constexpr ImU32 ColText = IM_COL32(232, 234, 240, 255);
    constexpr ImU32 ColTextDim = IM_COL32(150, 155, 172, 255);
    constexpr ImU32 ColTextFaint = IM_COL32(95, 100, 118, 255);

    // Aplica la paleta y el redondeado a ImGui::GetStyle()
    void Apply();

    // Carga una fuente del sistema (Segoe UI). Si no la encuentra, usa la default.
    ImFont* LoadUIFont(float size, bool bold = false);
}