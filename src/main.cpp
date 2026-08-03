#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "theme.h"
#include "processes.h"
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <tchar.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <random>
#include <commdlg.h>
#include <psapi.h>

// ============================================================
// INCLUIR EL ARCHIVO DE RECURSOS
// ============================================================
#include "resource.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Comdlg32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ------------------------------------------------------------
// Estado global
// ------------------------------------------------------------
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND g_hwnd = nullptr;
static bool g_IsMaximized = false;
static RECT g_RestoreRect{};

enum class AppState { Splash, Main };
static AppState g_State = AppState::Splash;
static float g_LoadProgress = 0.0f;
static float g_Time = 0.0f;

static const int kSplashW = 420, kSplashH = 290;
static const int kMainW = 540, kMainH = 420;  // Aumentado para dar espacio
static const int kTitleBarH = 38;

static ImFont* g_FontRegular = nullptr;
static ImFont* g_FontMedium = nullptr;
static ImFont* g_FontTitle = nullptr;
static ImFont* g_FontBig = nullptr;
static ImFont* g_FontSmall = nullptr;

// ------------------------------------------------------------
// Variables de proceso
// ------------------------------------------------------------
static std::vector<NexusProcesses::ProcessEntry> g_Processes;
static int g_SelectedIndex = -1;
static char g_SearchBuf[128] = "";
static bool g_ShowInjectionDialog = false;
static char g_DllPath[512] = "";
static uint32_t g_TargetPid = 0;
static std::string g_InjectionStatus = "";
static char g_DllPathDisplay[512] = "Ningun DLL seleccionado";
static bool g_InjectionSuccess = false;

// ------------------------------------------------------------
// Partículas y Estrellas Animadas
// ------------------------------------------------------------
struct Star {
    float x, y;
    float speed;
    float size;
    float brightness;
    float phase;
    float dx;
};
static std::vector<Star> g_Stars;
static std::mt19937 g_Rng(std::random_device{}());

void InitStars(int count = 110) {
    g_Stars.clear();
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < count; i++) {
        Star s;
        s.x = dist(g_Rng);
        s.y = dist(g_Rng);
        s.speed = 0.03f + dist(g_Rng) * 0.15f;
        s.size = 0.8f + dist(g_Rng) * 2.2f;
        s.brightness = 0.3f + dist(g_Rng) * 0.7f;
        s.phase = dist(g_Rng) * 6.2832f;
        s.dx = (dist(g_Rng) - 0.5f) * 0.02f;
        g_Stars.push_back(s);
    }
}

void UpdateStars(float dt) {
    for (auto& s : g_Stars) {
        s.y -= dt * s.speed * 0.12f;
        s.x += dt * s.dx * 0.1f;
        s.phase += dt * 0.6f;

        if (s.y < -0.05f) {
            s.y = 1.05f;
            s.x = std::uniform_real_distribution<float>(0.0f, 1.0f)(g_Rng);
        }
        if (s.x < -0.05f) s.x = 1.05f;
        if (s.x > 1.05f) s.x = -0.05f;

        s.brightness = 0.35f + 0.65f * (0.5f + 0.5f * sinf(s.phase));
    }
}

void DrawStars(ImDrawList* dl, ImVec2 size) {
    for (const auto& s : g_Stars) {
        float alpha = s.brightness * 0.85f;
        ImVec2 pos(s.x * size.x, s.y * size.y);

        if (s.size > 1.2f) {
            dl->AddCircleFilled(pos, s.size * 2.2f, IM_COL32(80, 130, 255, (int)(alpha * 20)));
        }
        dl->AddCircleFilled(pos, s.size * 0.6f, IM_COL32(230, 240, 255, (int)(alpha * 220)));

        if (s.size > 1.8f) {
            for (int i = 0; i < 4; i++) {
                float angle = i * 1.5708f + s.phase * 0.1f;
                float dist = 3.5f + sinf(s.phase + i) * 1.5f;
                dl->AddLine(pos, ImVec2(pos.x + cosf(angle) * dist, pos.y + sinf(angle) * dist),
                    IM_COL32(140, 180, 255, (int)(alpha * 50)));
            }
        }
    }
}

// ------------------------------------------------------------
// Funciones de Soporte
// ------------------------------------------------------------
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);
void CenterWindow(HWND hwnd, int w, int h);
void ToggleMaximize();
void RenderSplash();
void RenderMain();
void ApplyLiquidGlass();

