#include "theme.h"

void NexusTheme::Apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 16.0f;
    s.ChildRounding = 14.0f;
    s.FrameRounding = 9.0f;
    s.PopupRounding = 10.0f;
    s.ScrollbarRounding = 10.0f;
    s.GrabRounding = 10.0f;
    s.TabRounding = 8.0f;

    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;

    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(12, 9);
    s.ItemSpacing = ImVec2(10, 10);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 10.0f;
    s.IndentSpacing = 14.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(ColBg);
    c[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(ColPanel);
    c[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(ColPanelAlt);
    c[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(ColBorder);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(ColPanelAlt);
    c[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(ColPanelHover);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.18f, 0.27f, 1.0f);

    c[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(ColText);
    c[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(ColTextFaint);

    c[ImGuiCol_Button] = ImVec4(0.13f, 0.15f, 0.22f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.20f, 0.30f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.25f, 0.36f, 1.0f);

    c[ImGuiCol_Header] = ImVec4(0.18f, 0.20f, 0.30f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.25f, 0.36f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.28f, 0.40f, 1.0f);

    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.26f, 0.35f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.33f, 0.44f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(ColAccent);

    c[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(ColAccentHi);
    c[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(ColAccent);
    c[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(ColAccentHi);

    c[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(ColBorder);
    c[ImGuiCol_SeparatorHovered] = ImGui::ColorConvertU32ToFloat4(ColAccent);
    c[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(ColAccentHi);
}

ImFont* NexusTheme::LoadUIFont(float size, bool bold) {
    ImGuiIO& io = ImGui::GetIO();
    const char* path = bold
        ? "C:\\Windows\\Fonts\\segoeuib.ttf"
        : "C:\\Windows\\Fonts\\segoeui.ttf";

    ImFont* font = io.Fonts->AddFontFromFileTTF(path, size);
    if (!font) font = io.Fonts->AddFontDefault();
    return font;
}