#define _USE_MATH_DEFINES
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d3d11.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include <string>
#include <vector>
#include <algorithm>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "hook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shell32.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ─── D3D OBJECTS ─────────────────────────────────────────────────────────────
static ID3D11Device*           gDev  = nullptr;
static ID3D11DeviceContext*    gCtx  = nullptr;
static IDXGISwapChain*         gSwap = nullptr;
static ID3D11RenderTargetView* gRtv  = nullptr;

static void MakeRtv() {
    ID3D11Texture2D* bb = nullptr;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (bb) { gDev->CreateRenderTargetView(bb, nullptr, &gRtv); bb->Release(); }
}
static void DropRtv() { if (gRtv) { gRtv->Release(); gRtv = nullptr; } }

static bool MakeDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = {60,1};
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL lvl[] = {D3D_FEATURE_LEVEL_11_0}, got;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, lvl, 1, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, &got, &gCtx)))
        return false;
    MakeRtv(); return true;
}
static void DropDevice() {
    DropRtv();
    if (gSwap) { gSwap->Release(); gSwap = nullptr; }
    if (gCtx)  { gCtx->Release();  gCtx  = nullptr; }
    if (gDev)  { gDev->Release();  gDev  = nullptr; }
}

// ─── WINDOW PROC ─────────────────────────────────────────────────────────────
static LRESULT WINAPI WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) {
        LRESULT hit = DefWindowProc(h, m, w, l);
        if (hit != HTCLIENT) return hit;
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &p);
        if (p.y >= 0 && p.y < 36) return HTCAPTION;
        return HTCLIENT;
    }
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    switch (m) {
    case WM_SIZE:
        if (gDev && w != SIZE_MINIMIZED) {
            DropRtv();
            gSwap->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
            MakeRtv();
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

// ══════════════════════════════════════════════════════════════════════════════
//  DYNAMIC INTERACTIVE PARTICLE & SPARK SYSTEM
// ══════════════════════════════════════════════════════════════════════════════

struct Particle {
    float x, y;           // position
    float vx, vy;         // velocity
    float life, maxLife;  // life duration
    float size;           // radius
    ImU32 color;          // base color
};

struct ClickSpark {
    float x, y;
    float vx, vy;
    float life, maxLife;
    float size;
    ImU32 color;
};

static constexpr int  PMAX = 110;
static constexpr int  SMAX = 64;
static Particle       gParts[PMAX] = {};
static ClickSpark     gSparks[SMAX] = {};
static bool           gPartsInit   = false;
static DWORD          gLastTick    = 0;
static float          gAnimTime    = 0.0f;

static void PSpawn(Particle& p, float W, float H) {
    p.x       = ((float)(rand()%10000)/10000.0f) * W;
    p.y       = H + 6.0f;
    p.vx      = (((float)(rand()%200))-100.0f) * 0.0010f;
    p.vy      = -(0.035f + ((float)(rand()%90))*0.0012f);
    p.life    = 0.0f;
    p.maxLife = 3500.0f + (float)(rand()%4500);
    p.size    = 1.2f + ((float)(rand()%30))*0.1f;
    int t = rand()%6;
    switch (t) {
        case 0: p.color = IM_COL32(160, 60, 255, 220); break; // violet
        case 1: p.color = IM_COL32(40, 200, 255, 210); break; // cyan
        case 2: p.color = IM_COL32(240, 80, 255, 200); break; // magenta
        case 3: p.color = IM_COL32(255, 255, 255, 160); break;// star
        case 4: p.color = IM_COL32(80, 240, 190, 180); break; // neon mint
        case 5: p.color = IM_COL32(255, 160, 50, 190); break; // warm gold
    }
}

static void PInit(float W, float H) {
    srand(GetTickCount());
    for (int i = 0; i < PMAX; ++i) {
        PSpawn(gParts[i], W, H);
        gParts[i].y    = ((float)(rand()%10000)/10000.0f) * H;
        gParts[i].life = ((float)(rand()%10000)/10000.0f) * gParts[i].maxLife;
    }
    for (int i = 0; i < SMAX; ++i) gSparks[i].life = 99999.0f;
    gPartsInit = true;
    gLastTick  = GetTickCount();
}

static void SpawnClickSparks(float mx, float my) {
    int count = 14 + (rand() % 8);
    int spawned = 0;
    for (int i = 0; i < SMAX && spawned < count; ++i) {
        if (gSparks[i].life >= gSparks[i].maxLife) {
            float ang = ((float)(rand()%3600)/3600.0f) * 2.0f * (float)M_PI;
            float spd = 0.15f + ((float)(rand()%100)/100.0f) * 0.40f;
            gSparks[i].x = mx;
            gSparks[i].y = my;
            gSparks[i].vx = cosf(ang) * spd;
            gSparks[i].vy = sinf(ang) * spd;
            gSparks[i].life = 0.0f;
            gSparks[i].maxLife = 300.0f + (float)(rand()%350);
            gSparks[i].size = 1.5f + ((float)(rand()%25)/10.0f);
            int colType = rand()%4;
            if (colType == 0) gSparks[i].color = IM_COL32(255, 100, 255, 255);
            else if (colType == 1) gSparks[i].color = IM_COL32(80, 230, 255, 255);
            else if (colType == 2) gSparks[i].color = IM_COL32(255, 230, 90, 255);
            else gSparks[i].color = IM_COL32(255, 255, 255, 255);
            spawned++;
        }
    }
}

static void PUpdate(float W, float H, ImVec2 mousePos, float audioVU) {
    DWORD now = GetTickCount();
    float dt  = gLastTick ? (float)(now - gLastTick) : 16.0f;
    if (dt > 100.0f) dt = 100.0f;
    gLastTick = now;
    gAnimTime += dt * 0.001f;

    float audioBoost = 1.0f + audioVU * 4.0f;

    // Update floating background particles
    for (int i = 0; i < PMAX; ++i) {
        Particle& p = gParts[i];
        p.x    += p.vx * dt * audioBoost;
        p.y    += p.vy * dt * audioBoost;
        p.life += dt;
        p.vx   += (((float)(rand()%100)-50.0f) * 0.000006f);

        // Interactive mouse repulsion force field
        float dx = p.x - mousePos.x;
        float dy = p.y - mousePos.y;
        float distSq = dx*dx + dy*dy;
        if (distSq < 14400.0f && distSq > 0.01f) { // 120px radius
            float dist = sqrtf(distSq);
            float force = (120.0f - dist) / 120.0f * 0.08f;
            p.vx += (dx / dist) * force;
            p.vy += (dy / dist) * force;
        }

        if (p.life >= p.maxLife || p.y < -12.0f || p.x < -12.0f || p.x > W+12.0f) {
            PSpawn(p, W, H);
        }
    }

    // Update click sparks
    for (int i = 0; i < SMAX; ++i) {
        ClickSpark& s = gSparks[i];
        if (s.life < s.maxLife) {
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.vy += 0.0008f * dt; // gravity
            s.vx *= 0.96f;       // drag
            s.vy *= 0.96f;
            s.life += dt;
        }
    }
}

static void PDraw(ImDrawList* dl, float W, float H, float audioVU) {
    // Draw background particles
    for (int i = 0; i < PMAX; ++i) {
        Particle& p = gParts[i];
        if (p.y < -12.0f || p.y > H+12.0f) continue;

        float t = p.life / p.maxLife;
        float alpha = (t < 0.15f) ? (t/0.15f) :
                      (t > 0.85f) ? ((1.0f-t)/0.15f) : 1.0f;

        ImU32 col = p.color;
        int   ca  = (int)(((col >> 24) & 0xFF) * alpha);
        col = (col & 0x00FFFFFF) | ((ImU32)ca << 24);

        float sz = p.size * (1.0f + audioVU * 0.6f);
        dl->AddCircleFilled(ImVec2(p.x, p.y), sz, col, 8);

        if (sz > 1.8f && ca > 50) {
            int ga = ca / 4;
            ImU32 glow = (col & 0x00FFFFFF) | ((ImU32)ga << 24);
            dl->AddCircleFilled(ImVec2(p.x, p.y), sz * 2.6f, glow, 8);
        }
    }

    // Draw click spark explosion particles
    for (int i = 0; i < SMAX; ++i) {
        ClickSpark& s = gSparks[i];
        if (s.life < s.maxLife) {
            float t = s.life / s.maxLife;
            float alpha = 1.0f - t;
            int   ca = (int)(255.0f * alpha);
            ImU32 col = (s.color & 0x00FFFFFF) | ((ImU32)ca << 24);

            dl->AddCircleFilled(ImVec2(s.x, s.y), s.size * (1.0f - t*0.5f), col, 8);
            // Motion tail line
            dl->AddLine(ImVec2(s.x, s.y), ImVec2(s.x - s.vx*6.0f, s.y - s.vy*6.0f), col, 1.5f);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  STYLE & THEME
// ══════════════════════════════════════════════════════════════════════════════

static void Style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding      = ImVec2(12, 10);
    s.FramePadding       = ImVec2(8, 5);
    s.ItemSpacing        = ImVec2(7, 6);
    s.ItemInnerSpacing   = ImVec2(5, 4);
    s.IndentSpacing      = 16.0f;
    s.ScrollbarSize      = 10.0f;
    s.GrabMinSize        = 8.0f;
    s.WindowRounding     = 6.0f;
    s.FrameRounding      = 4.0f;
    s.GrabRounding       = 3.0f;
    s.TabRounding        = 4.0f;
    s.PopupRounding      = 4.0f;
    s.ScrollbarRounding  = 4.0f;
    s.WindowBorderSize   = 1.0f;
    s.FrameBorderSize    = 0.0f;
    s.TabBorderSize      = 0.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]              = ImVec4(0.010f, 0.010f, 0.016f, 1.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(0.015f, 0.015f, 0.024f, 1.00f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.018f, 0.018f, 0.028f, 1.00f);
    c[ImGuiCol_Border]                = ImVec4(0.25f,  0.08f,  0.42f,  0.50f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f,  0.00f,  0.00f,  0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(0.06f,  0.06f,  0.10f,  1.00f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.12f,  0.10f,  0.20f,  1.00f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.18f,  0.14f,  0.28f,  1.00f);
    c[ImGuiCol_Button]                = ImVec4(0.09f,  0.09f,  0.15f,  1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.48f,  0.14f,  0.80f,  1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.65f,  0.25f,  0.95f,  1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(0.60f,  0.18f,  0.92f,  1.00f);
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.75f,  0.32f,  1.00f,  1.00f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.78f,  0.35f,  1.00f,  1.00f);
    c[ImGuiCol_Header]                = ImVec4(0.35f,  0.10f,  0.60f,  0.55f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.52f,  0.16f,  0.82f,  0.75f);
    c[ImGuiCol_HeaderActive]          = ImVec4(0.65f,  0.25f,  0.95f,  1.00f);
    c[ImGuiCol_Tab]                   = ImVec4(0.04f,  0.04f,  0.08f,  1.00f);
    c[ImGuiCol_TabHovered]            = ImVec4(0.46f,  0.12f,  0.76f,  0.85f);
    c[ImGuiCol_TabActive]             = ImVec4(0.38f,  0.10f,  0.65f,  1.00f);
    c[ImGuiCol_TabUnfocused]          = ImVec4(0.04f,  0.04f,  0.07f,  1.00f);
    c[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.20f,  0.06f,  0.34f,  1.00f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.010f, 0.010f, 0.016f, 1.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.010f, 0.010f, 0.016f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.010f, 0.010f, 0.016f, 1.00f);
    c[ImGuiCol_Separator]             = ImVec4(0.25f,  0.08f,  0.42f,  0.55f);
    c[ImGuiCol_SeparatorHovered]      = ImVec4(0.55f,  0.18f,  0.85f,  0.70f);
    c[ImGuiCol_SeparatorActive]       = ImVec4(0.70f,  0.28f,  1.00f,  1.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.010f, 0.010f, 0.016f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.32f,  0.09f,  0.54f,  1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.48f,  0.14f,  0.76f,  1.00f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.64f,  0.24f,  0.94f,  1.00f);
    c[ImGuiCol_Text]                  = ImVec4(0.90f,  0.92f,  0.95f,  1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.42f,  0.44f,  0.50f,  1.00f);
}

// ══════════════════════════════════════════════════════════════════════════════
//  HELPERS & CUSTOM ANIMATED CONTROLS
// ══════════════════════════════════════════════════════════════════════════════

static void PinToDiscord(HWND self, int W, int H) {
    struct Ctx { HWND self; DWORD pid; HWND best; long area; };
    Ctx ctx {self, GetCurrentProcessId(), nullptr, 0};
    EnumWindows([](HWND hh, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        if (hh == c->self) return TRUE;
        DWORD pid=0; GetWindowThreadProcessId(hh, &pid);
        if (pid!=c->pid || !IsWindowVisible(hh) || GetWindow(hh,GW_OWNER)) return TRUE;
        RECT r; if (!GetWindowRect(hh, &r)) return TRUE;
        long a = (r.right-r.left)*(long)(r.bottom-r.top);
        if (a > c->area) {c->area=a; c->best=hh;}
        return TRUE;
    }, (LPARAM)&ctx);

    int x=120, y=120;
    RECT r;
    if (ctx.best && GetWindowRect(ctx.best,&r) &&
        (r.right-r.left)>W && (r.bottom-r.top)>H) {
        x = r.right-W-24; y = r.top+24;
    }
    SetWindowPos(self, HWND_TOPMOST, x, y, W, H, SWP_NOACTIVATE);
}

static void Shell(const wchar_t* cmd) {
    std::wstring args = std::wstring(L"cmd.exe /c ") + cmd;
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    CreateProcessW(nullptr, &args[0], nullptr, nullptr, FALSE,
                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread)  CloseHandle(pi.hThread);
}

static void ShellURL(const wchar_t* url) {
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

// ─── ANIMATED TOGGLE SWITCH ──────────────────────────────────────────────────
static bool AnimatedToggleSwitch(const char* label, volatile bool* flag, ImVec2 size = ImVec2(44, 22)) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 total_size = ImVec2(size.x + (label_size.x > 0 ? label_size.x + 8.0f : 0.0f), size.y);

    char idBuf[128];
    snprintf(idBuf, sizeof(idBuf), "##tgl_%s", label);
    bool clicked = ImGui::InvisibleButton(idBuf, total_size);
    bool hovered = ImGui::IsItemHovered();

    if (clicked) *flag = !(*flag);

    bool state = *flag;
    float t = state ? 1.0f : 0.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float radius = size.y * 0.5f;

    ImU32 bg_col = state ?
        (hovered ? IM_COL32(160, 40, 240, 255) : IM_COL32(130, 30, 200, 255)) :
        (hovered ? IM_COL32(50, 45, 70, 255) : IM_COL32(32, 30, 45, 255));

    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg_col, radius);

    if (state) {
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(220, 100, 255, 180), radius, 1.5f, 0);
    }

    float knob_radius = radius - 3.0f;
    float knob_x = pos.x + 3.0f + knob_radius + t * (size.x - 6.0f - knob_radius * 2.0f);
    float knob_y = pos.y + radius;

    ImU32 knob_col = state ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 180, 255);
    dl->AddCircleFilled(ImVec2(knob_x, knob_y), knob_radius, knob_col, 16);

    if (label_size.x > 0) {
        dl->AddText(ImVec2(pos.x + size.x + 8.0f, pos.y + (size.y - label_size.y) * 0.5f),
                    IM_COL32(220, 220, 230, 255), label);
    }

    if (clicked) {
        ImVec2 mpos = ImGui::GetMousePos();
        SpawnClickSparks(mpos.x, mpos.y);
    }

    return clicked;
}

// ─── ACCENT BUTTON WITH RIPPLE / HOVER GLOW ──────────────────────────────────
static bool AccentButton(const char* label, ImVec2 sz = ImVec2(0,0), bool active = false) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.50f,0.14f,0.85f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f,0.22f,0.95f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.75f,0.32f,1.00f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.0f,1.0f,1.0f,1.0f));
    }
    bool r = ImGui::Button(label, sz);
    if (active) ImGui::PopStyleColor(4);

    if (r) {
        ImVec2 mpos = ImGui::GetMousePos();
        SpawnClickSparks(mpos.x, mpos.y);
    }
    return r;
}