// ------------------------------------------------------------
// FUNCIONES PARA ABRIR ENLACES
// ------------------------------------------------------------
void OpenURL(const char* url) {
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

// Easing para animaciones suaves
static float EaseOutCubic(float x) {
    return 1.0f - powf(1.0f - x, 3.0f);
}

// ------------------------------------------------------------
// Verificar permisos de administrador
// ------------------------------------------------------------
bool IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

// ------------------------------------------------------------
// Convertir wstring a string
// ------------------------------------------------------------
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// ------------------------------------------------------------
// Funciones de Proceso
// ------------------------------------------------------------
void RefreshProcesses() {
    if (g_pd3dDevice) {
        g_Processes = NexusProcesses::Enumerate(g_pd3dDevice);
        if (g_SelectedIndex >= (int)g_Processes.size()) {
            g_SelectedIndex = -1;
        }
    }
}

std::wstring GetProcessNameByPID(uint32_t pid) {
    for (const auto& p : g_Processes) {
        if (p.pid == pid) return p.exeName;
    }
    return L"Desconocido";
}

void OpenFileLocation(const std::wstring& path) {
    if (!path.empty()) {
        std::wstring params = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

bool InjectDLL(uint32_t pid, const char* dllPath) {
    g_InjectionStatus = "Inyectando...";
    g_InjectionSuccess = false;

    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        g_InjectionStatus = "Error: El archivo DLL no existe";
        return false;
    }

    if (!IsAdmin()) {
        g_InjectionStatus = "Error: Se requieren permisos de Admin";
        return false;
    }

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid
    );

    if (!hProcess) {
        DWORD error = GetLastError();
        char errorMsg[256];
        sprintf_s(errorMsg, "Error: No se pudo abrir proceso (Cod: %d)", error);
        g_InjectionStatus = errorMsg;
        return false;
    }

    BOOL isWow64 = FALSE;
    if (!IsWow64Process(hProcess, &isWow64)) {
        g_InjectionStatus = "Error: Falla al verificar arquitectura";
        CloseHandle(hProcess);
        return false;
    }

#ifdef _WIN64
    if (isWow64) {
        g_InjectionStatus = "Error: Proceso 32-bit (DLL de 64-bit)";
        CloseHandle(hProcess);
        return false;
    }
#else
    if (!isWow64) {
        g_InjectionStatus = "Error: Proceso 64-bit (DLL de 32-bit)";
        CloseHandle(hProcess);
        return false;
    }
#endif

    char fullPath[MAX_PATH];
    GetFullPathNameA(dllPath, MAX_PATH, fullPath, nullptr);
    size_t dllPathSize = strlen(fullPath) + 1;

    LPVOID pRemoteMemory = VirtualAllocEx(hProcess, nullptr, dllPathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMemory) {
        g_InjectionStatus = "Error: Falló asignación de memoria";
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pRemoteMemory, fullPath, dllPathSize, nullptr)) {
        g_InjectionStatus = "Error: No se escribió la ruta en memoria";
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC pLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");

    if (!pLoadLibraryA) {
        g_InjectionStatus = "Error: No se halló LoadLibraryA";
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryA, pRemoteMemory, 0, nullptr);
    if (!hThread) {
        g_InjectionStatus = "Error: No se creó hilo remoto";
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (exitCode != 0) {
        char resultMsg[256];
        sprintf_s(resultMsg, "Inyección exitosa (Module Handle: 0x%X)", exitCode);
        g_InjectionStatus = resultMsg;
        g_InjectionSuccess = true;
        return true;
    }
    else {
        g_InjectionStatus = "El DLL cargó pero devolvió Handle nulo (0)";
        return false;
    }
}

// ============================================================
// WinMain - CON ICONO CORREGIDO PARA LA BARRA DE TAREAS
// ============================================================
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    bool isAdmin = IsAdmin();
    if (!isAdmin) {
        MessageBoxW(nullptr,
            L"ADVERTENCIA: No se ejecutó como Administrador.\n"
            L"La inyección en ciertos procesos del sistema podría fallar.",
            L"Nexus - Advertencia",
            MB_ICONWARNING | MB_OK);
    }

    // ========================================
    // CARGAR ICONO DESDE RESOURCE (ID 101)
    // ========================================
    HICON hAppIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    HICON hAppIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    // Si falla, usar icono por defecto
    if (!hAppIcon) {
        hAppIcon = LoadIcon(nullptr, IDI_APPLICATION);
        hAppIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    }

    // ========================================
    // REGISTRAR CLASE CON ICONOS
    // ========================================
    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        hInstance,
        hAppIcon,        // Icono grande (32x32)
        nullptr,
        nullptr,
        nullptr,
        L"NexusUITemplate",
        hAppIconSm       // Icono pequeño (16x16) - PARA LA BARRA DE TAREAS
    };
    RegisterClassExW(&wc);

    // ========================================
    // CREAR VENTANA CON WS_OVERLAPPEDWINDOW PARA EL ICONO
    // ========================================
    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,                    // Para que aparezca en la barra de tareas
        wc.lpszClassName,
        L"Nexus",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        100, 100, kSplashW, kSplashH,
        nullptr, nullptr, wc.hInstance, nullptr);

    // ========================================
    // ESTABLECER ICONOS EN LA VENTANA
    // ========================================
    if (hAppIcon) {
        SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
        SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIconSm);
    }

    // También establecer el icono a nivel de clase
    SetClassLongPtrW(g_hwnd, GCLP_HICON, (LONG_PTR)hAppIcon);
    SetClassLongPtrW(g_hwnd, GCLP_HICONSM, (LONG_PTR)hAppIconSm);

    // ========================================
    // QUITAR BORDES
    // ========================================
    LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowLongW(g_hwnd, GWL_STYLE, style);

    // ========================================
    // FORZAR ACTUALIZACIÓN DEL ICONO
    // ========================================
    RedrawWindow(g_hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);

    // ========================================
    // AGREGAR A LA BARRA DE TAREAS (FORZAR)
    // ========================================
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = hAppIconSm ? hAppIconSm : hAppIcon;
    wcscpy_s(nid.szTip, _countof(nid.szTip), L"Nexus Process Manager");
    Shell_NotifyIconW(NIM_ADD, &nid);
    Shell_NotifyIconW(NIM_DELETE, &nid);

    CenterWindow(g_hwnd, kSplashW, kSplashH);

    if (!CreateDeviceD3D(g_hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 1;
    fontConfig.RasterizerMultiply = 1.2f;

    const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
    g_FontRegular = io.Fonts->AddFontFromFileTTF(fontPath, 14.0f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
    if (!g_FontRegular) g_FontRegular = io.Fonts->AddFontDefault();

    g_FontSmall = io.Fonts->AddFontFromFileTTF(fontPath, 11.0f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
    if (!g_FontSmall) g_FontSmall = g_FontRegular;

    g_FontMedium = io.Fonts->AddFontFromFileTTF(fontPath, 15.0f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
    if (!g_FontMedium) g_FontMedium = g_FontRegular;

    g_FontTitle = io.Fonts->AddFontFromFileTTF(fontPath, 20.0f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
    if (!g_FontTitle) g_FontTitle = g_FontRegular;

    g_FontBig = io.Fonts->AddFontFromFileTTF(fontPath, 28.0f, &fontConfig, io.Fonts->GetGlyphRangesDefault());
    if (!g_FontBig) g_FontBig = g_FontRegular;

    ApplyLiquidGlass();

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    RefreshProcesses();
    InitStars();

    bool done = false;
    ULONGLONG lastTick = GetTickCount64();

    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ULONGLONG now = GetTickCount64();
        float dt = (now - lastTick) / 1000.0f;
        lastTick = now;
        g_Time += dt;

        UpdateStars(dt);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_State == AppState::Splash) {
            g_LoadProgress += dt / 1.8f;
            if (g_LoadProgress >= 1.0f) {
                g_LoadProgress = 1.0f;
                g_State = AppState::Main;
                SetWindowPos(g_hwnd, nullptr, 0, 0, kMainW, kMainH, SWP_NOMOVE | SWP_NOZORDER);
                CenterWindow(g_hwnd, kMainW, kMainH);
            }
            RenderSplash();
        }
        else {
            RenderMain();
        }

        ImGui::Render();
        const float clear[4] = { 9 / 255.0f, 10 / 255.0f, 15 / 255.0f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    NexusProcesses::ReleaseIconCache();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

// ============================================================
// ESTILO VISUAL / GLASSMORPHISM
// ============================================================
void ApplyLiquidGlass() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 16.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 14.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 8.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(10, 8);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(6, 4);

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(9 / 255.0f, 10 / 255.0f, 15 / 255.0f, 0.92f);
    c[ImGuiCol_ChildBg] = ImVec4(17 / 255.0f, 19 / 255.0f, 27 / 255.0f, 0.70f);
    c[ImGuiCol_PopupBg] = ImVec4(14 / 255.0f, 16 / 255.0f, 24 / 255.0f, 0.96f);
    c[ImGuiCol_Border] = ImVec4(50 / 255.0f, 60 / 255.0f, 95 / 255.0f, 0.40f);

    c[ImGuiCol_FrameBg] = ImVec4(22 / 255.0f, 24 / 255.0f, 34 / 255.0f, 0.60f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(32 / 255.0f, 38 / 255.0f, 56 / 255.0f, 0.70f);
    c[ImGuiCol_FrameBgActive] = ImVec4(40 / 255.0f, 48 / 255.0f, 72 / 255.0f, 0.80f);

    c[ImGuiCol_Text] = ImVec4(235 / 255.0f, 238 / 255.0f, 245 / 255.0f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(95 / 255.0f, 100 / 255.0f, 118 / 255.0f, 1.0f);

    c[ImGuiCol_Button] = ImVec4(25 / 255.0f, 30 / 255.0f, 48 / 255.0f, 0.70f);
    c[ImGuiCol_ButtonHovered] = ImVec4(74 / 255.0f, 118 / 255.0f, 255 / 255.0f, 0.40f);
    c[ImGuiCol_ButtonActive] = ImVec4(74 / 255.0f, 118 / 255.0f, 255 / 255.0f, 0.60f);

    c[ImGuiCol_Header] = ImVec4(22 / 255.0f, 24 / 255.0f, 34 / 255.0f, 0.50f);
    c[ImGuiCol_HeaderHovered] = ImVec4(74 / 255.0f, 118 / 255.0f, 255 / 255.0f, 0.25f);
    c[ImGuiCol_HeaderActive] = ImVec4(74 / 255.0f, 118 / 255.0f, 255 / 255.0f, 0.40f);
}

// ============================================================
// SPLASH SCREEN (ANIMACIÓN MEJORADA)
// ============================================================
void RenderSplash() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 size = ImGui::GetIO().DisplaySize;

    dl->AddRectFilledMultiColor(ImVec2(0, 0), size,
        IM_COL32(9, 10, 15, 255), IM_COL32(12, 14, 22, 255),
        IM_COL32(20, 16, 32, 255), IM_COL32(9, 10, 15, 255));

    DrawStars(dl, size);

    ImVec2 center(size.x * 0.5f, size.y * 0.36f);
    float pulse = 0.85f + 0.15f * sinf(g_Time * 1.5f);

    dl->AddCircleFilled(center, 90.0f * pulse, IM_COL32(74, 118, 255, 12), 64);
    dl->AddCircleFilled(center, 60.0f * pulse, IM_COL32(120, 90, 255, 20), 64);

    int segments = 40;
    float ringRadius = 42.0f;
    for (int i = 0; i < segments; i++) {
        float a1 = (float)i / segments * 6.28318f + g_Time * 1.2f;
        float a2 = (float)(i + 1) / segments * 6.28318f + g_Time * 1.2f;
        float alpha = (sinf(a1 * 2.0f) * 0.5f + 0.5f);
        dl->AddLine(
            ImVec2(center.x + cosf(a1) * ringRadius, center.y + sinf(a1) * ringRadius),
            ImVec2(center.x + cosf(a2) * ringRadius, center.y + sinf(a2) * ringRadius),
            IM_COL32(80, 150, 255, (int)(alpha * 200)), 2.0f);
    }

    float ring2Radius = 32.0f;
    for (int i = 0; i < segments; i++) {
        float a1 = (float)i / segments * 6.28318f - g_Time * 1.8f;
        float a2 = (float)(i + 1) / segments * 6.28318f - g_Time * 1.8f;
        float alpha = (cosf(a1 * 3.0f) * 0.5f + 0.5f);
        dl->AddLine(
            ImVec2(center.x + cosf(a1) * ring2Radius, center.y + sinf(a1) * ring2Radius),
            ImVec2(center.x + cosf(a2) * ring2Radius, center.y + sinf(a2) * ring2Radius),
            IM_COL32(160, 90, 255, (int)(alpha * 220)), 1.8f);
    }

    ImVec2 boxSize(24.0f, 24.0f);
    ImVec2 p0(center.x - boxSize.x, center.y - boxSize.y);
    ImVec2 p1(center.x + boxSize.x, center.y + boxSize.y);

    dl->AddRectFilledMultiColor(p0, p1,
        IM_COL32(85, 130, 255, 240), IM_COL32(125, 90, 255, 240),
        IM_COL32(50, 80, 220, 240), IM_COL32(85, 130, 255, 240));
    dl->AddRect(p0, p1, IM_COL32(180, 210, 255, 180), 12.0f, 0, 1.5f);

    ImGui::PushFont(g_FontBig);
    ImVec2 tN = ImGui::CalcTextSize("N");
    dl->AddText(g_FontBig, 24.0f, ImVec2(center.x - tN.x * 0.5f, center.y - tN.y * 0.5f),
        IM_COL32(255, 255, 255, 255), "N");
    ImGui::PopFont();

    ImGui::PushFont(g_FontTitle);
    const char* title = "N E X U S";
    ImVec2 tt = ImGui::CalcTextSize(title);
    dl->AddText(g_FontTitle, 20.0f, ImVec2((size.x - tt.x) * 0.5f, size.y * 0.58f),
        IM_COL32(240, 245, 255, 255), title);
    ImGui::PopFont();

    ImGui::PushFont(g_FontRegular);
    const char* sub = "Inicializando componentes...";
    if (g_LoadProgress > 0.35f) sub = "Analizando lista de procesos...";
    if (g_LoadProgress > 0.75f) sub = "Preparando interfaz de usuario...";

    ImVec2 ts = ImGui::CalcTextSize(sub);
    dl->AddText(g_FontRegular, 13.0f, ImVec2((size.x - ts.x) * 0.5f, size.y * 0.67f),
        IM_COL32(140, 150, 175, 255), sub);
    ImGui::PopFont();

    float barW = size.x * 0.68f;
    float barX = (size.x - barW) * 0.5f;
    float barY = size.y * 0.79f;
    float barH = 4.0f;

    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), IM_COL32(25, 30, 45, 255), 3.0f);

    float smoothedProgress = EaseOutCubic(g_LoadProgress);
    float fillW = barW * smoothedProgress;

    if (fillW > 2.0f) {
        dl->AddRectFilledMultiColor(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH),
            IM_COL32(74, 118, 255, 255), IM_COL32(170, 100, 255, 255),
            IM_COL32(170, 100, 255, 255), IM_COL32(74, 118, 255, 255));

        float shineX = barX + fillW;
        dl->AddCircleFilled(ImVec2(shineX, barY + barH * 0.5f), 6.0f, IM_COL32(200, 220, 255, 255));
        dl->AddCircleFilled(ImVec2(shineX, barY + barH * 0.5f), 10.0f, IM_COL32(140, 170, 255, 90));
    }

    dl->AddText(g_FontRegular, 11.0f, ImVec2(size.x - 85, size.y - 20),
        IM_COL32(95, 100, 120, 180), "v1.0.0");
}

// ------------------------------------------------------------
// Nombre del proceso auxiliar
// ------------------------------------------------------------
const char* GetProcessNameCStr(int index) {
    static std::string name;
    if (index >= 0 && index < (int)g_Processes.size()) {
        name = WStringToString(g_Processes[index].exeName);
        return name.c_str();
    }
    return "";
}

// ============================================================
// DIBUJAR VISTA PRINCIPAL
// ============================================================
void DrawTitleBar() {
    ImVec2 size = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(size.x, (float)kTitleBarH));
    ImGui::Begin("##titlebar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPos(ImVec2(12, 8));
    ImVec2 cp = ImGui::GetCursorScreenPos();
    ImDrawList* wdl = ImGui::GetWindowDrawList();
    wdl->AddRectFilled(cp, ImVec2(cp.x + 22, cp.y + 22), IM_COL32(74, 118, 255, 255), 7.0f);
    ImGui::PushFont(g_FontMedium);
    ImVec2 tN = ImGui::CalcTextSize("N");
    wdl->AddText(ImVec2(cp.x + 11 - tN.x * 0.5f, cp.y + 11 - tN.y * 0.5f), IM_COL32(255, 255, 255, 255), "N");
    ImGui::PopFont();

    ImGui::SetCursorPos(ImVec2(42, 9));
    ImGui::BeginGroup();
    ImGui::PushFont(g_FontMedium);
    ImGui::TextColored(ImVec4(0.91f, 0.92f, 0.94f, 1.0f), "NEXUS");
    ImGui::PopFont();
    ImGui::TextColored(ImVec4(0.47f, 0.62f, 1.0f, 1.0f), "Process Manager");
    ImGui::EndGroup();

    float btnW = 36.0f, btnH = (float)kTitleBarH;
    ImGui::SetCursorPos(ImVec2(size.x - btnW * 3, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.10f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

    if (ImGui::Button("-##min", ImVec2(btnW, btnH))) ShowWindow(g_hwnd, SW_MINIMIZE);
    ImGui::SameLine(0, 0);
    if (ImGui::Button(g_IsMaximized ? "[ ]##res" : "[]##max", ImVec2(btnW, btnH))) ToggleMaximize();
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.28f, 0.32f, 1.0f));
    if (ImGui::Button("x##close", ImVec2(btnW, btnH))) PostQuitMessage(0);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    ImVec2 wp = ImGui::GetWindowPos();
    dl->AddLine(ImVec2(wp.x, wp.y + kTitleBarH), ImVec2(wp.x + size.x, wp.y + kTitleBarH),
        IM_COL32(36, 39, 53, 100), 1.0f);

    ImGui::End();
}

void DrawProcessList(ImVec2 pos, ImVec2 sz) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(sz);
    ImGui::Begin("##process_list", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::BeginChild("##list_inner", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    ImGui::PushFont(g_FontMedium);
    ImGui::TextUnformatted("Procesos");
    ImGui::PopFont();
    ImGui::SameLine(sz.x - 32 - 12);
    ImGui::TextColored(ImVec4(0.37f, 0.39f, 0.46f, 1.0f), "%zu", g_Processes.size());

    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.086f, 0.094f, 0.133f, 0.60f));
    ImGui::SetNextItemWidth(sz.x - 32);
    ImGui::InputTextWithHint("##search", "Buscar...", g_SearchBuf, IM_ARRAYSIZE(g_SearchBuf));
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.039f, 0.059f, 0.50f));
    ImGui::BeginChild("##selstate", ImVec2(0, 28), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(4);
    if (g_SelectedIndex < 0)
        ImGui::TextColored(ImVec4(0.37f, 0.39f, 0.46f, 1.0f), "  Ningun proceso seleccionado");
    else
        ImGui::TextColored(ImVec4(0.47f, 0.62f, 1.0f, 1.0f), "  %s", GetProcessNameCStr(g_SelectedIndex));
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 6));

    ImGui::BeginChild("##processes", ImVec2(0, 0), false);

    std::string searchLower(g_SearchBuf);
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    int displayedCount = 0;
    for (int i = 0; i < (int)g_Processes.size(); i++) {
        std::string exeName = WStringToString(g_Processes[i].exeName);
        std::string exeLower = exeName;
        std::transform(exeLower.begin(), exeLower.end(), exeLower.begin(), ::tolower);

        if (!g_SearchBuf[0] || exeLower.find(searchLower) != std::string::npos) {
            ImGui::PushID(i);

            bool selected = (g_SelectedIndex == i);
            ImGui::SetCursorPosX(2);

            if (g_Processes[i].icon) {
                ImGui::Image((ImTextureID)g_Processes[i].icon, ImVec2(18, 18));
            }
            else {
                ImGui::Dummy(ImVec2(18, 18));
            }
            ImGui::SameLine();

            if (ImGui::Selectable(("##" + std::to_string(i)).c_str(), selected, 0, ImVec2(sz.x - 30, 26))) {
                g_SelectedIndex = i;
            }
            ImGui::SameLine(24);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
            ImGui::Text("%s", exeName.c_str());
            ImGui::SameLine(sz.x - 65);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
            ImGui::TextColored(ImVec4(0.37f, 0.39f, 0.46f, 1.0f), "%u", g_Processes[i].pid);

            ImGui::PopID();
            displayedCount++;
        }
    }

    if (displayedCount == 0) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(avail.x * 0.5f - 50, avail.y * 0.5f - 15));
        ImGui::TextColored(ImVec4(0.37f, 0.39f, 0.46f, 1.0f), "No hay procesos");
    }

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();
}

