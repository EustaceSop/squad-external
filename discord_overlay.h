#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include "crypt/lazyimporter.hpp"
#include "crypt/xor.hpp"

// ============================================================================
// Discord Overlay Hijack - DX11
// All WinAPI calls via LI_FN (IAT hidden), all strings via xorstr_
// ============================================================================

class DiscordOverlay {
public:
    bool HijackWindow() {
        m_hwnd = LI_FN(FindWindowA)(xorstr_("Chrome_WidgetWin_1"), xorstr_("Discord Overlay"));
        if (!m_hwnd) {
            printf(xorstr_("[-] Discord overlay window not found\n"));
            return false;
        }
        printf(xorstr_("[+] Discord overlay: 0x%p\n"), m_hwnd);

        RECT rc;
        LI_FN(GetClientRect)(m_hwnd, &rc);
        m_width = rc.right - rc.left;
        m_height = rc.bottom - rc.top;
        if (m_width <= 0 || m_height <= 0) {
            m_width = LI_FN(GetSystemMetrics)(SM_CXSCREEN);
            m_height = LI_FN(GetSystemMetrics)(SM_CYSCREEN);
        }

        return true;
    }

    bool CreateDevice() {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = m_hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0
        };

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels, 2, D3D11_SDK_VERSION,
            &sd, &m_swapChain, &m_device, &featureLevel, &m_ctx
        );

        if (FAILED(hr)) {
            printf(xorstr_("[-] D3D11CreateDeviceAndSwapChain failed: 0x%lX\n"), hr);
            return false;
        }

        ID3D11Texture2D* backBuffer = nullptr;
        hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr) || !backBuffer) return false;

        hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
        backBuffer->Release();
        if (FAILED(hr)) return false;

        printf(xorstr_("[+] DX11 device created\n"));
        return true;
    }

    void BeginFrame() {
        float clear[4] = { 0, 0, 0, 0 };
        m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
        m_ctx->ClearRenderTargetView(m_rtv, clear);
    }

    void Present() {
        m_swapChain->Present(0, 0);
    }

    void Destroy() {
        if (m_hwnd) {
            LONG_PTR ex = LI_FN(GetWindowLongPtrW)(m_hwnd, GWL_EXSTYLE);
            ex |= WS_EX_TRANSPARENT;
            LI_FN(SetWindowLongPtrW)(m_hwnd, GWL_EXSTYLE, ex);
        }

        if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
        if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
        if (m_ctx) { m_ctx->ClearState(); m_ctx->Flush(); m_ctx->Release(); m_ctx = nullptr; }
        if (m_device) { m_device->Release(); m_device = nullptr; }
    }

    void SetMenuVisible(bool visible) {
        if (!m_hwnd) return;
        LONG_PTR ex = LI_FN(GetWindowLongPtrW)(m_hwnd, GWL_EXSTYLE);
        if (visible) {
            ex &= ~WS_EX_TRANSPARENT;
            LI_FN(SetWindowLongPtrW)(m_hwnd, GWL_EXSTYLE, ex);
            FocusOverlay();
        } else {
            ex |= WS_EX_TRANSPARENT;
            LI_FN(SetWindowLongPtrW)(m_hwnd, GWL_EXSTYLE, ex);
            FocusGame();
        }
    }

    void CacheGameWindow(uint32_t pid) {
        struct Data { DWORD pid; HWND result; } data = { pid, NULL };
        LI_FN(EnumWindows)([](HWND hwnd, LPARAM lp) -> BOOL {
            auto* d = (Data*)lp;
            DWORD wndPid = 0;
            LI_FN(GetWindowThreadProcessId)(hwnd, &wndPid);
            if (wndPid == d->pid && LI_FN(IsWindowVisible)(hwnd) && LI_FN(GetWindow)(hwnd, GW_OWNER) == NULL) {
                d->result = hwnd;
                return FALSE;
            }
            return TRUE;
        }, (LPARAM)&data);
        m_gameHwnd = data.result;
    }

    HWND hwnd() const { return m_hwnd; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    ID3D11Device* device() const { return m_device; }
    ID3D11DeviceContext* context() const { return m_ctx; }
    IDXGISwapChain* swapChain() const { return m_swapChain; }

private:
    HWND m_hwnd = NULL;
    HWND m_gameHwnd = NULL;
    int m_width = 0;
    int m_height = 0;

    ID3D11Device*           m_device = nullptr;
    ID3D11DeviceContext*    m_ctx = nullptr;
    IDXGISwapChain*         m_swapChain = nullptr;
    ID3D11RenderTargetView* m_rtv = nullptr;

    void FocusOverlay() {
        if (!m_hwnd) return;
        HWND fg = LI_FN(GetForegroundWindow)();
        if (fg && fg != m_hwnd) {
            DWORD fgThread = LI_FN(GetWindowThreadProcessId)(fg, nullptr);
            DWORD myThread = LI_FN(GetCurrentThreadId)();
            LI_FN(AttachThreadInput)(myThread, fgThread, TRUE);
            LI_FN(BringWindowToTop)(m_hwnd);
            LI_FN(SetForegroundWindow)(m_hwnd);
            LI_FN(AttachThreadInput)(myThread, fgThread, FALSE);
        }
    }

    void FocusGame() {
        if (!m_gameHwnd || !LI_FN(IsWindow)(m_gameHwnd)) return;
        DWORD myThread = LI_FN(GetCurrentThreadId)();
        DWORD gameThread = LI_FN(GetWindowThreadProcessId)(m_gameHwnd, nullptr);
        LI_FN(AttachThreadInput)(myThread, gameThread, TRUE);
        LI_FN(BringWindowToTop)(m_gameHwnd);
        LI_FN(SetForegroundWindow)(m_gameHwnd);
        LI_FN(AttachThreadInput)(myThread, gameThread, FALSE);
    }
};
