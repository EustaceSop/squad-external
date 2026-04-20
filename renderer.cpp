#include "renderer.h"
#include "crypt/xor.hpp"

std::unique_ptr<Renderer> g_Renderer = nullptr;

// ============================================================================
// Minimal HLSL shaders (compiled at runtime via D3DCompile)
// ============================================================================

static const char* g_shaderSrc = R"(
cbuffer ScreenCB : register(b0) {
    float screenW;
    float screenH;
    float2 pad;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float4 col : COLOR;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;
    // Convert screen coords to NDC: x: [0,W] -> [-1,1], y: [0,H] -> [1,-1]
    output.pos.x = (input.pos.x / screenW) * 2.0f - 1.0f;
    output.pos.y = 1.0f - (input.pos.y / screenH) * 2.0f;
    output.pos.z = 0.0f;
    output.pos.w = 1.0f;
    output.col = input.col;
    return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET {
    return input.col;
}
)";

// ============================================================================
// Init / Destroy
// ============================================================================

bool Renderer::Init(ID3D11Device* device, ID3D11DeviceContext* ctx, IDXGISwapChain* swapChain, HWND hwnd)
{
    m_device = device;
    m_ctx = ctx;
    m_swapChain = swapChain;
    m_hwnd = hwnd;

    RECT rc;
    GetClientRect(hwnd, &rc);
    m_screenW = rc.right - rc.left;
    m_screenH = rc.bottom - rc.top;
    if (m_screenW <= 0) m_screenW = 1920;
    if (m_screenH <= 0) m_screenH = 1080;

    if (!CreateShaders()) return false;
    if (!CreateBuffers()) return false;
    if (!CreateStates()) return false;
    if (!CreateTextResources()) return false;

    return true;
}

void Renderer::Destroy()
{
    DestroyTextResources();

    if (m_dsState) { m_dsState->Release(); m_dsState = nullptr; }
    if (m_rastState) { m_rastState->Release(); m_rastState = nullptr; }
    if (m_blendState) { m_blendState->Release(); m_blendState = nullptr; }
    if (m_constantBuffer) { m_constantBuffer->Release(); m_constantBuffer = nullptr; }
    if (m_vertexBuffer) { m_vertexBuffer->Release(); m_vertexBuffer = nullptr; }
    if (m_inputLayout) { m_inputLayout->Release(); m_inputLayout = nullptr; }
    if (m_ps) { m_ps->Release(); m_ps = nullptr; }
    if (m_vs) { m_vs->Release(); m_vs = nullptr; }
}

// ============================================================================
// Shader creation
// ============================================================================

bool Renderer::CreateShaders()
{
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    HRESULT hr = D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
        xorstr_("VSMain"), xorstr_("vs_4_0"), D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) errBlob->Release();
        return false;
    }

    hr = D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
        xorstr_("PSMain"), xorstr_("ps_4_0"), D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        vsBlob->Release();
        if (errBlob) errBlob->Release();
        return false;
    }

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); return false; }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); return false; }

    // Input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = m_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
    vsBlob->Release();
    psBlob->Release();

    return SUCCEEDED(hr);
}

// ============================================================================
// Buffer creation
// ============================================================================

bool Renderer::CreateBuffers()
{
    // Vertex buffer (dynamic)
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = MAX_VERTICES * sizeof(Vertex2D);
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->CreateBuffer(&vbDesc, nullptr, &m_vertexBuffer);
    if (FAILED(hr)) return false;

    // Constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(ScreenConstant);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ScreenConstant sc = { (float)m_screenW, (float)m_screenH, {0, 0} };
    D3D11_SUBRESOURCE_DATA cbData = { &sc, 0, 0 };

    hr = m_device->CreateBuffer(&cbDesc, &cbData, &m_constantBuffer);
    return SUCCEEDED(hr);
}

// ============================================================================
// State creation
// ============================================================================