// ============================================================
// DrawProcessDetails - CON BOTONES DE DISCORD Y WEB
// ============================================================
void DrawProcessDetails(ImVec2 pos, ImVec2 sz) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(sz);
    ImGui::Begin("##details", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::BeginChild("##details_inner", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    ImGui::PushFont(g_FontMedium);
    ImGui::TextUnformatted("Detalles");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.039f, 0.059f, 0.50f));
    ImGui::BeginChild("##card", ImVec2(0, 70), true);

    if (g_SelectedIndex >= 0 && g_SelectedIndex < (int)g_Processes.size()) {
        const auto& proc = g_Processes[g_SelectedIndex];

        ImGui::SetCursorPos(ImVec2(10, 10));
        if (proc.icon) {
            ImGui::Image((ImTextureID)proc.icon, ImVec2(36, 36));
        }
        else {
            ImGui::Dummy(ImVec2(36, 36));
        }
        ImGui::SameLine();

        ImGui::SetCursorPos(ImVec2(54, 12));
        std::string exeName = WStringToString(proc.exeName);
        ImGui::TextColored(ImVec4(0.91f, 0.92f, 0.94f, 1.0f), "%s", exeName.c_str());
        ImGui::TextColored(ImVec4(0.37f, 0.39f, 0.46f, 1.0f), "PID: %u", proc.pid);
    }
    else {
        ImGui::SetCursorPos(ImVec2(10, 10));
        ImGui::TextColored(ImVec4(0.37f, 0.39f, 0.46f, 1.0f), "Ningun proceso seleccionado");
        ImGui::TextColored(ImVec4(0.27f, 0.29f, 0.36f, 1.0f), "Selecciona uno de la lista");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 6));

    if (g_SelectedIndex >= 0 && g_SelectedIndex < (int)g_Processes.size()) {
        const auto& proc = g_Processes[g_SelectedIndex];
        std::string path = WStringToString(proc.fullPath);
        if (!path.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.039f, 0.059f, 0.50f));
            ImGui::BeginChild("##path", ImVec2(0, 28), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPosY(4);
            ImGui::TextColored(ImVec4(0.47f, 0.49f, 0.56f, 1.0f), "Ruta: %s", path.c_str());
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 6));
        }
    }

    // ========================================
    // FILA DE BOTONES COMPACTA EN UNA SOLA LÍNEA
    // ========================================
    float availWidth = ImGui::GetContentRegionAvail().x;
    float gap = 4.0f;
    float btnWidth = (availWidth - (gap * 2.0f)) / 3.0f;
    bool hasSelection = (g_SelectedIndex >= 0 && g_SelectedIndex < (int)g_Processes.size());

    if (!hasSelection) ImGui::BeginDisabled();

    if (ImGui::Button("Ubicacion", ImVec2(btnWidth, 28))) {
        if (hasSelection && !g_Processes[g_SelectedIndex].fullPath.empty()) {
            OpenFileLocation(g_Processes[g_SelectedIndex].fullPath);
        }
        else {
            MessageBoxA(g_hwnd, "No se pudo encontrar la ruta del proceso", "Error", MB_OK);
        }
    }

    ImGui::SameLine(0, gap);

    if (ImGui::Button("Inyectar", ImVec2(btnWidth, 28))) {
        if (hasSelection) {
            g_TargetPid = g_Processes[g_SelectedIndex].pid;
            g_ShowInjectionDialog = true;
            strcpy_s(g_DllPath, "");
            strcpy_s(g_DllPathDisplay, "Ningun DLL seleccionado");
            g_InjectionStatus = "";
            g_InjectionSuccess = false;
        }
    }

    if (!hasSelection) ImGui::EndDisabled();

    ImGui::SameLine(0, gap);

    if (ImGui::Button("Refrescar", ImVec2(btnWidth, 28))) {
        RefreshProcesses();
    }

    // ========================================
    // SEPARADOR
    // ========================================
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));

    // ========================================
    // SECCIÓN DE ENLACES
    // ========================================
    ImGui::PushFont(g_FontSmall);
    ImGui::TextColored(ImVec4(0.47f, 0.49f, 0.56f, 1.0f), "Enlaces oficiales");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4));

    // Botón Discord - Estilo morado
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.25f, 0.65f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.35f, 0.80f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("Discord Oficial", ImVec2(-1, 28))) {
        OpenURL("https://discord.gg/XBSPRBAXeJ");
    }
    ImGui::PopStyleColor(3);

    ImGui::Dummy(ImVec2(0, 4));

    // Botón Web - Estilo azul
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.80f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.90f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("Pagina Web", ImVec2(-1, 28))) {
        OpenURL("https://proyect-nexus.vercel.app/");
    }
    ImGui::PopStyleColor(3);

    ImGui::EndChild();
    ImGui::End();
}

