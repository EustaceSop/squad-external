#include "gui.h"
#include "crypt/lazyimporter.hpp"
#include "crypt/xor.hpp"
#include <algorithm>

std::unique_ptr<GUI> g_GUI = nullptr;

GUI::GUI(Renderer* renderer, HWND hwnd) : m_Renderer(renderer), m_Hwnd(hwnd) {
    m_TabHeights.resize(16, 0);
}

void GUI::NewFrame() {
    static uint64_t lastTick = 0;
    uint64_t now = GetTickCount64();
    if (lastTick == 0) lastTick = now;
    m_DeltaTime = (float)(now - lastTick) / 1000.0f;
    lastTick = now;
    if (m_DeltaTime > 0.1f) m_DeltaTime = 0.1f;

    POINT p;
    LI_FN(GetCursorPos)(&p);
    LI_FN(ScreenToClient)(m_Hwnd, &p);

    bool isDown = (LI_FN(GetAsyncKeyState)(VK_LBUTTON) & 0x8000) != 0;
    m_Mouse.clicked = (isDown && !m_Mouse.down);
    m_Mouse.down = isDown;
    m_Mouse.x = p.x;
    m_Mouse.y = p.y;

    m_CurrentTooltip = ">";
    m_TooltipSetThisFrame = false;
}

bool GUI::IsMouseInRect(int x, int y, int width, int height) {
    return m_Mouse.x >= x && m_Mouse.y >= y && m_Mouse.x <= x + width && m_Mouse.y <= y + height;
}

void GUI::EndFrame() {}

void GUI::BeginUI(int x, int y, const char* title) {
    if (!m_Renderer || !m_Open) return;

    RECT rc{};
    LI_FN(GetClientRect)(m_Hwnd, &rc);
    int screenW = rc.right - rc.left;
    int screenH = rc.bottom - rc.top;
    int footerHeight = m_LineHeight + m_Padding;
    int totalNonPanel = m_HeaderHeight + m_HeaderGap + footerHeight + 20;
    int maxPanelHeight = (std::max)(m_MinHeight, screenH - totalNonPanel);

    static bool initialized = false;
    if (!initialized) {
        m_WinX = x; m_WinY = y;
        m_CurrentWidth = m_Width;
        initialized = true;
    }

    if (m_AutoResize) {
        if (m_ActiveTab >= (int)m_TabHeights.size())
            m_TabHeights.resize(m_ActiveTab + 1, 0);

        if (m_TabHeights[m_ActiveTab] > 0)
            m_Height = std::clamp(m_TabHeights[m_ActiveTab], m_MinHeight, maxPanelHeight);
        else
            m_Height = std::clamp(m_Height, m_MinHeight, maxPanelHeight);
    }

    bool headerHovered = IsMouseInRect(m_WinX, m_WinY, m_Width, m_HeaderHeight);
    if (headerHovered && m_Mouse.clicked) {
        m_Dragging = true;
        m_DragOffsetX = m_Mouse.x - m_WinX;
        m_DragOffsetY = m_Mouse.y - m_WinY;
    }
    if (!m_Mouse.down) m_Dragging = false;
    if (m_Dragging) {
        m_WinX = m_Mouse.x - m_DragOffsetX;
        m_WinY = m_Mouse.y - m_DragOffsetY;
    }

    int totalMenuHeight = m_HeaderHeight + m_HeaderGap + m_Height + footerHeight;
    if (m_WinX < 10) m_WinX = 10;
    if (m_WinY < 10) m_WinY = 10;
    if (m_WinX + m_Width > screenW - 10) m_WinX = (std::max)(10, screenW - 10 - m_Width);
    if (m_WinY + totalMenuHeight > screenH - 10) m_WinY = (std::max)(10, screenH - 10 - totalMenuHeight);

    int panelY = m_WinY + m_HeaderGap + m_HeaderHeight;
    Vector2 titleTextSize = m_Renderer->GetTextSize(title);

    // Header
    m_Renderer->DrawFilledRect(m_WinX, m_WinY, m_Width, m_HeaderHeight, HEADER_COLOR);
    m_Renderer->DrawRect(m_WinX, m_WinY, m_Width, m_HeaderHeight, 1, BORDER_COLOR);
    m_Renderer->RenderText(title, m_WinX + 8, m_WinY + (m_HeaderHeight / 2) - (int)(titleTextSize.y / 2), TEXT_COLOR, true);

    // Background
    m_Renderer->DrawFilledRect(m_WinX, panelY, m_Width, m_Height, BG_COLOR);
    m_Renderer->DrawRect(m_WinX, panelY, m_Width, m_Height, 1, BORDER_COLOR);

    int tabBarHeight = m_LineHeight + m_Padding;
    m_CursorX = m_WinX + m_Padding;
    m_CursorY = panelY + m_Padding + tabBarHeight;
}