bool Renderer::CreateStates()
{
    // Blend state (alpha blending)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = m_device->CreateBlendState(&blendDesc, &m_blendState);
    if (FAILED(hr)) return false;

    // Rasterizer state (no culling, scissor off)
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.ScissorEnable = FALSE;
    rastDesc.DepthClipEnable = TRUE;

    hr = m_device->CreateRasterizerState(&rastDesc, &m_rastState);
    if (FAILED(hr)) return false;

    // Depth stencil state (disabled)
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.StencilEnable = FALSE;

    hr = m_device->CreateDepthStencilState(&dsDesc, &m_dsState);
    return SUCCEEDED(hr);
}

// ============================================================================
// Text resources (D2D1 + DWrite interop over DXGI surface)
// ============================================================================

bool Renderer::CreateTextResources()
{
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwFactory));
    if (FAILED(hr)) return false;

    // Create D2D render target from swap chain back buffer
    IDXGISurface* surface = nullptr;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(surface, &rtProps, &m_d2dRT);
    surface->Release();
    if (FAILED(hr)) return false;

    hr = m_d2dRT->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &m_d2dBrush);
    if (FAILED(hr)) return false;

    // Pre-create common text formats
    int sizes[] = { 12, 14, 16, 18, 24 };
    for (int s : sizes) {
        IDWriteTextFormat* fmt = nullptr;
        hr = m_dwFactory->CreateTextFormat(xorstr_(L"Verdana"), nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, (float)s, xorstr_(L"en-us"), &fmt);
        if (SUCCEEDED(hr)) {
            m_textFormats[s] = fmt;
        }
    }

    return true;
}

void Renderer::DestroyTextResources()
{
    for (auto& [sz, fmt] : m_textFormats) {
        if (fmt) fmt->Release();
    }
    m_textFormats.clear();

    if (m_d2dBrush) { m_d2dBrush->Release(); m_d2dBrush = nullptr; }
    if (m_d2dRT) { m_d2dRT->Release(); m_d2dRT = nullptr; }
    if (m_dwFactory) { m_dwFactory->Release(); m_dwFactory = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
}

IDWriteTextFormat* Renderer::GetTextFormat(int size)
{
    auto it = m_textFormats.find(size);
    if (it != m_textFormats.end()) return it->second;

    // Create on demand
    IDWriteTextFormat* fmt = nullptr;
    HRESULT hr = m_dwFactory->CreateTextFormat(xorstr_(L"Verdana"), nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, (float)size, xorstr_(L"en-us"), &fmt);
    if (SUCCEEDED(hr)) {
        m_textFormats[size] = fmt;
        return fmt;
    }
    return nullptr;
}

// ============================================================================
// Frame management
// ============================================================================

void Renderer::BeginFrame()
{
    // Update screen size
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w > 0 && h > 0) {
        m_screenW = w;
        m_screenH = h;
    }

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_ctx->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        auto* sc = (ScreenConstant*)mapped.pData;
        sc->screenW = (float)m_screenW;
        sc->screenH = (float)m_screenH;
        m_ctx->Unmap(m_constantBuffer, 0);
    }

    // Set pipeline state
    m_ctx->IASetInputLayout(m_inputLayout);
    UINT stride = sizeof(Vertex2D);
    UINT offset = 0;
    m_ctx->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
    m_ctx->VSSetShader(m_vs, nullptr, 0);
    m_ctx->VSSetConstantBuffers(0, 1, &m_constantBuffer);
    m_ctx->PSSetShader(m_ps, nullptr, 0);

    float blendFactor[4] = { 0, 0, 0, 0 };
    m_ctx->OMSetBlendState(m_blendState, blendFactor, 0xFFFFFFFF);
    m_ctx->RSSetState(m_rastState);
    m_ctx->OMSetDepthStencilState(m_dsState, 0);

    // Viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_screenW;
    vp.Height = (float)m_screenH;
    vp.MaxDepth = 1.0f;
    m_ctx->RSSetViewports(1, &vp);
}

void Renderer::EndFrame()
{
    // Nothing needed - Present is called by overlay
}

// ============================================================================
// Vertex drawing
// ============================================================================

