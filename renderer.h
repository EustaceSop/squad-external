#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dwrite.h>
#include <d2d1.h>
#include <dxgi1_2.h>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <cstdint>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

// ============================================================================
// DX11 Renderer - replaces DX9 Renderer for Discord overlay
// Uses D3D11 for geometry, D2D1/DWrite interop for text
// ============================================================================

// Color helper (replaces D3DCOLOR)
// Format: ARGB (same as D3DCOLOR for compatibility)
#define RGBA(r,g,b,a) ((uint32_t)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))
#define RGBA_R(c) (((c) >> 16) & 0xFF)
#define RGBA_G(c) (((c) >> 8) & 0xFF)
#define RGBA_B(c) ((c) & 0xFF)
#define RGBA_A(c) (((c) >> 24) & 0xFF)

// Keep D3DCOLOR_ARGB compatibility macro
#ifndef D3DCOLOR_ARGB
#define D3DCOLOR_ARGB(a,r,g,b) RGBA(r,g,b,a)
#endif

struct Vertex2D {
    float x, y;
    float color[4]; // RGBA float
};

struct Vector2 {
    constexpr Vector2(const float x = 0.0f, const float y = 0.0f) noexcept : x(x), y(y) {}
    float x, y;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer() { Destroy(); }

    bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx, IDXGISwapChain* swapChain, HWND hwnd);
    void Destroy();

    // Frame management
    void BeginFrame();
    void EndFrame();

    // Drawing primitives
    void RenderText(const char* text, int x, int y, uint32_t color, bool outlined = false, int fontSize = 16);
    void DrawFilledRect(int x, int y, int w, int h, uint32_t color);
    void DrawRect(int x, int y, int w, int h, int thickness, uint32_t color, bool outlined = false);
    void DrawCircle(int cx, int cy, float radius, int segments, uint32_t color, bool outlined = false);
    void DrawLine(int x1, int y1, int x2, int y2, int thickness, uint32_t color, bool outlined = false);

    Vector2 GetTextSize(const char* text, int fontSize = 16);

    // ESP drawing helpers (for non-menu rendering)
    void DrawBoneLine(float x1, float y1, float x2, float y2, uint32_t color, float thickness = 1.5f);
    void DrawFilledCircle(int cx, int cy, float radius, int segments, uint32_t color);

    ID3D11Device* GetDevice() const { return m_device; }
    ID3D11DeviceContext* GetContext() const { return m_ctx; }

private:
    ID3D11Device*           m_device = nullptr;
    ID3D11DeviceContext*    m_ctx = nullptr;
    IDXGISwapChain*         m_swapChain = nullptr;
    HWND                    m_hwnd = nullptr;

    // Pipeline state
    ID3D11VertexShader*     m_vs = nullptr;
    ID3D11PixelShader*      m_ps = nullptr;
    ID3D11InputLayout*      m_inputLayout = nullptr;
    ID3D11Buffer*           m_vertexBuffer = nullptr;
    ID3D11Buffer*           m_constantBuffer = nullptr;
    ID3D11BlendState*       m_blendState = nullptr;
    ID3D11RasterizerState*  m_rastState = nullptr;
    ID3D11DepthStencilState* m_dsState = nullptr;

    // Text rendering via D2D1/DWrite interop
    ID2D1Factory*           m_d2dFactory = nullptr;
    ID2D1RenderTarget*      m_d2dRT = nullptr;
    IDWriteFactory*         m_dwFactory = nullptr;
    std::map<int, IDWriteTextFormat*> m_textFormats;
    ID2D1SolidColorBrush*   m_d2dBrush = nullptr;

    int m_screenW = 0;
    int m_screenH = 0;

    static constexpr int MAX_VERTICES = 8192;

    // Shader constant: screen dimensions for NDC conversion
    struct alignas(16) ScreenConstant {
        float screenW;
        float screenH;
        float pad[2];
    };

    bool CreateShaders();
    bool CreateBuffers();
    bool CreateStates();
    bool CreateTextResources();
    void DestroyTextResources();

    IDWriteTextFormat* GetTextFormat(int size);

    // Batch drawing
    void DrawVertices(const Vertex2D* verts, int count, D3D11_PRIMITIVE_TOPOLOGY topology);

    static void ColorToFloat4(uint32_t color, float out[4]) {
        out[0] = RGBA_R(color) / 255.0f;
        out[1] = RGBA_G(color) / 255.0f;
        out[2] = RGBA_B(color) / 255.0f;
        out[3] = RGBA_A(color) / 255.0f;
    }
};

extern std::unique_ptr<Renderer> g_Renderer;