static bool ToggleButton(const char* labelOn, const char* labelOff,
                         volatile bool* flag, ImVec2 sz = ImVec2(-1,0)) {
    bool v = *flag;
    if (v) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.48f,0.12f,0.78f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f,0.20f,0.90f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.70f,0.30f,1.00f,1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.08f,0.08f,0.13f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f,0.10f,0.32f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.30f,0.12f,0.48f,1.0f));
    }
    bool clicked = ImGui::Button(v ? labelOn : labelOff, sz);
    ImGui::PopStyleColor(3);
    if (clicked) {
        *flag = !v;
        ImVec2 mpos = ImGui::GetMousePos();
        SpawnClickSparks(mpos.x, mpos.y);
    }
    return clicked;
}

// ─── ENHANCED MULTI-SEGMENT LED VU METER WITH GRAVITY PEAK DROP ──────────────
static void DrawVUBar(ImDrawList* dl, ImVec2 pos, float W, float H,
                      float level, float spoofDB, float& peakHold, DWORD& peakTime) {
    // Outer background panel
    dl->AddRectFilled(pos, ImVec2(pos.x+W, pos.y+H), IM_COL32(14,12,22,240), 3.0f);
    dl->AddRect(pos, ImVec2(pos.x+W, pos.y+H), IM_COL32(70,30,110,180), 3.0f);

    float clampedLvl = level > 1.0f ? 1.0f : (level < 0.0f ? 0.0f : level);
    
    // Segmented LED blocks
    constexpr int segCount = 20;
    float segW = (W - (segCount + 1) * 1.5f) / segCount;
    int litSegs = (int)(clampedLvl * segCount + 0.5f);

    for (int i = 0; i < segCount; ++i) {
        float x0 = pos.x + 1.5f + i * (segW + 1.5f);
        float x1 = x0 + segW;
        float y0 = pos.y + 2.0f;
        float y1 = pos.y + H - 2.0f;

        float ratio = (float)i / segCount;
        ImU32 colLit, colUnlit;
        if (ratio < 0.60f) {
            colLit   = IM_COL32(40, 230, 110, 240); // vibrant green
            colUnlit = IM_COL32(12, 40, 22, 140);
        } else if (ratio < 0.85f) {
            colLit   = IM_COL32(245, 210, 30, 240); // vibrant yellow/amber
            colUnlit = IM_COL32(40, 35, 10, 140);
        } else {
            colLit   = IM_COL32(255, 50, 50, 255);  // vibrant red
            colUnlit = IM_COL32(45, 12, 12, 140);
        }

        if (i < litSegs) {
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colLit, 1.0f);
        } else {
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colUnlit, 1.0f);
        }
    }

    // Peak Hold Line with smooth gravity drop
    DWORD now = GetTickCount();
    if (clampedLvl >= peakHold) {
        peakHold = clampedLvl;
        peakTime = now;
    } else {
        if (now - peakTime > 400) {
            peakHold -= 0.018f;
            if (peakHold < 0.0f) peakHold = 0.0f;
        }
    }

    if (peakHold > 0.02f) {
        float px = pos.x + 1.5f + peakHold * (W - 3.0f);
        dl->AddLine(ImVec2(px, pos.y + 1.0f), ImVec2(px, pos.y + H - 1.0f),
                    IM_COL32(255, 255, 255, 255), 2.0f);
    }

    // Decibel label overlay
    float db = (level > 0.00001f) ? 20.0f*log10f(level) : -99.0f;
    if (spoofDB != 0.0f) db = spoofDB;
    char dbStr[32]; snprintf(dbStr, sizeof dbStr, "%.1f dB", db);
    ImVec2 ts = ImGui::CalcTextSize(dbStr);
    dl->AddText(ImVec2(pos.x + W*0.5f - ts.x*0.5f, pos.y + H*0.5f - ts.y*0.5f),
                IM_COL32(240,240,250,255), dbStr);
}