void DrawStatusBar(ImVec2 pos, ImVec2 sz) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(sz);
    ImGui::Begin("##status", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::SetCursorPos(ImVec2(10, sz.y * 0.5f - 6));

    if (!IsAdmin()) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "SIN PERMISOS ADMIN");
    }
    else {
        ImU32 dot = g_SelectedIndex < 0 ? IM_COL32(95, 100, 118, 255) : IM_COL32(74, 118, 255, 255);
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cp.x + 4, cp.y + 6), 4.0f, dot);
        ImGui::SetCursorPos(ImVec2(20, sz.y * 0.5f - 8));
        ImGui::TextColored(ImVec4(0.47f, 0.49f, 0.56f, 1.0f),
            g_SelectedIndex < 0 ? "Ningun proceso seleccionado" : "Proceso seleccionado");
    }

    const char* ver = "Nexus v1.0";
    ImVec2 vs = ImGui::CalcTextSize(ver);
    ImGui::SetCursorPos(ImVec2(sz.x - vs.x - 10, sz.y * 0.5f - 8));
    ImGui::TextColored(ImVec4(0.37f, 0.39f, 0.46f, 1.0f), "%s", ver);

    ImGui::End();
}

// ============================================================
// INJECTION DIALOG - DISEÑO MODERNO Y ELEGANTE
// ============================================================
void RenderInjectionDialog() {
    if (!g_ShowInjectionDialog) return;

    ImGui::OpenPopup("Inyeccion DLL");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440, 360));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 16.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(14 / 255.0f, 16 / 255.0f, 24 / 255.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(70 / 255.0f, 110 / 255.0f, 230 / 255.0f, 0.45f));

    if (ImGui::BeginPopupModal("Inyeccion DLL", &g_ShowInjectionDialog,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar)) {

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        dl->AddRectFilledMultiColor(
            winPos, ImVec2(winPos.x + winSize.x, winPos.y + 45),
            IM_COL32(22, 28, 48, 255), IM_COL32(35, 28, 58, 255),
            IM_COL32(18, 22, 36, 255), IM_COL32(18, 22, 36, 255)
        );
        dl->AddLine(
            ImVec2(winPos.x, winPos.y + 45), ImVec2(winPos.x + winSize.x, winPos.y + 45),
            IM_COL32(60, 80, 140, 100), 1.0f
        );

        ImGui::SetCursorPos(ImVec2(16, 12));
        ImGui::PushFont(g_FontTitle);
        ImGui::TextColored(ImVec4(0.92f, 0.94f, 1.0f, 1.0f), "Inyección de DLL");
        ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(winSize.x - 32, 10));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.4f));
        if (ImGui::Button("X##close_modal", ImVec2(24, 24))) {
            g_ShowInjectionDialog = false;
        }
        ImGui::PopStyleColor(2);

        ImGui::SetCursorPosY(56);

        // --- TARJETA 1: PROCESO OBJETIVO ---
        ImGui::PushFont(g_FontMedium);
        ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.85f, 1.0f), "Proceso Objetivo");
        ImGui::PopFont();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(22 / 255.0f, 26 / 255.0f, 40 / 255.0f, 0.70f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::BeginChild("##target_card", ImVec2(-1, 44), true);
        ImGui::SetCursorPosY(10);
        ImGui::SetCursorPosX(12);

        std::wstring procName = GetProcessNameByPID(g_TargetPid);
        std::string procNameStr = WStringToString(procName);

        ImVec2 dotPos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dotPos.x + 4, dotPos.y + 7), 4.0f, IM_COL32(60, 220, 120, 255));
        ImGui::SetCursorPosX(24);

        ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "%s", procNameStr.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.65f, 1.0f), "(PID: %u)", g_TargetPid);

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 6));

        // --- TARJETA 2: ARCHIVO DLL ---
        ImGui::PushFont(g_FontMedium);
        ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.85f, 1.0f), "Módulo DLL");
        ImGui::PopFont();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(22 / 255.0f, 26 / 255.0f, 40 / 255.0f, 0.70f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::BeginChild("##dll_card", ImVec2(-1, 44), true);
        ImGui::SetCursorPosY(8);
        ImGui::SetCursorPosX(10);

        bool hasDll = (strlen(g_DllPath) > 0);

        if (hasDll) {
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "[DLL]");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.90f, 0.92f, 0.98f, 1.0f), "%s", g_DllPathDisplay);
        }
        else {
            ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.72f, 1.0f), "[ -- ]  Sin DLL seleccionado");
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 8));

        // --- ACCIONES ---
        float availW = ImGui::GetContentRegionAvail().x;
        float btnW = (availW - 10) * 0.5f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(32 / 255.0f, 38 / 255.0f, 58 / 255.0f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(48 / 255.0f, 58 / 255.0f, 88 / 255.0f, 1.0f));
        if (ImGui::Button("Buscar DLL...", ImVec2(btnW, 32))) {
            OPENFILENAMEA ofn = {};
            char fileName[MAX_PATH] = "";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g_hwnd;
            ofn.lpstrFilter = "Archivos DLL (*.dll)\0*.dll\0Todos los archivos (*.*)\0*.*\0";
            ofn.lpstrFile = fileName;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle = "Seleccionar DLL para Inyectar";

            if (GetOpenFileNameA(&ofn)) {
                char* ext = strrchr(fileName, '.');
                if (ext && _stricmp(ext, ".dll") == 0) {
                    strcpy_s(g_DllPath, fileName);
                    char* shortName = strrchr(fileName, '\\');
                    strcpy_s(g_DllPathDisplay, shortName ? shortName + 1 : fileName);
                    g_InjectionStatus = "";
                    g_InjectionSuccess = false;
                }
                else {
                    g_InjectionStatus = "Error: El archivo debe ser .dll";
                    g_InjectionSuccess = false;
                }
            }
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine(0, 10);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(65 / 255.0f, 105 / 255.0f, 245 / 255.0f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(85 / 255.0f, 130 / 255.0f, 255 / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(45 / 255.0f, 85 / 255.0f, 215 / 255.0f, 1.0f));

        if (ImGui::Button("INYECTAR", ImVec2(btnW, 32))) {
            if (strlen(g_DllPath) > 0) {
                InjectDLL(g_TargetPid, g_DllPath);
            }
            else {
                g_InjectionStatus = "Selecciona un archivo DLL primero";
                g_InjectionSuccess = false;
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::Dummy(ImVec2(0, 8));

        if (!g_InjectionStatus.empty()) {
            ImVec4 bgCol = g_InjectionSuccess ? ImVec4(15 / 255.0f, 45 / 255.0f, 25 / 255.0f, 0.8f) : ImVec4(50 / 255.0f, 20 / 255.0f, 20 / 255.0f, 0.8f);
            ImVec4 borderCol = g_InjectionSuccess ? ImVec4(40 / 255.0f, 180 / 255.0f, 80 / 255.0f, 0.6f) : ImVec4(220 / 255.0f, 70 / 255.0f, 70 / 255.0f, 0.6f);

            ImGui::PushStyleColor(ImGuiCol_ChildBg, bgCol);
            ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

            ImGui::BeginChild("##status_banner", ImVec2(-1, 34), true);
            ImGui::SetCursorPosY(8);
            ImGui::SetCursorPosX(10);

            ImVec4 txtCol = g_InjectionSuccess ? ImVec4(0.4f, 0.95f, 0.55f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f);
            ImGui::TextColored(txtCol, "%s %s", g_InjectionSuccess ? "[OK]" : "[!]", g_InjectionStatus.c_str());

            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// ============================================================
// RENDER MAIN
// ============================================================
void RenderMain() {
    ImVec2 size = ImGui::GetIO().DisplaySize;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size,
        IM_COL32(9, 10, 15, 255), IM_COL32(9, 10, 15, 255),
        IM_COL32(15, 12, 25, 255), IM_COL32(15, 12, 25, 255));
    DrawStars(dl, size);

    DrawTitleBar();

    const float statusH = 28.0f;
    const float pad = 10.0f;
    ImVec2 bodyPos(pad, kTitleBarH + pad);
    ImVec2 bodySize(size.x - pad * 2, size.y - kTitleBarH - statusH - pad * 2);

    float leftW = bodySize.x * 0.58f;
    float gap = 8.0f;

    DrawProcessList(bodyPos, ImVec2(leftW, bodySize.y));
    DrawProcessDetails(ImVec2(bodyPos.x + leftW + gap, bodyPos.y), ImVec2(bodySize.x - leftW - gap, bodySize.y));
    DrawStatusBar(ImVec2(0, size.y - statusH), ImVec2(size.x, statusH));

    RenderInjectionDialog();
}

// ============================================================
// UTILIDADES DE VENTANA
// ============================================================
void CenterWindow(HWND hwnd, int w, int h) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfo(mon, &mi);
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER);
}

void ToggleMaximize() {
    if (!g_IsMaximized) {
        GetWindowRect(g_hwnd, &g_RestoreRect);
        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfo(mon, &mi);
        SetWindowPos(g_hwnd, nullptr, mi.rcWork.left, mi.rcWork.top,
            mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top, SWP_NOZORDER);
        g_IsMaximized = true;
    }
    else {
        SetWindowPos(g_hwnd, nullptr, g_RestoreRect.left, g_RestoreRect.top,
            g_RestoreRect.right - g_RestoreRect.left, g_RestoreRect.bottom - g_RestoreRect.top, SWP_NOZORDER);
        g_IsMaximized = false;
    }
}

// ============================================================
// D3D11 SETUP
// ============================================================
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// ============================================================
// WNDPROC
// ============================================================
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_NCHITTEST: {
        if (g_State == AppState::Main) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            RECT rc; GetClientRect(hWnd, &rc);
            const int buttonsWidth = 36 * 3;
            if (pt.y >= 0 && pt.y < kTitleBarH && pt.x < (rc.right - buttonsWidth))
                return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}