void Renderer::DrawVertices(const Vertex2D* verts, int count, D3D11_PRIMITIVE_TOPOLOGY topology)
{
    if (!m_ctx || !m_vertexBuffer || count <= 0 || count > MAX_VERTICES) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_ctx->Map(m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, verts, count * sizeof(Vertex2D));
        m_ctx->Unmap(m_vertexBuffer, 0);
    }

    m_ctx->IASetPrimitiveTopology(topology);
    m_ctx->Draw(count, 0);
}

// ============================================================================
// Primitives
// ============================================================================

void Renderer::DrawFilledRect(int x, int y, int w, int h, uint32_t color)
{
    float c[4];
    ColorToFloat4(color, c);

    Vertex2D verts[4] = {
        { (float)x,       (float)y,       { c[0], c[1], c[2], c[3] } },
        { (float)(x + w), (float)y,       { c[0], c[1], c[2], c[3] } },
        { (float)x,       (float)(y + h), { c[0], c[1], c[2], c[3] } },
        { (float)(x + w), (float)(y + h), { c[0], c[1], c[2], c[3] } },
    };

    DrawVertices(verts, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
}

void Renderer::DrawRect(int x, int y, int w, int h, int thickness, uint32_t color, bool outlined)
{
    DrawFilledRect(x, y, w, thickness, color);
    DrawFilledRect(x, y + h - thickness, w, thickness, color);
    DrawFilledRect(x, y, thickness, h, color);
    DrawFilledRect(x + w - thickness, y, thickness, h, color);

    if (outlined) {
        uint32_t black = D3DCOLOR_ARGB(255, 0, 0, 0);
        DrawFilledRect(x - 1, y - 1, w + 2, 1, black);
        DrawFilledRect(x - 1, y + h, w + 2, 1, black);
        DrawFilledRect(x - 1, y, 1, h, black);
        DrawFilledRect(x + w, y, 1, h, black);
    }
}

void Renderer::DrawLine(int x1, int y1, int x2, int y2, int thickness, uint32_t color, bool outlined)
{
    float fx1 = (float)x1, fy1 = (float)y1;
    float fx2 = (float)x2, fy2 = (float)y2;
    float dx = fx2 - fx1, dy = fy2 - fy1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) return;

    dx /= len; dy /= len;
    float half = thickness / 2.0f;
    float px = -dy * half, py = dx * half;

    float c[4];
    ColorToFloat4(color, c);

    Vertex2D verts[4] = {
        { fx1 + px, fy1 + py, { c[0], c[1], c[2], c[3] } },
        { fx1 - px, fy1 - py, { c[0], c[1], c[2], c[3] } },
        { fx2 + px, fy2 + py, { c[0], c[1], c[2], c[3] } },
        { fx2 - px, fy2 - py, { c[0], c[1], c[2], c[3] } },
    };

    if (outlined) {
        float oc[4];
        ColorToFloat4(D3DCOLOR_ARGB(255, 0, 0, 0), oc);
        float ohalf = half + 1.0f;
        float opx = -dy * ohalf, opy = dx * ohalf;
        Vertex2D overts[4] = {
            { fx1 + opx, fy1 + opy, { oc[0], oc[1], oc[2], oc[3] } },
            { fx1 - opx, fy1 - opy, { oc[0], oc[1], oc[2], oc[3] } },
            { fx2 + opx, fy2 + opy, { oc[0], oc[1], oc[2], oc[3] } },
            { fx2 - opx, fy2 - opy, { oc[0], oc[1], oc[2], oc[3] } },
        };
        DrawVertices(overts, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    }

    DrawVertices(verts, 4, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
}

void Renderer::DrawCircle(int cx, int cy, float radius, int segments, uint32_t color, bool outlined)
{
    if (segments < 3 || segments > 256) return;

    auto drawRing = [&](float r, uint32_t col) {
        float c[4];
        ColorToFloat4(col, c);
        std::vector<Vertex2D> verts(segments + 1);
        float step = 6.28318530718f / segments;
        for (int i = 0; i <= segments; i++) {
            float angle = step * i;
            verts[i] = { cx + cosf(angle) * r, cy + sinf(angle) * r, { c[0], c[1], c[2], c[3] } };
        }
        DrawVertices(verts.data(), (int)verts.size(), D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    };

    if (outlined) {
        drawRing(radius + 1.0f, D3DCOLOR_ARGB(255, 0, 0, 0));
        drawRing(radius - 1.0f, D3DCOLOR_ARGB(255, 0, 0, 0));
    }
    drawRing(radius, color);
}

void Renderer::DrawFilledCircle(int cx, int cy, float radius, int segments, uint32_t color)
{
    if (segments < 3 || segments > 256) return;

    float c[4];
    ColorToFloat4(color, c);

    // DX11 doesn't support TRIANGLEFAN, convert to TRIANGLELIST
    std::vector<Vertex2D> verts(segments * 3);
    float step = 6.28318530718f / segments;
    Vertex2D center = { (float)cx, (float)cy, { c[0], c[1], c[2], c[3] } };

    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        verts[i * 3 + 0] = center;
        verts[i * 3 + 1] = { cx + cosf(a0) * radius, cy + sinf(a0) * radius, { c[0], c[1], c[2], c[3] } };
        verts[i * 3 + 2] = { cx + cosf(a1) * radius, cy + sinf(a1) * radius, { c[0], c[1], c[2], c[3] } };
    }
    DrawVertices(verts.data(), (int)verts.size(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Renderer::DrawBoneLine(float x1, float y1, float x2, float y2, uint32_t color, float thickness)
{
    DrawLine((int)x1, (int)y1, (int)x2, (int)y2, (int)thickness, color, false);
}

// ============================================================================
// Text rendering via D2D1 interop
// ============================================================================

void Renderer::RenderText(const char* text, int x, int y, uint32_t color, bool outlined, int fontSize)
{
    if (!m_d2dRT || !m_d2dBrush || !text || !text[0]) return;

    IDWriteTextFormat* fmt = GetTextFormat(fontSize);
    if (!fmt) return;

    // Convert to wide string
    int len = (int)strlen(text);
    std::wstring wstr(text, text + len);

    m_d2dRT->BeginDraw();

    if (outlined) {
        m_d2dBrush->SetColor(D2D1::ColorF(0, 0, 0, RGBA_A(color) / 255.0f));
        D2D1_RECT_F r1 = { (float)x, (float)y - 1, (float)(x + 500), (float)(y + 50) };
        m_d2dRT->DrawTextW(wstr.c_str(), (UINT32)wstr.size(), fmt, r1, m_d2dBrush);
        D2D1_RECT_F r2 = { (float)x - 1, (float)y, (float)(x + 500), (float)(y + 50) };
        m_d2dRT->DrawTextW(wstr.c_str(), (UINT32)wstr.size(), fmt, r2, m_d2dBrush);
        D2D1_RECT_F r3 = { (float)x, (float)y + 1, (float)(x + 500), (float)(y + 50) };
        m_d2dRT->DrawTextW(wstr.c_str(), (UINT32)wstr.size(), fmt, r3, m_d2dBrush);
    }

    m_d2dBrush->SetColor(D2D1::ColorF(RGBA_R(color) / 255.0f, RGBA_G(color) / 255.0f, RGBA_B(color) / 255.0f, RGBA_A(color) / 255.0f));
    D2D1_RECT_F rect = { (float)x, (float)y, (float)(x + 500), (float)(y + 50) };
    m_d2dRT->DrawTextW(wstr.c_str(), (UINT32)wstr.size(), fmt, rect, m_d2dBrush);

    m_d2dRT->EndDraw();
}

Vector2 Renderer::GetTextSize(const char* text, int fontSize)
{
    if (!m_dwFactory || !text || !text[0]) return {};

    IDWriteTextFormat* fmt = GetTextFormat(fontSize);
    if (!fmt) return {};

    std::wstring wstr(text, text + strlen(text));

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = m_dwFactory->CreateTextLayout(wstr.c_str(), (UINT32)wstr.size(), fmt, 1000.f, 100.f, &layout);
    if (FAILED(hr) || !layout) return {};

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    layout->Release();

    return { metrics.width, metrics.height };
}