// ─── DYNAMIC AUDIO OSCILLOSCOPE WAVEFORM ──────────────────────────────────────
static void DrawAudioOscilloscope(ImDrawList* dl, ImVec2 pos, float W, float H, float audioVU) {
    dl->AddRectFilled(pos, ImVec2(pos.x+W, pos.y+H), IM_COL32(10, 8, 16, 200), 4.0f);
    dl->AddRect(pos, ImVec2(pos.x+W, pos.y+H), IM_COL32(50, 20, 80, 160), 4.0f);

    float centerY = pos.y + H * 0.5f;
    constexpr int points = 60;
    float dx = W / (points - 1);

    std::vector<ImVec2> polyPoints;
    polyPoints.reserve(points);

    float t = gAnimTime * 12.0f;
    float amp = (H * 0.40f) * (0.15f + audioVU * 2.8f);
    if (amp > H*0.45f) amp = H*0.45f;

    for (int i = 0; i < points; ++i) {
        float x = pos.x + i * dx;
        float normX = (float)i / points;
        float wave1 = sinf(normX * 12.5f + t);
        float wave2 = sinf(normX * 25.0f - t * 1.4f) * 0.5f;
        float noise = ((rand() % 100) - 50) * 0.01f * audioVU;
        float y = centerY + (wave1 + wave2 + noise) * amp;
        polyPoints.push_back(ImVec2(x, y));
    }

    // Multi-pass neon glow line
    ImU32 glowCol = IM_COL32(160, 40, 255, 90);
    ImU32 lineCol = IM_COL32(70, 220, 255, 240);

    dl->AddPolyline(polyPoints.data(), (int)polyPoints.size(), glowCol, 4.5f, 0);
    dl->AddPolyline(polyPoints.data(), (int)polyPoints.size(), lineCol, 1.8f, 0);
}

// ─── CONFIG SAVE/LOAD ────────────────────────────────────────────────────────
static wchar_t gCfgPath[MAX_PATH] = {};

static void BuildCfgPath() {
    if (gCfgPath[0]) return;
    wchar_t mod[MAX_PATH]={};
    GetModuleFileNameW(GetModuleHandleW(L"hookdll.dll"), mod, MAX_PATH);
    wchar_t* last = wcsrchr(mod, L'\\');
    if (last) wcscpy_s(last+1, MAX_PATH-(last-mod)-1, L"hookdll.cfg");
    wcscpy_s(gCfgPath, MAX_PATH, mod);
}

static void SaveConfig() {
    BuildCfgPath();
    FILE* f=nullptr; _wfopen_s(&f, gCfgPath, L"w");
    if (!f) return;
#define WF(k,v) fprintf(f, k "=%g\n", (double)(v))
#define WI(k,v) fprintf(f, k "=%d\n", (int)(v))
    WF("gain",           g_gain);         WF("rage",    g_rage_gain);
    WI("kbps",           g_target_kbps);  WF("pitch",   g_pitch_semitones);
    WF("haas_ms",        g_haas_delay_ms);WF("haas_d",  g_haas_depth);
    WF("sw",             g_stereo_width); WF("sg",      g_stereo_side_gain);
    WF("echo_ms",        g_echo_delay_ms);WF("echo_fb", g_echo_feedback);
    WF("echo_lvl",       g_echo_level);
    WF("eq_b",g_eq_bass);WF("eq_lm",g_eq_lowmid);WF("eq_m",g_eq_mid);
    WF("eq_hm",g_eq_highmid);WF("eq_t",g_eq_treble);
    WI("rev_en",g_reverb_enable);WF("rev_rs",g_reverb_room_size);
    WF("rev_da",g_reverb_damping);WF("rev_we",g_reverb_wet);
    WF("rev_dr",g_reverb_dry);WF("rev_wi",g_reverb_width);
    WF("rev_pd",g_reverb_predelay);
    WI("comp_en",g_comp_enable);WF("comp_th",g_comp_threshold);
    WF("comp_ra",g_comp_ratio);WF("comp_at",g_comp_attack);
    WI("wid_en",g_widener_enable);WF("wid_w",g_widener_width);
    WF("pan",g_pan);WF("dist",g_distortion);WF("bc",g_bit_crusher);
    WF("delay",g_delay_ms);WF("wider",g_wider);
    WF("ch_ra",g_chorus_rate);WF("ch_de",g_chorus_depth);WF("ch_mi",g_chorus_mix);
    WF("wn",g_white_noise);WF("pn",g_pink_noise);WF("bn",g_brown_noise);WF("gn",g_green_noise);
    WI("fps",g_fps_limit);
#undef WF
#undef WI
    fclose(f);
}

static void LoadConfig() {
    BuildCfgPath();
    FILE* f=nullptr; _wfopen_s(&f, gCfgPath, L"r");
    if (!f) return;
    char line[128]; float fv; int iv;
    while (fgets(line, sizeof line, f)) {
#define RF(k,g) if (sscanf_s(line, k "=%f", &fv)==1) { g = fv; continue; }
#define RI(k,g) if (sscanf_s(line, k "=%d", &iv)==1) { g = iv; continue; }
        RF("gain",g_gain) RF("rage",g_rage_gain) RF("pitch",g_pitch_semitones)
        RF("haas_ms",g_haas_delay_ms) RF("haas_d",g_haas_depth)
        RF("sw",g_stereo_width) RF("sg",g_stereo_side_gain)
        RF("echo_ms",g_echo_delay_ms) RF("echo_fb",g_echo_feedback) RF("echo_lvl",g_echo_level)
        RF("eq_b",g_eq_bass) RF("eq_lm",g_eq_lowmid) RF("eq_m",g_eq_mid)
        RF("eq_hm",g_eq_highmid) RF("eq_t",g_eq_treble)
        RF("rev_rs",g_reverb_room_size) RF("rev_da",g_reverb_damping)
        RF("rev_we",g_reverb_wet) RF("rev_dr",g_reverb_dry)
        RF("rev_wi",g_reverb_width) RF("rev_pd",g_reverb_predelay)
        RF("comp_th",g_comp_threshold) RF("comp_ra",g_comp_ratio) RF("comp_at",g_comp_attack)
        RF("wid_w",g_widener_width) RF("pan",g_pan) RF("dist",g_distortion)
        RF("bc",g_bit_crusher) RF("delay",g_delay_ms) RF("wider",g_wider)
        RF("ch_ra",g_chorus_rate) RF("ch_de",g_chorus_depth) RF("ch_mi",g_chorus_mix)
        RF("wn",g_white_noise) RF("pn",g_pink_noise) RF("bn",g_brown_noise) RF("gn",g_green_noise)
        RI("kbps",g_target_kbps) RI("rev_en",g_reverb_enable)
        RI("comp_en",g_comp_enable) RI("wid_en",g_widener_enable)
        RI("fps",g_fps_limit)
#undef RF
#undef RI
    }
    fclose(f);
}

// ══════════════════════════════════════════════════════════════════════════════
//  RUN GUI
// ══════════════════════════════════════════════════════════════════════════════