void GUI::EndUI() {
    if (!m_Renderer || !m_Open) return;

    // Tooltip bar
    int panelY = m_WinY + m_HeaderGap + m_HeaderHeight;
    m_Renderer->DrawFilledRect(m_WinX, panelY + m_Height, m_Width, m_LineHeight + m_Padding, BG_COLOR);
    m_Renderer->DrawRect(m_WinX, panelY + m_Height, 1, m_LineHeight + m_Padding, 1, BORDER_COLOR);
    m_Renderer->DrawRect(m_WinX + m_Width - 1, panelY + m_Height, 1, m_LineHeight + m_Padding, 1, BORDER_COLOR);
    m_Renderer->DrawRect(m_WinX, panelY + m_Height + m_LineHeight + m_Padding - 1, m_Width, 1, 1, BORDER_COLOR);

    if (!m_CurrentTooltip.empty()) {
        m_Renderer->RenderText(m_CurrentTooltip.c_str(), m_WinX + m_Padding, panelY + m_Height, TEXT_COLOR, true);
    }

    // Draw popups
    for (auto& popup : m_Popups) popup.draw();
    m_Popups.clear();

    // Draw notifications
    if (!m_Notifications.empty()) {
        const int width = m_Width;
        const int height = 50;
        const int spacing = 6;
        const int margin = 10;

        RECT rc{};
        LI_FN(GetClientRect)(m_Hwnd, &rc);
        int nx = rc.right - width - margin;
        int ny = rc.bottom - margin - height;

        for (size_t i = 0; i < m_Notifications.size();) {
            auto& n = m_Notifications[i];
            n.timeLeft -= m_DeltaTime;
            if (n.timeLeft <= 0.0f) {
                m_Notifications.erase(m_Notifications.begin() + i);
                continue;
            }

            m_Renderer->DrawFilledRect(nx, ny, width, height, BG_COLOR);
            m_Renderer->DrawRect(nx, ny, width, height, 1, BORDER_COLOR);

            Vector2 textSize = m_Renderer->GetTextSize(n.text.c_str());
            m_Renderer->RenderText(n.text.c_str(), nx + m_Padding, ny + (height - (int)textSize.y - 4) / 2, TEXT_COLOR, true);

            float progress = n.timeLeft / n.lifetime;
            if (progress < 0.f) progress = 0.f;
            int barMaxW = (int)(width * 0.75f);
            int barX = nx + (width - barMaxW) / 2;
            int barY = ny + (height - (int)textSize.y - 4) / 2 + (int)textSize.y + 4;
            m_Renderer->DrawFilledRect(barX, barY, (int)(barMaxW * progress), 4, WIDGET_COLOR);

            ny += height + spacing;
            i++;
        }
    }

    if (m_AutoResize) {
        RECT rc{};
        LI_FN(GetClientRect)(m_Hwnd, &rc);
        int screenH = rc.bottom - rc.top;
        int maxPanelHeight = (std::max)(m_MinHeight, screenH - (m_HeaderHeight + m_HeaderGap + m_LineHeight + m_Padding + 20));

        int usedHeight = (m_CursorY - panelY) + (m_Padding * 2);
        int desiredHeight = std::clamp(usedHeight, m_MinHeight, maxPanelHeight);

        if (m_ActiveTab >= (int)m_TabHeights.size())
            m_TabHeights.resize(m_ActiveTab + 1, 0);

        m_TabHeights[m_ActiveTab] = desiredHeight;
        m_Height = desiredHeight;
    }
}

