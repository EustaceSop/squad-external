#pragma once
#include "renderer.h"
#include <functional>
#include <vector>
#include <string>
#include <memory>

// ============================================================================
// GUI Framework - DX11 port of syscalls custom UI
// Self-drawn overlay menu: tabs, checkbox, slider, keybind, combo, color picker
// No ImGui dependency - pure vertex draw + DWrite text
// ============================================================================

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define HEADER_COLOR D3DCOLOR_ARGB(255, 34, 35, 34)
#define BG_COLOR D3DCOLOR_ARGB(255, 34, 35, 34)
#define BORDER_COLOR D3DCOLOR_ARGB(255, 98, 98, 98)
#define TEXT_COLOR D3DCOLOR_ARGB(255, 111, 111, 111)
#define TEXT_ACTIVE_COLOR D3DCOLOR_ARGB(255, 255, 255, 255)
#define WIDGET_COLOR D3DCOLOR_ARGB(255, 239, 23, 18)

struct Popup {
    std::function<void()> draw;
};

struct Notification {
    std::string text;
    float lifetime;
    float timeLeft;
};

class GUI {
public:
    GUI(Renderer* renderer, HWND hwnd);

    void NewFrame();
    void EndFrame();

    bool IsMouseInRect(int x, int y, int width, int height);

    void BeginUI(int x, int y, const char* title);
    void EndUI();

    bool Tab(const char* label, int index, int tabCount);
    void BeginGroup(int w, int h);
    void EndGroup();
    bool Checkbox(const char* label, const char* tooltipLabel, bool* v);
    bool SliderFloat(const char* label, float* value, float min, float max, float step = 1.0f);
    bool SliderInt(const char* label, int* value, int min, int max, int step = 1);
    bool Keybind(const char* label, int* key);
    bool ComboBox(const char* label, int* value, const char** items, int itemCount);
    bool MultiComboBox(const char* label, std::vector<int>* values, const char** items, int itemCount);
    bool TextInput(const char* label, char* buffer, int bufferSize);
    bool ColorPicker(const char* label, uint32_t* color);
    bool ListBox(const char* label, int* value, const char** items, int itemCount);

    // Button widget
    bool Button(const char* label);

    void SetCursor(int x, int y) {
        m_CursorX = x;
        m_CursorY = y;
    }

    void SetTooltip(const char* label) {
        if (m_TooltipSetThisFrame) return;
        if (label && label[0]) {
            m_CurrentTooltip = label;
            m_TooltipSetThisFrame = true;
        }
    }

    void AddPopup(std::function<void()> drawFunc) {
        m_Popups.push_back({ drawFunc });
    }

    void AddNotification(const char* text, float seconds = 3.0f) {
        if (!text || !text[0]) return;
        for (auto& n : m_Notifications) {
            if (n.text == text) return;
        }
        m_Notifications.push_back({ text, seconds, seconds });
    }

    Renderer* m_Renderer = nullptr;
    HWND m_Hwnd = nullptr;

    struct MouseState {
        int x = 0;
        int y = 0;
        bool down = false;
        bool clicked = false;
    } m_Mouse;

    float m_DeltaTime = 0.0f;
    bool m_Open = true;
    bool m_Dragging = false;
    int m_DragOffsetX = 0;
    int m_DragOffsetY = 0;
    int m_WinX = 0;
    int m_WinY = 0;
    int m_Width = 420;
    int m_Height = 420;
    int m_MinHeight = 260;
    int m_MaxHeight = 900;
    bool m_AutoResize = true;
    int m_CursorX = 0;
    int m_CursorY = 0;
    int m_HeaderHeight = 24;
    int m_HeaderGap = 4;
    int m_Padding = 10;
    int m_LineHeight = 10;
    int m_ActiveTab = 0;
    int m_TabScroll = 0;
    bool m_ScrollButtonDown = false;
    int m_CurrentWidth = 0;
    std::vector<int> m_ContainerStack;
    int* m_ListeningKey = nullptr;
    bool m_WaitingForKey = false;
    bool m_ConfirmingKey = false;
    int m_PendingKey = 0;
    std::string m_CurrentTooltip = ">";
    bool m_TooltipSetThisFrame = false;
    int* m_OpenCombo = nullptr;
    std::vector<int>* m_OpenMultiCombo = nullptr;
    char* m_ActiveTextInput = nullptr;
    int m_TextCursorBlink = 0;
    int m_ScrollOffset = 0;
    std::vector<int> m_TabHeights;
    std::vector<Popup> m_Popups;
    std::vector<Notification> m_Notifications;
};

extern std::unique_ptr<GUI> g_GUI;