void RunGui() {
    constexpr int W = 540, H = 840;

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
                      GetModuleHandle(nullptr), nullptr, nullptr,
                      nullptr, nullptr, L"VaraxxCls", nullptr};
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        wc.lpszClassName, L"Varaxx", WS_POPUP,
        120, 120, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { UnregisterClassW(wc.lpszClassName, wc.hInstance); return; }
    SetLayeredWindowAttributes(hwnd, 0, 240, LWA_ALPHA);

    if (!MakeDevice(hwnd)) {
        DropDevice(); DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance); return;
    }

    PinToDiscord(hwnd, W, H);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    Style();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gDev, gCtx);

    // Local state
    static int   inputKbps    = 128;
    static bool  streamerMode = false;
    static bool  visible      = true;
    static float vuSmooth     = 0.0f;
    static float vuPeak       = 0.0f;
    static DWORD vuPeakTime   = 0;

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Hide hotkey
        if (GetAsyncKeyState(g_hide_hotkey) & 1) {
            visible = !visible;
            if (visible) { PinToDiscord(hwnd,W,H); ShowWindow(hwnd,SW_SHOW); }
            else ShowWindow(hwnd, SW_HIDE);
        }
        if (!visible) { Sleep(16); continue; }

        // FPS cap
        int fps = (int)g_fps_limit;
        if (fps < 15) fps = 15; if (fps > 165) fps = 165;

        // Dynamic smoothed VU calculation
        float rawVU = (float)g_vunits;
        vuSmooth = vuSmooth * 0.78f + rawVU * 0.22f;

        // Mouse click spark trigger
        if (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) {
            ImVec2 m = ImGui::GetMousePos();
            SpawnClickSparks(m.x, m.y);
        }

        // Particle update with interactive mouse repulsion & audio pulse
        if (g_use_particles) {
            if (!gPartsInit) PInit((float)W, (float)H);
            PUpdate((float)W, (float)H, io.MousePos, vuSmooth);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos  = ImGui::GetWindowPos();
        float  fw = io.DisplaySize.x, fh = io.DisplaySize.y;

        // ── CHROMATIC NEON CYCLING BORDER ──────────────────────────────────────
        float t = gAnimTime * 0.8f;
        ImU32 neonCol1 = ImColor::HSV(fmodf(t*0.15f, 1.0f), 0.90f, 1.0f);
        ImU32 neonCol2 = ImColor::HSV(fmodf(t*0.15f + 0.33f, 1.0f), 0.90f, 1.0f);
        ImU32 neonCol3 = ImColor::HSV(fmodf(t*0.15f + 0.66f, 1.0f), 0.90f, 1.0f);

        // Top flowing gradient line
        dl->AddRectFilledMultiColor(winPos, ImVec2(winPos.x+fw, winPos.y+4),
            neonCol1, neonCol2, neonCol2, neonCol1);

        // Outer pulsing neon frame
        dl->AddRect(winPos, ImVec2(winPos.x+fw, winPos.y+fh),
                    neonCol1, 4.0f, 1.5f, 0);

        // Render interactive particle canvas behind UI
        if (g_use_particles && gPartsInit) {
            PDraw(dl, fw, fh, vuSmooth);
        }

        // ════════════════════════════════════════════════════════════════════
        //  TITLE BAR WITH CHROMATIC ABERRATION & AUDIO-REACTIVE VU BAR
        // ════════════════════════════════════════════════════════════════════
        ImGui::SetCursorPos(ImVec2(14, 8));
        
        // Chromatic offset text effect
        ImVec2 titlePos = ImGui::GetCursorScreenPos();
        dl->AddText(ImVec2(titlePos.x-1.2f, titlePos.y), IM_COL32(255, 0, 120, 180), "Varaxx");
        dl->AddText(ImVec2(titlePos.x+1.2f, titlePos.y), IM_COL32(0, 220, 255, 180), "Varaxx");
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.92f, 1.0f, 1.0f));
        ImGui::Text("Varaxx");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 6);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.60f, 1.0f));
        ImGui::Text("v2.5 Pro");
        ImGui::PopStyleColor();

        // Multi-segment LED VU Bar in title
        {
            float vuBarW = 160.0f, vuBarH = 16.0f;
            float vuBarX = fw * 0.5f - vuBarW * 0.5f;
            float vuBarY = 10.0f;
            DrawVUBar(dl,
                ImVec2(winPos.x + vuBarX, winPos.y + vuBarY),
                vuBarW, vuBarH,
                vuSmooth * 8.0f,
                (float)g_spoof_db, vuPeak, vuPeakTime);
        }

        // Close button
        ImGui::SetCursorPos(ImVec2(fw - 34.0f, 7.0f));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f,0.05f,0.05f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f,0.10f,0.10f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f,0.20f,0.20f,1.0f));
        if (ImGui::Button("X##xx", ImVec2(26, 20))) done = true;
        ImGui::PopStyleColor(3);

        // Divider
        ImGui::SetCursorPos(ImVec2(0, 32));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.40f,0.10f,0.70f,0.80f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        // ════════════════════════════════════════════════════════════════════
        //  REAL-TIME AUDIO OSCILLOSCOPE WAVEFORM STRIP
        // ════════════════════════════════════════════════════════════════════
        {
            ImVec2 oscPos = ImVec2(winPos.x + 12.0f, winPos.y + 36.0f);
            DrawAudioOscilloscope(dl, oscPos, fw - 24.0f, 28.0f, vuSmooth);
        }

        // ════════════════════════════════════════════════════════════════════
        //  TAB BAR
        // ════════════════════════════════════════════════════════════════════
        ImGui::SetCursorPosY(70);
        if (ImGui::BeginTabBar("##TB", ImGuiTabBarFlags_FittingPolicyScroll)) {

            // ================================================================
            //  TAB: MAIN
            // ================================================================
            if (ImGui::BeginTabItem(" Main ")) {
                ImGui::BeginChild("##main_scroll", ImVec2(0,0), false);
                ImGui::Spacing();

                // ── Gain ─────────────────────────────────────────────────────
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f,0.30f,1.0f,1.0f));
                ImGui::SeparatorText(" GAIN ");
                ImGui::PopStyleColor();

                float gain = g_gain;
                float dB = 20.0f*log10f(gain>0.0001f?gain:0.0001f);
                ImGui::TextDisabled("%.2fx  |  %+.1f dB", gain, dB);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##gn", &gain, 1.0f, 300.0f,
                                       "%.2fx", ImGuiSliderFlags_Logarithmic))
                    g_gain = gain;
                {
                    float bw = (ImGui::GetContentRegionAvail().x - 56.0f)/8.0f;
                    const float gp[] = {1,10,30,50,100,200,300};
                    const char* gl[] = {"1x","10x","30x","50x","100x","200x","300x"};
                    for (int i=0;i<7;++i) {
                        bool act = fabsf((float)g_gain-(float)gp[i])<0.5f;
                        if (AccentButton(gl[i], ImVec2(bw,0), act)) g_gain=gp[i];
                        if (i<6) ImGui::SameLine();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("RST##g", ImVec2(bw,0))) g_gain=10.0f;
                }
                ImGui::Spacing();

                // ── Rage Gain ─────────────────────────────────────────────────
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.35f,0.35f,1.0f));
                ImGui::SeparatorText(" RAGE GAIN ");
                ImGui::PopStyleColor();
                float rage = g_rage_gain;
                ImGui::TextDisabled("Multiplier on top of gain: %.2fx", rage);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##rg", &rage, 1.0f, 30.0f,
                                       "%.2fx", ImGuiSliderFlags_Logarithmic))
                    g_rage_gain = rage;
                {
                    float bw = (ImGui::GetContentRegionAvail().x-16.0f)/3.0f;
                    if (AccentButton("OFF##rg",ImVec2(bw,0), fabsf((float)g_rage_gain-1.0f)<0.1f)) g_rage_gain=1.0f;
                    ImGui::SameLine();
                    if (AccentButton("5x##rg", ImVec2(bw,0), fabsf((float)g_rage_gain-5.0f)<0.1f)) g_rage_gain=5.0f;
                    ImGui::SameLine();
                    if (AccentButton("10x##rg",ImVec2(bw,0), fabsf((float)g_rage_gain-10.0f)<0.1f)) g_rage_gain=10.0f;
                }
                ImGui::Spacing();

                // ── GainFade ──────────────────────────────────────────────────
                ImGui::SeparatorText(" GAINFADE ");
                {
                    float tgt = g_gainfade_target, spd = g_gainfade_speed;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##gft",&tgt,1.0f,300.0f,"Target: %.1fx",ImGuiSliderFlags_Logarithmic))
                        g_gainfade_target = tgt;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##gfs",&spd,0.0f,0.15f,"Speed: %.4f"))
                        g_gainfade_speed = spd;
                    float bw = (ImGui::GetContentRegionAvail().x-8.0f)/2.0f;
                    if (AccentButton("FADE ON##gf",ImVec2(bw,0), g_gainfade_speed > 0.001f))  { g_gainfade_speed=0.04f; }
                    ImGui::SameLine();
                    if (AccentButton("FADE OFF##gf",ImVec2(bw,0), g_gainfade_speed <= 0.001f)) { g_gainfade_speed=0.0f;  }
                }
                ImGui::Spacing();

                // ── StereioFade ───────────────────────────────────────────────
                ImGui::SeparatorText(" STEREOFADE ");
                {
                    float tgt = g_stereofade_target, spd = g_stereofade_speed;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##sft",&tgt,0.0f,30.0f,"Width Target: %.2fx"))
                        g_stereofade_target = tgt;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##sfs",&spd,0.0f,0.15f,"Speed: %.4f"))
                        g_stereofade_speed = spd;
                    float bw = (ImGui::GetContentRegionAvail().x-8.0f)/2.0f;
                    if (AccentButton("FADE ON##sf",ImVec2(bw,0), g_stereofade_speed > 0.001f))  { g_stereofade_speed=0.04f; }
                    ImGui::SameLine();
                    if (AccentButton("FADE OFF##sf",ImVec2(bw,0), g_stereofade_speed <= 0.001f)) { g_stereofade_speed=0.0f;  }
                }
                ImGui::Spacing();

                // ── Bitrate ───────────────────────────────────────────────────
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f,0.85f,0.65f,1.0f));
                ImGui::SeparatorText(" OPUS BITRATE ");
                ImGui::PopStyleColor();
                {
                    int cur = g_target_kbps;
                    if (cur <= 0) ImGui::TextDisabled("Mode: Auto (Discord default)");
                    else if (cur >= 1000) ImGui::TextDisabled("Active: %.2f Mbps", (float)cur/1000.0f);
                    else ImGui::TextDisabled("Active: %d kbps", cur);

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-68.0f);
                    ImGui::InputInt("##kb", &inputKbps, 64, 512);
                    ImGui::SameLine();
                    if (AccentButton("SET##kb", ImVec2(60,0))) {
                        if (inputKbps < 8)    inputKbps = 8;
                        if (inputKbps > 4000) inputKbps = 4000;
                        g_target_kbps = inputKbps;
                    }
                    const int kv[] = {128,320,510,1000,2000,4000,0};
                    const char* kl[]= {"128k","320k","510k","1M","2M","4M","Auto"};
                    float kw = (ImGui::GetContentRegionAvail().x-48.0f)/7.0f;
                    for (int i=0;i<7;++i) {
                        bool act = (kv[i]==g_target_kbps);
                        if (AccentButton(kl[i], ImVec2(kw,0), act)) {
                            inputKbps = (kv[i]>0)?kv[i]:64; g_target_kbps=kv[i];
                        }
                        if (i<6) ImGui::SameLine();
                    }
                }
                ImGui::Spacing();

                // ── ExploitDB + SpoofDB + VUnits ─────────────────────────────
                ImGui::SeparatorText(" EXPLOIT / METERS ");
                {
                    bool ex = g_exploit_db;
                    if (AnimatedToggleSwitch("ExploitDB (NOP dB cap in discord_voice.node)", &ex))
                        g_exploit_db = ex;
                }
                ImGui::Spacing();
                {
                    float sp = g_spoof_db;
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-60.0f);
                    if (ImGui::SliderFloat("##spdb",&sp,-60.0f,0.0f,"Spoof dB: %.1f"))
                        g_spoof_db = sp;
                    ImGui::SameLine();
                    if (AccentButton("Real##sp",ImVec2(52,0))) g_spoof_db=0.0f;
                }
                ImGui::TextDisabled("VU: %.5f  |  Energy: %.5f  |  Pkts: %d",
                    (float)g_vunits, (float)g_energy, (int)g_packets);

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // ================================================================
            //  TAB: AUDIO
            // ================================================================
            if (ImGui::BeginTabItem(" Audio ")) {
                ImGui::BeginChild("##aud_scroll", ImVec2(0,0), false);
                ImGui::Spacing();

                ImGui::SeparatorText(" PROCESSING FLAGS ");
                bool v;
                v=g_int16_limiter;   if (AnimatedToggleSwitch("Int16 Soft Limiter (tanh)",&v))       g_int16_limiter=v;
                v=g_enable_clipping; if (AnimatedToggleSwitch("Enable Hard Clipping (bypass limiter)",&v)) g_enable_clipping=v;
                v=g_high_pass_filter;if (AnimatedToggleSwitch("High Pass Filter (80 Hz cut)",&v))    g_high_pass_filter=v;
                v=g_above_head;      if (AnimatedToggleSwitch("Above Head (HRTF wide preset)",&v))   g_above_head=v;
                v=g_in_head;         if (AnimatedToggleSwitch("In Head (mono center collapse)",&v))  g_in_head=v;

                ImGui::Spacing();
                ImGui::SeparatorText(" GATE / AGC / VAD ");
                {
                    float bw=(ImGui::GetContentRegionAvail().x-16.0f)/3.0f;
                    ToggleButton("GATE ON","GATE OFF",&g_noise_gate_enable,ImVec2(bw,0));
                    ImGui::SameLine();
                    ToggleButton("AGC ON","AGC OFF",&g_agc_enable,ImVec2(bw,0));
                    ImGui::SameLine();
                    ToggleButton("VAD ON","VAD OFF",&g_vad_enable,ImVec2(bw,0));
                }

                float gate=g_noise_gate_threshold;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##gth",&gate,0.0f,1.0f,"Gate Threshold: %.3f")) g_noise_gate_threshold=gate;

                float gamt=g_gate_amount;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##gam",&gamt,0.0f,1.0f,"Gate Amount: %.3f")) g_gate_amount=gamt;

                float agcAmt=g_agc_amount;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##agca",&agcAmt,0.0f,1.0f,"AGC Target Level: %.3f")) g_agc_amount=agcAmt;

                float vadTh=g_vad_threshold;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##vadt",&vadTh,0.0f,0.5f,"VAD Threshold: %.4f")) g_vad_threshold=vadTh;

                ImGui::Spacing();
                ImGui::SeparatorText(" CODEC PARAMETERS ");
                {
                    int pac=g_pacsize;
                    if (ImGui::InputInt("Pacsize (0=auto)##ps",&pac,120,960)) {
                        if (pac<0) pac=0; g_pacsize=pac;
                    }
                    int pay=g_audio_payload;
                    if (ImGui::InputInt("Audio Payload##ap",&pay,1,10)) {
                        if (pay<96) pay=96; if (pay>127) pay=127; g_audio_payload=pay;
                    }
                    int cmp=g_complexity;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderInt("##cmp",&cmp,0,10,"Complexity: %d")) g_complexity=cmp;
                    int lsb=g_lsb_depth;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderInt("##lsb",&lsb,8,24,"LSB Depth: %d")) g_lsb_depth=lsb;
                    const char* eMeth[]={"SILK","HYBRID","CELT"};
                    int em=g_encoding_method;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo("Encoding Method",&em,eMeth,3)) g_encoding_method=em;
                    const char* aApp[]={"VOIP","AUDIO","LOW DELAY"};
                    int aa=g_audio_application;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo("Audio Application",&aa,aApp,3)) g_audio_application=aa;
                    float vr=g_voice_ratio;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##vr",&vr,0.0f,1.0f,"Voice Ratio: %.2f")) g_voice_ratio=vr;
                    const char* bwN[]={"Narrow","Medium","Wide","SuperWide","Full"};
                    int bw=g_bandwidth;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo("Bandwidth",&bw,bwN,5)) g_bandwidth=bw;
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // ================================================================
            //  TAB: EQ / FX (WITH INTERACTIVE SPLINE CURVE VISUALIZER)
            // ================================================================
            if (ImGui::BeginTabItem(" EQ/FX ")) {
                ImGui::BeginChild("##eq_scroll", ImVec2(0,0), false);
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f,0.95f,0.45f,1.0f));
                ImGui::SeparatorText(" 5-BAND EQUALIZER ");
                ImGui::PopStyleColor();

                volatile float* bgs[] = {&g_eq_bass,&g_eq_lowmid,&g_eq_mid,&g_eq_highmid,&g_eq_treble};
                const char* bln[] = {"Bass  80Hz","LowMid 250Hz","Mids 1kHz","HiMid 4kHz","Treble 12kHz"};
                const char* bid[] = {"##eqb","##eqlm","##eqm","##eqhm","##eqt"};
                for (int i=0;i<5;++i) {
                    float g2=(float)*bgs[i];
                    ImGui::SetNextItemWidth(-1.0f);
                    char fmt[64]; snprintf(fmt,sizeof fmt,"%s: %%+.1f dB",bln[i]);
                    if (ImGui::SliderFloat(bid[i],&g2,-12.0f,12.0f,fmt)) *bgs[i]=g2;
                }
                {
                    float bw=(ImGui::GetContentRegionAvail().x-16.0f)/3.0f;
                    if (AccentButton("Flat##eq",ImVec2(bw,0))) {
                        g_eq_bass=g_eq_lowmid=g_eq_mid=g_eq_highmid=g_eq_treble=0.0f;
                    } ImGui::SameLine();
                    if (AccentButton("Boost HF##eq",ImVec2(bw,0))) {
                        g_eq_highmid=5.0f; g_eq_treble=8.0f;
                    } ImGui::SameLine();
                    if (AccentButton("Boost LF##eq",ImVec2(bw,0))) {
                        g_eq_bass=8.0f; g_eq_lowmid=4.0f;
                    }
                }
                bool eqvis=g_show_eq_vis;
                if (AnimatedToggleSwitch("Show Interactive EQ Spline Visualizer",&eqvis)) g_show_eq_vis=eqvis;

                if (eqvis) {
                    float vals[5]={(float)g_eq_bass,(float)g_eq_lowmid,(float)g_eq_mid,
                                   (float)g_eq_highmid,(float)g_eq_treble};
                    const char* labs[5]={"80Hz","250Hz","1kHz","4kHz","12kHz"};
                    
                    float eqBoxW = ImGui::GetContentRegionAvail().x;
                    float eqBoxH = 90.0f;
                    ImVec2 curPos = ImGui::GetCursorScreenPos();
                    ImDrawList* ddl = ImGui::GetWindowDrawList();

                    // Background panel with grid lines
                    ddl->AddRectFilled(curPos, ImVec2(curPos.x + eqBoxW, curPos.y + eqBoxH), IM_COL32(10, 8, 18, 230), 6.0f);
                    ddl->AddRect(curPos, ImVec2(curPos.x + eqBoxW, curPos.y + eqBoxH), IM_COL32(50, 30, 80, 160), 6.0f);

                    // 0 dB center line
                    float zeroY = curPos.y + eqBoxH * 0.5f;
                    ddl->AddLine(ImVec2(curPos.x + 8, zeroY), ImVec2(curPos.x + eqBoxW - 8, zeroY), IM_COL32(80, 80, 110, 120), 1.0f);

                    // Calculate 5 control point positions
                    ImVec2 nodePts[5];
                    float nodeStep = (eqBoxW - 40.0f) / 4.0f;
                    for (int i = 0; i < 5; ++i) {
                        float nx = curPos.x + 20.0f + i * nodeStep;
                        // Map -12dB..+12dB to eqBoxH..0
                        float normVal = (vals[i] + 12.0f) / 24.0f;
                        float ny = curPos.y + eqBoxH - 10.0f - normVal * (eqBoxH - 20.0f);
                        nodePts[i] = ImVec2(nx, ny);
                    }

                    // Render smooth spline curve using fine sub-segments
                    std::vector<ImVec2> curvePts;
                    constexpr int subDivs = 16;
                    for (int i = 0; i < 4; ++i) {
                        ImVec2 p0 = (i > 0) ? nodePts[i-1] : nodePts[i];
                        ImVec2 p1 = nodePts[i];
                        ImVec2 p2 = nodePts[i+1];
                        ImVec2 p3 = (i < 3) ? nodePts[i+2] : nodePts[i+1];

                        for (int j = 0; j < subDivs; ++j) {
                            float t = (float)j / subDivs;
                            float t2 = t * t;
                            float t3 = t2 * t;

                            // Catmull-Rom spline formulation
                            float q0 = -t3 + 2.0f*t2 - t;
                            float q1 = 3.0f*t3 - 5.0f*t2 + 2.0f;
                            float q2 = -3.0f*t3 + 4.0f*t2 + t;
                            float q3 = t3 - t2;

                            float cx = 0.5f * (p0.x * q0 + p1.x * q1 + p2.x * q2 + p3.x * q3);
                            float cy = 0.5f * (p0.y * q0 + p1.y * q1 + p2.y * q2 + p3.y * q3);
                            curvePts.push_back(ImVec2(cx, cy));
                        }
                    }
                    curvePts.push_back(nodePts[4]);

                    // Fill polygon under curve
                    std::vector<ImVec2> fillPts = curvePts;
                    fillPts.push_back(ImVec2(nodePts[4].x, zeroY));
                    fillPts.push_back(ImVec2(nodePts[0].x, zeroY));
                    ddl->AddConvexPolyFilled(fillPts.data(), (int)fillPts.size(), IM_COL32(140, 40, 220, 60));

                    // Curve multi-pass glow line
                    ddl->AddPolyline(curvePts.data(), (int)curvePts.size(), IM_COL32(180, 60, 255, 120), 4.0f, 0);
                    ddl->AddPolyline(curvePts.data(), (int)curvePts.size(), IM_COL32(80, 220, 255, 240), 2.0f, 0);

                    // Render interactive glowing node handles
                    ImVec2 mpos = ImGui::GetMousePos();
                    for (int i = 0; i < 5; ++i) {
                        float dx = mpos.x - nodePts[i].x;
                        float dy = mpos.y - nodePts[i].y;
                        bool hovered = (dx*dx + dy*dy < 120.0f);

                        ImU32 nCol = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 100, 255, 255);
                        float r = hovered ? 6.5f : 4.5f;

                        ddl->AddCircleFilled(nodePts[i], r, nCol, 12);
                        ddl->AddCircle(nodePts[i], r + 2.5f, IM_COL32(60, 200, 255, 200), 12);

                        // Frequency label below
                        ddl->AddText(ImVec2(nodePts[i].x - 12.0f, curPos.y + eqBoxH - 16.0f), IM_COL32(160, 160, 180, 220), labs[i]);
                    }

                    ImGui::Dummy(ImVec2(0, eqBoxH + 6.0f));
                }

                ImGui::Spacing();
                ImGui::SeparatorText(" EFFECTS ");

                float wider=g_wider;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##wd",&wider,0.0f,4.0f,"Wider: %.2f")) g_wider=wider;

                float pan=g_pan;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##pn",&pan,-1.0f,1.0f,"Pan L<0>R: %.2f")) g_pan=pan;
                if (AccentButton("Center Pan",ImVec2(-1,0), fabsf((float)g_pan)<0.01f)) g_pan=0.0f;

                float pitch=(float)g_pitch_semitones;
                float pfac=powf(2.0f,pitch/12.0f);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##pt",&pitch,-12.0f,12.0f,"Pitch: %+.1f semitones")) g_pitch_semitones=pitch;
                ImGui::TextDisabled("Factor: %.3fx", pfac);
                {
                    float bw=(ImGui::GetContentRegionAvail().x-32.0f)/5.0f;
                    const float pv[]={-6,-3,0,4,12};
                    const char* pl[]={"Demon","Deep","Reset","Female","Chip"};
                    for (int i=0;i<5;++i) {
                        bool act=fabsf((float)g_pitch_semitones-pv[i])<0.1f;
                        if (AccentButton(pl[i],ImVec2(bw,0),act)) g_pitch_semitones=pv[i];
                        if (i<4) ImGui::SameLine();
                    }
                }

                float dist=g_distortion;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##dt",&dist,0.0f,1.0f,"Distortion: %.3f")) g_distortion=dist;

                float bc=g_bit_crusher;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##bc",&bc,1.0f,16.0f,"Bit Crusher: %.1f bits")) g_bit_crusher=bc;

                float dms=g_delay_ms;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##dms",&dms,0.0f,2000.0f,"Delay: %.0f ms")) g_delay_ms=dms;

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // ================================================================
            //  TAB: REVERB (WITH ANIMATED 3D WIREFRAME ROOM GRAPHIC)
            // ================================================================
            if (ImGui::BeginTabItem(" Reverb ")) {
                ImGui::BeginChild("##rev_scroll", ImVec2(0,0), false);
                ImGui::Spacing();

                {
                    bool revEn = g_reverb_enable;
                    if (AnimatedToggleSwitch("ENABLE REVERB DYNAMICS", &revEn, ImVec2(52, 26)))
                        g_reverb_enable = revEn;
                }
                ImGui::Spacing();

                // ── 3D PERSPECTIVE ANIMATED ROOM BOX GRAPHIC ──────────────────
                {
                    float boxW = ImGui::GetContentRegionAvail().x;
                    float boxH = 90.0f;
                    ImVec2 curPos = ImGui::GetCursorScreenPos();
                    ImDrawList* ddl = ImGui::GetWindowDrawList();

                    ddl->AddRectFilled(curPos, ImVec2(curPos.x + boxW, curPos.y + boxH), IM_COL32(8, 8, 16, 230), 6.0f);
                    ddl->AddRect(curPos, ImVec2(curPos.x + boxW, curPos.y + boxH), IM_COL32(60, 30, 90, 160), 6.0f);

                    float cx = curPos.x + boxW * 0.5f;
                    float cy = curPos.y + boxH * 0.5f;
                    
                    float rmSize = (float)g_reverb_room_size; // 0..1
                    float sz = 18.0f + rmSize * 22.0f;
                    float rot = gAnimTime * 1.5f;

                    // 3D wireframe cube vertices projected
                    float cosR = cosf(rot), sinR = sinf(rot);
                    auto Project3D = [&](float x, float y, float z) -> ImVec2 {
                        float rx = x * cosR - z * sinR;
                        float rz = x * sinR + z * cosR + 120.0f;
                        float scale = 160.0f / rz;
                        return ImVec2(cx + rx * scale, cy + y * scale);
                    };

                    ImVec2 v[8] = {
                        Project3D(-sz, -sz, -sz), Project3D( sz, -sz, -sz),
                        Project3D( sz,  sz, -sz), Project3D(-sz,  sz, -sz),
                        Project3D(-sz, -sz,  sz), Project3D( sz, -sz,  sz),
                        Project3D( sz,  sz,  sz), Project3D(-sz,  sz,  sz)
                    };

                    ImU32 edgeCol = g_reverb_enable ? IM_COL32(180, 80, 255, 230) : IM_COL32(80, 80, 100, 120);

                    // Front & back face
                    for (int i = 0; i < 4; ++i) {
                        ddl->AddLine(v[i], v[(i+1)%4], edgeCol, 1.6f);
                        ddl->AddLine(v[i+4], v[(i+1)%4 + 4], edgeCol, 1.6f);
                        ddl->AddLine(v[i], v[i+4], edgeCol, 1.6f);
                    }

                    // Inner wet glow sphere
                    if (g_reverb_enable) {
                        float glowR = sz * 0.65f * (0.8f + (float)g_reverb_wet * 0.6f);
                        ddl->AddCircleFilled(ImVec2(cx, cy), glowR, IM_COL32(120, 40, 240, 70), 16);
                    }

                    ImGui::Dummy(ImVec2(0, boxH + 6.0f));
                }

                float rs=g_reverb_room_size;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##rrs",&rs,0.0f,1.0f,"Room Size: %.3f")) g_reverb_room_size=rs;

                float rdec=g_reverb_decay;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##rdec",&rdec,0.1f,10.0f,"Decay: %.2f s")) g_reverb_decay=rdec;

                float rpd=g_reverb_predelay;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##rpd",&rpd,0.0f,200.0f,"Pre-Delay: %.1f ms")) g_reverb_predelay=rpd;

                float rda=g_reverb_damping;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##rda",&rda,0.0f,1.0f,"Damping: %.3f")) g_reverb_damping=rda;

                float rwe=g_reverb_wet;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##rwe",&rwe,0.0f,1.0f,"Wet: %.3f")) g_reverb_wet=rwe;

                float rdr=g_reverb_dry;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##rdr",&rdr,0.0f,1.0f,"Dry: %.3f")) g_reverb_dry=rdr;

                float rwi=g_reverb_width;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##rwi",&rwi,0.0f,1.0f,"Width: %.3f")) g_reverb_width=rwi;

                ImGui::Spacing();
                {
                    float bw=(ImGui::GetContentRegionAvail().x-24.0f)/4.0f;
                    if (AccentButton("Room##r",  ImVec2(bw,0))) { g_reverb_room_size=0.45f;g_reverb_damping=0.55f;g_reverb_wet=0.30f;g_reverb_dry=0.45f;g_reverb_predelay=10.0f; } ImGui::SameLine();
                    if (AccentButton("Hall##r",  ImVec2(bw,0))) { g_reverb_room_size=0.80f;g_reverb_damping=0.30f;g_reverb_wet=0.50f;g_reverb_dry=0.30f;g_reverb_predelay=30.0f; } ImGui::SameLine();
                    if (AccentButton("Cave##r",  ImVec2(bw,0))) { g_reverb_room_size=0.90f;g_reverb_damping=0.10f;g_reverb_wet=0.70f;g_reverb_dry=0.20f;g_reverb_predelay=80.0f; } ImGui::SameLine();
                    if (AccentButton("Plate##r", ImVec2(bw,0))) { g_reverb_room_size=0.30f;g_reverb_damping=0.70f;g_reverb_wet=0.40f;g_reverb_dry=0.50f;g_reverb_predelay=5.0f;  }
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // ================================================================
            //  TAB: WIDENER (WITH DYNAMIC STEREO PHASE GONIOMETER RADAR)
            // ================================================================
            if (ImGui::BeginTabItem(" Widener ")) {
                ImGui::BeginChild("##wid_scroll", ImVec2(0,0), false);
                ImGui::Spacing();

                // ── STEREO PHASE VECTOR SCOPE (GONIOMETER RADAR) ─────────────
                {
                    float scopeW = ImGui::GetContentRegionAvail().x;
                    float scopeH = 100.0f;
                    ImVec2 curPos = ImGui::GetCursorScreenPos();
                    ImDrawList* ddl = ImGui::GetWindowDrawList();

                    ddl->AddRectFilled(curPos, ImVec2(curPos.x + scopeW, curPos.y + scopeH), IM_COL32(8, 12, 20, 230), 6.0f);
                    ddl->AddRect(curPos, ImVec2(curPos.x + scopeW, curPos.y + scopeH), IM_COL32(30, 60, 100, 160), 6.0f);

                    float cx = curPos.x + scopeW * 0.5f;
                    float cy = curPos.y + scopeH * 0.5f;

                    // Radar crosshair lines
                    ddl->AddLine(ImVec2(cx - 40, cy), ImVec2(cx + 40, cy), IM_COL32(60, 80, 120, 120), 1.0f);
                    ddl->AddLine(ImVec2(cx, cy - 40), ImVec2(cx, cy + 40), IM_COL32(60, 80, 120, 120), 1.0f);
                    ddl->AddCircle(ImVec2(cx, cy), 35.0f, IM_COL32(40, 70, 110, 120), 24);

                    // Dynamic Lissajous phase pattern based on stereo width & pan
                    float stWidth = (float)g_stereo_width * (1.0f + (float)g_haas_depth);
                    float panVal = (float)g_pan;
                    constexpr int phasePts = 45;
                    float t = gAnimTime * 10.0f;

                    std::vector<ImVec2> pts;
                    pts.reserve(phasePts);
                    for (int i = 0; i < phasePts; ++i) {
                        float a = ((float)i / phasePts) * 2.0f * (float)M_PI;
                        float L = sinf(a * 2.0f + t) * (1.0f - panVal * 0.4f);
                        float R = sinf(a * 2.0f + t * 1.2f + stWidth * 0.5f) * (1.0f + panVal * 0.4f);
                        
                        // Rotate 45 degrees for M/S representation
                        float px = (L - R) * 0.707f * (20.0f + stWidth * 4.0f);
                        float py = (L + R) * 0.707f * 25.0f;

                        pts.push_back(ImVec2(cx + px, cy - py));
                    }

                    ddl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(40, 220, 255, 230), 1.8f, ImDrawFlags_Closed);
                    ddl->AddText(ImVec2(curPos.x + 8, curPos.y + 6), IM_COL32(140, 180, 220, 220), "Stereo Phase Vector (M/S Radar)");

                    ImGui::Dummy(ImVec2(0, scopeH + 6.0f));
                }

                // ── Haas ─────────────────────────────────────────────────────
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f,0.90f,1.0f,1.0f));
                ImGui::SeparatorText(" HAAS STEREO WIDENER ");
                ImGui::PopStyleColor();

                float haasMs = (float)g_haas_delay_ms;
                float haasDp = (float)g_haas_depth;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##hms",&haasMs,0.0f,80.0f,"Delay: %.1f ms"))
                    g_haas_delay_ms = haasMs;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##hdp",&haasDp,0.0f,1.0f,"Depth (cross-feed): %.3f"))
                    g_haas_depth = haasDp;
                {
                    float bw=(ImGui::GetContentRegionAvail().x-40.0f)/6.0f;
                    if (AccentButton("OFF##h",ImVec2(bw,0),(float)g_haas_delay_ms<0.5f))
                        { g_haas_delay_ms=0.0f; g_haas_depth=0.0f; }
                    ImGui::SameLine();
                    const float hv[]={10,15,25,35,50};
                    const char* hl[]={"10ms","15ms","25ms","35ms","50ms"};
                    for (int i=0;i<5;++i) {
                        bool act=fabsf((float)g_haas_delay_ms-hv[i])<0.5f;
                        if (AccentButton(hl[i],ImVec2(bw,0),act)) {
                            g_haas_delay_ms=hv[i]; g_haas_depth=0.8f;
                        }
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();
                }

                // ── M/S Stereo ────────────────────────────────────────────────
                ImGui::SeparatorText(" MID/SIDE STEREO ");
                float sw=(float)g_stereo_width, sg=(float)g_stereo_side_gain;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##sw",&sw,0.0f,30.0f,"Stereo Width: %.2fx")) g_stereo_width=sw;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##sg",&sg,0.0f,30.0f,"Side Boost: %.2fx"))   g_stereo_side_gain=sg;
                {
                    float bw=(ImGui::GetContentRegionAvail().x-40.0f)/6.0f;
                    const float wv[]={1,5,10,20,30};
                    const char* wl[]={"1x","5x","10x","20x","30x"};
                    for (int i=0;i<5;++i) {
                        if (AccentButton(wl[i],ImVec2(bw,0),fabsf(sw-wv[i])<0.5f))
                            g_stereo_width=wv[i];
                        ImGui::SameLine();
                    }
                    if (AccentButton("Rst##ms",ImVec2(bw,0))) {g_stereo_width=1.0f;g_stereo_side_gain=1.0f;}
                }
                ImGui::Spacing();

                // ── Dedicated Widener ─────────────────────────────────────────
                ImGui::SeparatorText(" STEREO WIDENER ");
                bool widEn = g_widener_enable;
                if (AnimatedToggleSwitch("ENABLE WIDENER PROCESSOR", &widEn)) g_widener_enable = widEn;
                float ww=(float)g_widener_width;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##ww",&ww,0.0f,4.0f,"Width: %.2f")) g_widener_width=ww;

                // ── Echo ──────────────────────────────────────────────────────
                ImGui::Spacing();
                ImGui::SeparatorText(" ECHO ");
                float ems=(float)g_echo_delay_ms, efb=(float)g_echo_feedback, elvl=(float)g_echo_level;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##ems",&ems,0.0f,1000.0f,"Delay: %.0f ms")) g_echo_delay_ms=ems;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##efb",&efb,0.0f,0.90f,"Feedback: %.3f"))   g_echo_feedback=efb;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##elvl",&elvl,0.0f,2.0f,"Level: %.3f"))     g_echo_level=elvl;
                if (AccentButton("Echo OFF##e",ImVec2(-1,0))) {
                    g_echo_delay_ms=0.0f; g_echo_feedback=0.0f;
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // ================================================================
            //  TAB: COMP / CHORUS / NOISE
            // ================================================================
            if (ImGui::BeginTabItem(" Comp ")) {
                ImGui::BeginChild("##comp_scroll", ImVec2(0,0), false);
                ImGui::Spacing();

                // ── Compressor ────────────────────────────────────────────────
                bool cEn = g_comp_enable;
                if (AnimatedToggleSwitch("ENABLE COMPRESSOR", &cEn)) g_comp_enable = cEn;
                ImGui::Spacing();
                float cth=(float)g_comp_threshold;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##cth",&cth,-60.0f,0.0f,"Threshold: %.1f dBFS")) g_comp_threshold=cth;
                float cra=(float)g_comp_ratio;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##cra",&cra,1.0f,20.0f,"Ratio: %.1f:1")) g_comp_ratio=cra;
                float cat=(float)g_comp_attack;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##cat",&cat,0.1f,100.0f,"Attack: %.1f ms")) g_comp_attack=cat;
                if (AccentButton("Reset Envelope##cr",ImVec2(-1,0))) g_comp_reset=true;
                {
                    float bw=(ImGui::GetContentRegionAvail().x-16.0f)/3.0f;
                    if (AccentButton("Gentle",ImVec2(bw,0))) { g_comp_threshold=-20.0f;g_comp_ratio=2.0f; g_comp_attack=20.0f; } ImGui::SameLine();
                    if (AccentButton("Vocal", ImVec2(bw,0))) { g_comp_threshold=-18.0f;g_comp_ratio=4.0f; g_comp_attack=5.0f;  } ImGui::SameLine();
                    if (AccentButton("Slam",  ImVec2(bw,0))) { g_comp_threshold=-10.0f;g_comp_ratio=10.0f;g_comp_attack=1.0f;  }
                }

                ImGui::Spacing();
                ImGui::SeparatorText(" CHORUS ");
                float csr=(float)g_chorus_rate, csd=(float)g_chorus_depth, csm=(float)g_chorus_mix;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##csr",&csr,0.1f,10.0f,"Rate: %.2f Hz")) g_chorus_rate=csr;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##csd",&csd,0.0f,30.0f,"Depth: %.1f ms")) g_chorus_depth=csd;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##csm",&csm,0.0f,1.0f,"Mix: %.3f")) g_chorus_mix=csm;
                {
                    float bw=(ImGui::GetContentRegionAvail().x-24.0f)/4.0f;
                    if (AccentButton("OFF##ch",    ImVec2(bw,0))) { g_chorus_mix=0.0f; } ImGui::SameLine();
                    if (AccentButton("Subtle##ch", ImVec2(bw,0))) { g_chorus_rate=0.5f;g_chorus_depth=3.0f; g_chorus_mix=0.20f; } ImGui::SameLine();
                    if (AccentButton("Classic##ch",ImVec2(bw,0))) { g_chorus_rate=1.0f;g_chorus_depth=8.0f; g_chorus_mix=0.40f; } ImGui::SameLine();
                    if (AccentButton("Heavy##ch",  ImVec2(bw,0))) { g_chorus_rate=3.0f;g_chorus_depth=20.0f;g_chorus_mix=0.65f; }
                }

                ImGui::Spacing();
                ImGui::SeparatorText(" NOISE GENERATORS ");
                ImGui::TextDisabled("Inject background noise into mic signal");
                float wn=(float)g_white_noise;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##wn",&wn,0.0f,1.0f,"White Noise: %.4f")) g_white_noise=wn;
                float pn=(float)g_pink_noise;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##pn",&pn,0.0f,1.0f,"Pink Noise: %.4f"))  g_pink_noise=pn;
                float bn=(float)g_brown_noise;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##bn",&bn,0.0f,1.0f,"Brown Noise: %.4f")) g_brown_noise=bn;
                float gn=(float)g_green_noise;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##gnn",&gn,0.0f,1.0f,"Green Noise: %.4f"))g_green_noise=gn;
                if (AccentButton("Silence All##ns",ImVec2(-1,0))) {
                    g_white_noise=g_pink_noise=g_brown_noise=g_green_noise=0.0f;
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // ================================================================
            //  TAB: DECODER
            // ================================================================
            if (ImGui::BeginTabItem(" Decoder ")) {
                ImGui::BeginChild("##dec_scroll", ImVec2(0,0), false);
                ImGui::Spacing();

                bool v;
                v=g_client_detect;   if (AnimatedToggleSwitch("Client User Detection",&v))  g_client_detect=v;
                v=g_force_not_natty; if (AnimatedToggleSwitch("Force NOT NATTY",&v))         g_force_not_natty=v;
                v=g_force_frame_480; if (AnimatedToggleSwitch("Force Frame Size 480",&v))    g_force_frame_480=v;
                v=g_force_frame_5760;if (AnimatedToggleSwitch("Force Frame Size 5760",&v))   g_force_frame_5760=v;
                ImGui::Spacing();
                float anD=(float)g_analysis_delay;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##anD",&anD,0.0f,500.0f,"Analysis Delay: %.1f ms")) g_analysis_delay=anD;
                float dbT=(float)g_db_threshold;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##dbT",&dbT,-80.0f,0.0f,"dB Threshold: %.1f dBFS")) g_db_threshold=dbT;

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // ================================================================
            //  TAB: UTILS
            // ================================================================
            if (ImGui::BeginTabItem(" Utils ")) {
                ImGui::BeginChild("##util_scroll", ImVec2(0,0), false);
                ImGui::Spacing();

                ImGui::SeparatorText(" SYSTEM CLEANUP ");
                float uw=(ImGui::GetContentRegionAvail().x-8.0f)/2.0f;
                if (AccentButton("Clear Temp##u",        ImVec2(uw,0))) Shell(L"del /Q /F /S \"%TEMP%\\*\" 2>nul");
                ImGui::SameLine();
                if (AccentButton("Clear Run History##u", ImVec2(uw,0)))
                    RegDeleteKeyW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU");

                if (AccentButton("Clear Journal##u",     ImVec2(uw,0))) Shell(L"del /Q /F /S \"%SystemRoot%\\System32\\LogFiles\\*\" 2>nul");
                ImGui::SameLine();
                if (AccentButton("Clear Registry##u",    ImVec2(uw,0))) {
                    RegDeleteKeyW(HKEY_CURRENT_USER,  L"Software\\Discord");
                    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Discord");
                }
                if (AccentButton("Close TaskMgr##u",     ImVec2(uw,0))) Shell(L"taskkill /F /IM Taskmgr.exe 2>nul");
                ImGui::SameLine();
                if (AccentButton("Clear Prefetch##u",    ImVec2(uw,0))) Shell(L"del /Q /F /S \"%SystemRoot%\\Prefetch\\*\" 2>nul");

                if (AccentButton("Clear Recycle##u",     ImVec2(uw,0)))
                    SHEmptyRecycleBinW(nullptr,nullptr,SHERB_NOCONFIRMATION|SHERB_NOPROGRESSUI|SHERB_NOSOUND);
                ImGui::SameLine();
                if (AccentButton("Clear Browser Cache##u",ImVec2(uw,0))) {
                    Shell(L"del /Q /F /S \"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\Default\\Cache\\*\" 2>nul");
                    Shell(L"del /Q /F /S \"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\Default\\Cache\\*\" 2>nul");
                }

                ImGui::Spacing();
                ImGui::SeparatorText(" LINKS ");
                if (AccentButton("Join Discord##u",      ImVec2(uw,0))) ShellURL(L"https://discord.gg/varaxx");
                ImGui::SameLine();
                if (AccentButton("Download Modules##u",  ImVec2(uw,0))) ShellURL(L"https://github.com/varaxx/modules");
                if (AccentButton("Download Injector##u", ImVec2(uw,0))) ShellURL(L"https://github.com/varaxx/injector");

                ImGui::Spacing();
                ImGui::SeparatorText(" CONFIGURATION ");
                if (AccentButton("Save Config##u",       ImVec2(uw,0))) SaveConfig();
                ImGui::SameLine();
                if (AccentButton("Load Config##u",       ImVec2(uw,0))) LoadConfig();

                if (AccentButton("Hide Window##u",       ImVec2(uw,0))) {
                    visible=false; ShowWindow(hwnd,SW_HIDE);
                }
                ImGui::SameLine();
                bool partOn = g_use_particles;
                if (AnimatedToggleSwitch("Particles", &partOn, ImVec2(44, 20))) g_use_particles = partOn;

                // Hotkey picker
                const char* kn[]={"INSERT","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12","HOME","END","PAUSE"};
                const int   kc[]={VK_INSERT,VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12,VK_HOME,VK_END,VK_PAUSE};
                int curKey=0;
                for (int i=0;i<16;++i) if (kc[i]==(int)g_hide_hotkey) { curKey=i; break; }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("Hide UI Hotkey",&curKey,kn,16)) g_hide_hotkey=kc[curKey];

                int fps2=(int)g_fps_limit;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderInt("##fps",&fps2,15,165,"FPS Limit: %d")) g_fps_limit=fps2;

                ImGui::Spacing();
                ImGui::SeparatorText(" RUNTIME ");
                {
                    uint64_t rt=g_runtime_ms;
                    ImGui::TextDisabled("Runtime: %02llu:%02llu:%02llu  (%llu ms  |  Pkts: %d)",
                        rt/3600000ULL, (rt/60000ULL)%60, (rt/1000ULL)%60, rt, (int)g_packets);
                }
                if (AnimatedToggleSwitch("Streamer Mode (hide from OBS/capture)", &streamerMode))
                    SetWindowDisplayAffinity(hwnd, streamerMode ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        // ── FOOTER ───────────────────────────────────────────────────────────
        ImGui::SetCursorPos(ImVec2(6.0f, fh - 20.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f,0.35f,0.48f,1.0f));
        ImGui::TextDisabled("Varaxx Pro v2.5  |  by varaxx  |  %02llu:%02llu:%02llu  |  %d pkts",
            g_runtime_ms/3600000ULL,(g_runtime_ms/60000ULL)%60,(g_runtime_ms/1000ULL)%60,
            (int)g_packets);
        ImGui::PopStyleColor();

        // Bottom chromatic accent line
        dl->AddRectFilledMultiColor(
            ImVec2(winPos.x, winPos.y+fh-3), ImVec2(winPos.x+fw, winPos.y+fh),
            neonCol1, neonCol2, neonCol2, neonCol1);

        ImGui::End();

        // Render
        ImGui::Render();
        const float clear[4] = {0.010f, 0.010f, 0.016f, 1.0f};
        gCtx->OMSetRenderTargets(1, &gRtv, nullptr);
        gCtx->ClearRenderTargetView(gRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        gSwap->Present(1, 0);

        Sleep(1000 / fps);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DropDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