bool GUI::Tab(const char* label, int index, int tabCount) {
    if (!m_Renderer || !m_Open) return false;

    const int maxVisibleTabs = 5;
    int totalTabWidth = m_Width;
    int startIndex = 0;
    if (tabCount > maxVisibleTabs && m_TabScroll == 1) startIndex = maxVisibleTabs;
    int visibleTabs = (std::min)(tabCount - startIndex, maxVisibleTabs);
    int tabWidth = totalTabWidth / maxVisibleTabs;

    int x = m_WinX + ((index - startIndex) * tabWidth);
    int panelY = m_WinY + m_HeaderGap + m_HeaderHeight;
    int y = panelY + m_Padding;

    if (index < startIndex || index >= startIndex + visibleTabs) return false;

    bool hovered = IsMouseInRect(x, panelY, tabWidth, m_LineHeight);
    bool active = (m_ActiveTab == index);
    Vector2 textSize = m_Renderer->GetTextSize(label);

    m_Renderer->RenderText(label, x + (tabWidth / 2) - (int)(textSize.x / 2), y + (m_LineHeight / 2) - (int)(textSize.y / 2),
        active || hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawRect(m_WinX, y + (int)textSize.y, m_Width, 1, 1, BORDER_COLOR);

    if (tabCount > maxVisibleTabs && index == startIndex + visibleTabs - 1) {
        const char* indicatorLabel = (m_TabScroll == 0) ? ">>" : "<<";
        Vector2 indicatorTextSize = m_Renderer->GetTextSize(">>");
        int indicatorX = m_WinX + totalTabWidth - (int)indicatorTextSize.x - 8;
        int indicatorY = y + (m_LineHeight / 2) - (int)(indicatorTextSize.y / 2) - 2;
        bool indicatorHovered = IsMouseInRect(indicatorX, panelY, (int)indicatorTextSize.x + 8, m_LineHeight);
        m_Renderer->RenderText(indicatorLabel, indicatorX + 6, indicatorY, indicatorHovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);

        bool currentMouseDown = (LI_FN(GetAsyncKeyState)(VK_LBUTTON) & 0x8000) != 0;
        if (indicatorHovered) {
            if (currentMouseDown && !m_ScrollButtonDown) m_TabScroll = 1 - m_TabScroll;
            m_ScrollButtonDown = currentMouseDown;
        } else {
            m_ScrollButtonDown = false;
        }
    }

    if (hovered && m_Mouse.clicked) {
        m_ActiveTab = index;
        if (m_AutoResize && index < (int)m_TabHeights.size() && m_TabHeights[index] > 0)
            m_Height = m_TabHeights[index];
        return true;
    }
    return false;
}

void GUI::BeginGroup(int w, int h) {
    if (!m_Renderer || !m_Open) return;
    m_ContainerStack.push_back(m_CurrentWidth);
    m_CurrentWidth = w;
    m_CursorY += m_Padding;
}

void GUI::EndGroup() {
    if (!m_ContainerStack.empty()) {
        m_CurrentWidth = m_ContainerStack.back();
        m_ContainerStack.pop_back();
    }
}

bool GUI::Checkbox(const char* label, const char* tooltipLabel, bool* v) {
    if (!m_Renderer || !m_Open || !v) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    constexpr int boxSize = 12;

    Vector2 textSize = m_Renderer->GetTextSize(label);
    int textY = y + (m_LineHeight / 2) - (int)(textSize.y / 2);
    int boxX = x + m_CurrentWidth - (m_Padding * 2) - boxSize;
    int boxY = y + (m_LineHeight / 2) - (boxSize / 2);

    bool hovered = IsMouseInRect(x, y, (boxX + boxSize) - x, m_LineHeight);
    if (hovered) SetTooltip(tooltipLabel);
    if (hovered && m_Mouse.clicked) *v = !*v;

    m_Renderer->RenderText(label, x, textY, hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawRect(boxX, boxY, boxSize, boxSize, 1, BORDER_COLOR);

    if (*v) {
        int padding = 3;
        int midX = boxX + boxSize / 2;
        int topY = boxY + padding;
        int bottomY = boxY + boxSize - padding;
        m_Renderer->DrawLine(boxX + padding, topY, midX, bottomY, 1, WIDGET_COLOR);
        m_Renderer->DrawLine(midX, bottomY, boxX + boxSize - padding, topY, 1, WIDGET_COLOR);
    }

    m_CursorY += m_LineHeight + m_Padding;
    return hovered && m_Mouse.clicked;
}

bool GUI::SliderFloat(const char* label, float* value, float min, float max, float step) {
    if (!m_Renderer || !m_Open || !value || max <= min) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    Vector2 textSize = m_Renderer->GetTextSize(label);
    int textY = y + (m_LineHeight / 2) - (int)(textSize.y / 2);
    int sliderWidth = (int)(m_CurrentWidth * 0.35f);
    constexpr int sliderHeight = 6;
    int sliderX = x + (m_CurrentWidth - (m_Padding * 2) - sliderWidth);
    int sliderY = y + (m_LineHeight / 2) - (sliderHeight / 2);

    bool hovered = IsMouseInRect(sliderX, sliderY, sliderWidth, m_LineHeight);
    if (hovered && m_Mouse.down) {
        float t = (float)(m_Mouse.x - sliderX) / (float)sliderWidth;
        t = std::clamp(t, 0.0f, 1.0f);
        *value = min + t * (max - min);
        if (step > 0.0f) *value = std::round(*value / step) * step;
    }

    float t = (*value - min) / (max - min);

    m_Renderer->RenderText(label, x, textY, hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawFilledRect(sliderX, sliderY, (int)(sliderWidth * t), sliderHeight, WIDGET_COLOR);
    m_Renderer->DrawRect(sliderX, sliderY, sliderWidth, sliderHeight, 1, BORDER_COLOR);

    char buffer[32];
    sprintf_s(buffer, "%.0f", *value);
    Vector2 bufSize = m_Renderer->GetTextSize(buffer, 12);
    m_Renderer->RenderText(buffer, sliderX + (sliderWidth / 2) - (int)(bufSize.x / 2), textY + sliderHeight + 4, WIDGET_COLOR, true, 12);

    m_CursorY += m_LineHeight + m_Padding;
    return hovered && m_Mouse.down;
}

bool GUI::SliderInt(const char* label, int* value, int min, int max, int step) {
    if (!m_Renderer || !m_Open || !value || max <= min) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    Vector2 textSize = m_Renderer->GetTextSize(label);
    int textY = y + (m_LineHeight / 2) - (int)(textSize.y / 2);
    int sliderWidth = (int)(m_CurrentWidth * 0.35f);
    constexpr int sliderHeight = 6;
    int sliderX = x + (m_CurrentWidth - (m_Padding * 2) - sliderWidth);
    int sliderY = y + (m_LineHeight / 2) - (sliderHeight / 2);

    bool hovered = IsMouseInRect(sliderX, sliderY, sliderWidth, m_LineHeight);
    if (hovered && m_Mouse.down) {
        float t = (float)(m_Mouse.x - sliderX) / (float)sliderWidth;
        t = std::clamp(t, 0.0f, 1.0f);
        int newValue = min + (int)(t * (max - min));
        if (step > 1) newValue = ((newValue - min + step / 2) / step) * step + min;
        *value = std::clamp(newValue, min, max);
    }

    float t = (float)(*value - min) / (float)(max - min);

    m_Renderer->RenderText(label, x, textY, hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawFilledRect(sliderX, sliderY, (int)(sliderWidth * t), sliderHeight, WIDGET_COLOR);
    m_Renderer->DrawRect(sliderX, sliderY, sliderWidth, sliderHeight, 1, BORDER_COLOR);

    char buffer[32];
    sprintf_s(buffer, "%d", *value);
    Vector2 bufSize = m_Renderer->GetTextSize(buffer, 12);
    m_Renderer->RenderText(buffer, sliderX + (sliderWidth / 2) - (int)(bufSize.x / 2), textY + sliderHeight + 4, WIDGET_COLOR, true, 12);

    m_CursorY += m_LineHeight + m_Padding;
    return hovered && m_Mouse.down;
}

bool GUI::Keybind(const char* label, int* key) {
    if (!m_Renderer || !m_Open || !key) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    Vector2 textSize = m_Renderer->GetTextSize(label);
    int textY = y + (m_LineHeight / 2) - (int)(textSize.y / 2);
    constexpr int boxHeight = 14;
    int boxWidth = (int)(m_CurrentWidth * 0.35f);
    int boxX = x + m_CurrentWidth - (m_Padding * 2) - boxWidth;
    int boxY = y + (m_LineHeight / 2) - (boxHeight / 2);

    bool hovered = IsMouseInRect(x, y, (boxX + boxWidth) - x, m_LineHeight);
    if (hovered && m_Mouse.clicked) {
        m_WaitingForKey = true;
        m_ListeningKey = key;
        m_ConfirmingKey = false;
        m_PendingKey = 0;
    }

    if (m_WaitingForKey && m_ListeningKey == key) {
        if (m_ConfirmingKey) {
            char keyName[32]{};
            LI_FN(GetKeyNameTextA)(LI_FN(MapVirtualKeyA)(m_PendingKey, MAPVK_VK_TO_VSC) << 16, keyName, sizeof(keyName));
            char buffer[64];
            sprintf_s(buffer, "Confirm %s? (y/n)", keyName);
            SetTooltip(buffer);
            if ((LI_FN(GetAsyncKeyState)('Y') & 1)) { *key = m_PendingKey; m_WaitingForKey = false; m_ConfirmingKey = false; m_ListeningKey = nullptr; }
            else if ((LI_FN(GetAsyncKeyState)('N') & 1)) { m_WaitingForKey = false; m_ConfirmingKey = false; m_ListeningKey = nullptr; m_PendingKey = 0; }
        } else {
            SetTooltip("Press any key...");
            for (int i = 8; i < 256; i++) {
                if (i == VK_LBUTTON || i == VK_RBUTTON || i == VK_MBUTTON) continue;
                if (LI_FN(GetAsyncKeyState)(i) & 1) {
                    if (i == VK_ESCAPE || i == VK_BACK) { *key = 0; m_WaitingForKey = false; m_ListeningKey = nullptr; }
                    else { m_PendingKey = i; m_ConfirmingKey = true; }
                    break;
                }
            }
        }
    }

    m_Renderer->RenderText(label, x, textY, hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawRect(boxX, boxY, boxWidth, boxHeight, 1, BORDER_COLOR);

    const char* keyText = "None";
    char keyName[32]{};
    if (m_WaitingForKey && m_ListeningKey == key) { keyText = "..."; }
    else if (*key != 0) {
        LI_FN(GetKeyNameTextA)(LI_FN(MapVirtualKeyA)(*key, MAPVK_VK_TO_VSC) << 16, keyName, sizeof(keyName));
        keyText = keyName;
    }
    Vector2 keyTextSize = m_Renderer->GetTextSize(keyText);
    m_Renderer->RenderText(keyText, boxX + (boxWidth / 2) - (int)(keyTextSize.x / 2), boxY + (boxHeight / 2) - (int)(keyTextSize.y / 2), TEXT_COLOR, true);

    m_CursorY += m_LineHeight + m_Padding;
    return hovered && m_Mouse.clicked;
}

bool GUI::ComboBox(const char* label, int* value, const char** items, int itemCount) {
    if (!m_Renderer || !m_Open || !value || !items || itemCount <= 0) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    Vector2 textSize = m_Renderer->GetTextSize(label);
    int textY = y + (m_LineHeight / 2) - (int)(textSize.y / 2);
    constexpr int boxHeight = 14;
    int boxWidth = (int)(m_CurrentWidth * 0.35f);
    int boxX = x + m_CurrentWidth - (m_Padding * 2) - boxWidth;
    int boxY = y + (m_LineHeight / 2) - (boxHeight / 2);

    bool hovered = IsMouseInRect(x, y, (boxX + boxWidth) - x, m_LineHeight);
    if (hovered && m_Mouse.clicked) {
        m_OpenCombo = (m_OpenCombo == value) ? nullptr : value;
    }

    m_Renderer->RenderText(label, x, textY, hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawRect(boxX, boxY, boxWidth, boxHeight, 1, BORDER_COLOR);

    const char* preview = (*value >= 0 && *value < itemCount) ? items[*value] : "?";
    Vector2 previewSize = m_Renderer->GetTextSize(preview);
    m_Renderer->RenderText(preview, boxX + (boxWidth / 2) - (int)(previewSize.x / 2), boxY + (boxHeight / 2) - (int)(previewSize.y / 2), TEXT_COLOR, true);

    bool valueChanged = false;
    if (m_OpenCombo == value) {
        AddPopup([=, &valueChanged]() {
            m_Renderer->DrawFilledRect(boxX, boxY + boxHeight, boxWidth, itemCount * (m_LineHeight + 5), BG_COLOR);
            m_Renderer->DrawRect(boxX, boxY + boxHeight, boxWidth, itemCount * (m_LineHeight + 5), 1, BORDER_COLOR);
            for (int i = 0; i < itemCount; i++) {
                int itemY = boxY + boxHeight + (i * (m_LineHeight + 4));
                bool itemHovered = IsMouseInRect(boxX, itemY, boxWidth, m_LineHeight);
                if (itemHovered && m_Mouse.clicked) { *value = i; m_OpenCombo = nullptr; valueChanged = true; }
                Vector2 itemSize = m_Renderer->GetTextSize(items[i]);
                m_Renderer->RenderText(items[i], boxX + 4, itemY + 4 + (m_LineHeight / 2) - (int)(itemSize.y / 2), (*value == i || itemHovered) ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
            }
        });
    }

    m_CursorY += m_LineHeight + m_Padding;
    return valueChanged;
}

bool GUI::MultiComboBox(const char* label, std::vector<int>* values, const char** items, int itemCount) {
    if (!m_Renderer || !m_Open || !values || !items || itemCount <= 0) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    constexpr int boxHeight = 14;
    int boxWidth = (int)(m_CurrentWidth * 0.35f);
    int boxX = x + m_CurrentWidth - (m_Padding * 2) - boxWidth;
    int boxY = y + (m_LineHeight / 2) - (boxHeight / 2);

    bool hovered = IsMouseInRect(x, y, (boxX + boxWidth) - x, m_LineHeight);
    if (hovered && m_Mouse.clicked) {
        m_OpenMultiCombo = (m_OpenMultiCombo == values) ? nullptr : values;
    }

    m_Renderer->RenderText(label, x, y + (m_LineHeight / 2) - (int)(m_Renderer->GetTextSize(label).y / 2), hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawRect(boxX, boxY, boxWidth, boxHeight, 1, BORDER_COLOR);

    std::string preview = values->empty() ? "None" : "";
    for (size_t i = 0; i < values->size(); i++) {
        preview += items[(*values)[i]];
        if (i < values->size() - 1) preview += ", ";
    }
    Vector2 previewSize = m_Renderer->GetTextSize(preview.c_str());
    m_Renderer->RenderText(preview.c_str(), boxX + (boxWidth / 2) - (int)(previewSize.x / 2), boxY + (boxHeight / 2) - (int)(previewSize.y / 2), TEXT_COLOR, true);

    bool changed = false;
    if (m_OpenMultiCombo == values) {
        AddPopup([=, &changed]() {
            m_Renderer->DrawFilledRect(boxX, boxY + boxHeight, boxWidth, itemCount * (m_LineHeight + 5), BG_COLOR);
            m_Renderer->DrawRect(boxX, boxY + boxHeight, boxWidth, itemCount * (m_LineHeight + 5), 1, BORDER_COLOR);
            for (int i = 0; i < itemCount; i++) {
                int itemY = boxY + boxHeight + (i * (m_LineHeight + 4));
                bool itemHovered = IsMouseInRect(boxX, itemY, boxWidth, m_LineHeight);
                if (itemHovered && m_Mouse.clicked) {
                    auto it = std::find(values->begin(), values->end(), i);
                    if (it != values->end()) values->erase(it); else values->push_back(i);
                    changed = true;
                }
                bool selected = std::find(values->begin(), values->end(), i) != values->end();
                Vector2 sz = m_Renderer->GetTextSize(items[i]);
                m_Renderer->RenderText(items[i], boxX + 4, itemY + 4 + (m_LineHeight / 2) - (int)(sz.y / 2), (selected || itemHovered) ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
            }
        });
    }

    m_CursorY += m_LineHeight + m_Padding;
    return changed;
}

bool GUI::TextInput(const char* label, char* buffer, int bufferSize) {
    if (!m_Renderer || !m_Open || !buffer || bufferSize <= 1) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    constexpr int boxHeight = 14;
    int boxWidth = (int)(m_CurrentWidth * 0.35f);
    int boxX = x + m_CurrentWidth - (m_Padding * 2) - boxWidth;
    int boxY = y + (m_LineHeight / 2) - (boxHeight / 2);

    bool hovered = IsMouseInRect(x, y, (boxX + boxWidth) - x, m_LineHeight);
    bool active = (m_ActiveTextInput == buffer);
    if (hovered && m_Mouse.clicked) m_ActiveTextInput = buffer;

    if (active) {
        for (int i = 'A'; i <= 'Z'; i++) {
            if (LI_FN(GetAsyncKeyState)(i) & 1) {
                int len = (int)strlen(buffer);
                if (len < bufferSize - 1) { buffer[len] = (char)i; buffer[len + 1] = '\0'; }
            }
        }
        for (int i = '0'; i <= '9'; i++) {
            if (LI_FN(GetAsyncKeyState)(i) & 1) {
                int len = (int)strlen(buffer);
                if (len < bufferSize - 1) { buffer[len] = (char)i; buffer[len + 1] = '\0'; }
            }
        }
        if (LI_FN(GetAsyncKeyState)(VK_OEM_PERIOD) & 1) { int len = (int)strlen(buffer); if (len < bufferSize - 1) { buffer[len] = '.'; buffer[len + 1] = '\0'; } }
        if (LI_FN(GetAsyncKeyState)(VK_SPACE) & 1) { int len = (int)strlen(buffer); if (len < bufferSize - 1) { buffer[len] = ' '; buffer[len + 1] = '\0'; } }
        if (LI_FN(GetAsyncKeyState)(VK_BACK) & 1) { int len = (int)strlen(buffer); if (len > 0) buffer[len - 1] = '\0'; }
        if (LI_FN(GetAsyncKeyState)(VK_RETURN) & 1 || LI_FN(GetAsyncKeyState)(VK_ESCAPE) & 1) m_ActiveTextInput = nullptr;
    }

    m_Renderer->RenderText(label, x, y + (m_LineHeight / 2) - (int)(m_Renderer->GetTextSize(label).y / 2), hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawRect(boxX, boxY, boxWidth, boxHeight, 1, BORDER_COLOR);

    // Render text content (clipped)
    int clipLeft = boxX + 4;
    int clipRight = boxX + boxWidth - 4;
    int currentX = clipLeft;
    for (int i = 0; buffer[i] != '\0'; i++) {
        char c[2] = { buffer[i], '\0' };
        Vector2 cs = m_Renderer->GetTextSize(c);
        if (currentX + cs.x > clipRight) break;
        m_Renderer->RenderText(c, currentX, boxY + (boxHeight / 2) - 8, TEXT_COLOR, true);
        currentX += (int)cs.x;
    }

    if (active) {
        m_TextCursorBlink++;
        if ((m_TextCursorBlink / 30) % 2 == 0) {
            m_Renderer->DrawLine(currentX, boxY + 3, currentX, boxY + boxHeight - 3, 1, TEXT_COLOR, true);
        }
    }

    m_CursorY += m_LineHeight + m_Padding;
    return active;
}

bool GUI::ColorPicker(const char* label, uint32_t* color) {
    if (!m_Renderer || !m_Open || !color) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    constexpr int pickerSize = 14;
    int pickerX = x + m_CurrentWidth - (m_Padding * 2) - pickerSize;
    int pickerY = y + (m_LineHeight / 2) - (pickerSize / 2);

    bool hovered = IsMouseInRect(x, y, (pickerX + pickerSize) - x, m_LineHeight);
    m_Renderer->RenderText(label, x, y + (m_LineHeight / 2) - (int)(m_Renderer->GetTextSize(label).y / 2), hovered ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    m_Renderer->DrawFilledRect(pickerX, pickerY, pickerSize, pickerSize, *color);
    m_Renderer->DrawRect(pickerX, pickerY, pickerSize, pickerSize, 1, BORDER_COLOR);

    m_CursorY += m_LineHeight + m_Padding;
    return hovered && m_Mouse.clicked;
}

bool GUI::ListBox(const char* label, int* value, const char** items, int itemCount) {
    if (!m_Renderer || !m_Open || !value || !items || itemCount <= 0) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    int boxWidth = (int)(m_CurrentWidth * 0.35f);

    m_Renderer->DrawFilledRect(x, y, boxWidth, itemCount * (m_LineHeight + 5), BG_COLOR);
    m_Renderer->DrawRect(x, y, boxWidth, itemCount * (m_LineHeight + 5), 1, BORDER_COLOR);

    bool changed = false;
    for (int i = 0; i < itemCount; i++) {
        int itemY = y + (i * (m_LineHeight + 4));
        bool itemHovered = IsMouseInRect(x, itemY, boxWidth, m_LineHeight);
        if (itemHovered && m_Mouse.clicked) { *value = i; changed = true; }
        Vector2 sz = m_Renderer->GetTextSize(items[i]);
        m_Renderer->RenderText(items[i], x + 4, itemY + 4 + (m_LineHeight / 2) - (int)(sz.y / 2), (*value == i || itemHovered) ? TEXT_ACTIVE_COLOR : TEXT_COLOR, true);
    }

    m_CursorY += itemCount * (m_LineHeight + 4) + m_Padding;
    return changed;
}

bool GUI::Button(const char* label) {
    if (!m_Renderer || !m_Open) return false;

    int x = m_CursorX;
    int y = m_CursorY + m_Padding;
    Vector2 textSize = m_Renderer->GetTextSize(label);
    int btnW = (int)textSize.x + m_Padding * 2;
    int btnH = (int)textSize.y + 6;

    bool hovered = IsMouseInRect(x, y, btnW, btnH);

    m_Renderer->DrawFilledRect(x, y, btnW, btnH, hovered ? WIDGET_COLOR : HEADER_COLOR);
    m_Renderer->DrawRect(x, y, btnW, btnH, 1, BORDER_COLOR);
    m_Renderer->RenderText(label, x + m_Padding, y + 3, TEXT_ACTIVE_COLOR, true);

    m_CursorY += btnH + m_Padding;
    return hovered && m_Mouse.clicked;
}
